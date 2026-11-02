# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Opt-in operation timing: ``pytest -p krema_e2e.profile_ops``.

``KREMA_E2E_PROFILE_OPS_OUTPUT`` overrides ``/artifacts/profile-ops.json``.
Timings include instrumentation overhead and existing waits; self time excludes
only nested instrumented calls in the same thread. Background-thread operations
are aggregate-only. This is not a CPU profiler or an uninstrumented benchmark.
No waits, retries, assertions, or extra calls to wrapped operations are added.
"""

from __future__ import annotations

import atexit
import functools
import hashlib
import importlib
import json
import os
import platform
import resource
import shutil
import sys
import threading
import time
from contextlib import ContextDecorator
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

import pytest


_NOW = time.monotonic_ns


@dataclass
class _Frame:
    started_ns: int
    child_ns: int = 0


class _State:
    def __init__(self) -> None:
        self.started_ns = 0
        self.finished_ns: int | None = None
        self.exitstatus: int | None = None
        self.aggregate: dict[str, dict[str, Any]] = {}
        self.nodes: dict[str, dict[str, Any]] = {}
        self.collection: list[dict[str, Any]] = []
        self.full_collection_manifest: list[dict[str, Any]] = []
        self.selected_nodeids: list[str] = []
        self.omitted: list[str] = []
        self.errors: list[dict[str, str]] = []
        self.installed = False
        self.install_error: str | None = None
        self.provenance: dict[str, Any] = {}
        self.resource_start: dict[str, Any] = {}
        self.resource_end: dict[str, Any] = {}
        self.local = threading.local()

    def stack(self) -> list[_Frame]:
        stack = getattr(self.local, "stack", None)
        if stack is None:
            stack = []
            self.local.stack = stack
        return stack

    def enter_node(self, item: Any) -> str | None:
        previous = getattr(self.local, "nodeid", None)
        self.local.nodeid = str(item.nodeid)
        return previous

    def leave_node(self, previous: str | None) -> None:
        self.local.nodeid = previous

    @staticmethod
    def _add(
        table: dict[str, dict[str, Any]],
        key: str,
        inclusive_ns: int,
        self_ns: int,
        exception: BaseException | None,
    ) -> None:
        bucket = table.get(key)
        if bucket is None:
            bucket = {
                "calls": 0,
                "failures": 0,
                "inclusive_ns": 0,
                "self_ns": 0,
                "max_inclusive_ns": 0,
                "exceptions": {},
            }
            table[key] = bucket
        bucket["calls"] += 1
        bucket["inclusive_ns"] += inclusive_ns
        bucket["self_ns"] += self_ns
        bucket["max_inclusive_ns"] = max(bucket["max_inclusive_ns"], inclusive_ns)
        if exception is not None:
            bucket["failures"] += 1
            name = type(exception).__name__
            errors = bucket["exceptions"]
            errors[name] = errors.get(name, 0) + 1

    def begin_call(self) -> _Frame:
        frame = _Frame(_NOW())
        self.stack().append(frame)
        return frame

    def finish_call(self, key: str, frame: _Frame, exception: BaseException | None) -> None:
        # Detach the frame even when reading the clock fails.
        stack = self.stack()
        if stack and stack[-1] is frame:
            stack.pop()
        else:
            for index, entry in enumerate(stack):
                if entry is frame:
                    del stack[index]
                    break
        inclusive_ns = max(0, _NOW() - frame.started_ns)
        self_ns = max(0, inclusive_ns - frame.child_ns)
        if stack:
            stack[-1].child_ns += inclusive_ns
        self._add(self.aggregate, key, inclusive_ns, self_ns, exception)
        nodeid = getattr(self.local, "nodeid", None)
        if nodeid is not None:
            node = self.nodes.setdefault(nodeid, {"profile": {}, "phases": {}, "status": "unknown"})
            self._add(node["profile"], key, inclusive_ns, self_ns, exception)

    def phase_report(self, report: Any) -> None:
        node = self.nodes.setdefault(str(report.nodeid), {"profile": {}, "phases": {}, "status": "unknown"})
        outcome = str(report.outcome)
        phase = str(report.when)
        longrepr = report.longrepr
        reason = str(longrepr[2]) if isinstance(longrepr, tuple) and len(longrepr) == 3 else str(longrepr or "")
        xfail_reason = getattr(report, "wasxfail", None)
        self._record_phase(node, phase, outcome, report, reason, xfail_reason)

    @staticmethod
    def _record_phase(
        node: dict[str, Any],
        phase: str,
        outcome: str,
        report: Any,
        reason: str,
        xfail_reason: Any,
    ) -> None:
        node["phases"][phase] = {
            "outcome": outcome,
            "duration_ns": max(0, int(float(report.duration) * 1_000_000_000)),
            "longrepr": bool(report.longrepr),
            "skip_reason": reason if outcome == "skipped" else None,
            "xfail_reason": str(xfail_reason) if xfail_reason is not None else None,
            "failure_reason": reason if outcome == "failed" else None,
            "failure_kind": ("failure" if phase == "call" else "error") if outcome == "failed" else None,
        }
        if outcome == "failed":
            status = "failed"
        elif xfail_reason is not None and outcome == "skipped":
            status = "xfailed"
        elif xfail_reason is not None and outcome == "passed":
            status = "xpassed"
        elif outcome == "skipped":
            status = "skipped"
        else:
            status = "passed"
        priority = {"unknown": 0, "passed": 1, "skipped": 2, "xfailed": 3, "xpassed": 4, "failed": 5}
        if priority[status] > priority.get(node.get("status", "unknown"), 0):
            node["status"] = status

    def collect_manifest(self, items: Any) -> None:
        self.full_collection_manifest = _manifest_entries(items)

    def manifest(self, session: Any) -> None:
        self.collection = _manifest_entries(session.items)
        self.selected_nodeids = [entry["nodeid"] for entry in self.collection]
        selected = set(self.selected_nodeids)
        for entry in self.collection:
            entry["selected"] = True
        for entry in self.full_collection_manifest:
            entry["selected"] = entry["nodeid"] in selected

    def snapshot(self) -> dict[str, Any]:
        finished_ns = self.finished_ns if self.finished_ns is not None else _NOW()
        counts = {
            "passed": 0,
            "failed": 0,
            "skipped": 0,
            "xfailed": 0,
            "xpassed": 0,
            "unknown": 0,
        }
        for node in self.nodes.values():
            status = node.get("status", "unknown")
            counts[status if status in counts else "unknown"] += 1
        return {
            "schema_version": 1,
            "plugin": "krema_e2e.profile_ops",
            "started_monotonic_ns": self.started_ns,
            "finished_monotonic_ns": finished_ns,
            "run_duration_ns": max(0, finished_ns - self.started_ns),
            "exitstatus": self.exitstatus,
            "result_counts": counts,
            "topology": _topology(),
            "provenance": self.provenance,
            "resource_usage": {
                "start": self.resource_start,
                "end": self.resource_end,
                "scope": "pytest process and its children only; external KWin/WebDriver daemons are not measured",
                "units": {
                    "cpu": "seconds",
                    "max_rss": "bytes" if sys.platform == "darwin" else "KiB",
                    "block_io": "operations, not bytes",
                },
            },
            "selected_nodeids": self.selected_nodeids,
            "collection_manifest": self.collection,
            "full_collection_manifest": self.full_collection_manifest,
            "nodes": self.nodes,
            "aggregates": self.aggregate,
            "omitted": self.omitted,
            "errors": self.errors,
            "measurements_complete": not (self.errors or self.omitted or self.install_error),
            "install_error": self.install_error,
        }

    def flush(self, exitstatus: int | None = None) -> None:
        if exitstatus is not None:
            self.exitstatus = int(exitstatus)
        if self.finished_ns is None:
            self.finished_ns = _observe(_NOW)
            self.resource_end = _observe(_resource_snapshot) or {}
        output = Path(os.environ.get("KREMA_E2E_PROFILE_OPS_OUTPUT", "/artifacts/profile-ops.json"))
        try:
            payload = self.snapshot()
            output.parent.mkdir(parents=True, exist_ok=True)
            temporary = output.with_name(output.name + ".tmp")
            temporary.write_text(json.dumps(payload, separators=(",", ":"), sort_keys=True), encoding="utf-8")
            os.replace(temporary, output)
        except BaseException as exc:
            _record_error("flush", exc)
            # A failed atomic write may still leave a writable final path.
            # If the destination is unavailable, errors remain in memory.
            _observe(_write_fallback, output)


_STATE = _State()


def _record_error(operation: str, exception: BaseException) -> None:
    try:
        _STATE.errors.append({"operation": operation, "exception": type(exception).__name__})
    except BaseException:
        pass


def _observe(fn: Callable[..., Any], *args: Any, **kwargs: Any) -> Any:
    """Keep profiler failures separate from exceptions in observed operations."""
    try:
        return fn(*args, **kwargs)
    except BaseException as exc:
        _record_error(getattr(fn, "__name__", "profiling"), exc)
        return None


def _write_fallback(output: Path) -> None:
    output.write_text(json.dumps(_STATE.snapshot(), separators=(",", ":"), sort_keys=True), encoding="utf-8")


def _env_int(name: str, default: int) -> int:
    try:
        return int(os.environ.get(name, str(default)))
    except (TypeError, ValueError) as exc:
        _record_error(name, exc)
        return default


def _topology() -> dict[str, Any]:
    values: dict[str, Any] = {
        "output_count": _env_int("KREMA_E2E_OUTPUT_COUNT", 1),
        "screen_width": _env_int("KREMA_E2E_SCREEN_WIDTH", 1024),
        "screen_height": _env_int("KREMA_E2E_SCREEN_HEIGHT", 768),
        "shard": os.environ.get("KREMA_E2E_SHARD"),
    }
    for key in ("KREMA_E2E_BINARY", "KREMA_E2E_DISTRO", "FLASK_PORT"):
        if key in os.environ:
            values[key.lower()] = os.environ[key]
    return values


def _manifest_entries(items: Any) -> list[dict[str, Any]]:
    session_outputs = _topology()["output_count"]
    entries = []
    for item in items:
        marker = item.get_closest_marker("outputs")
        entries.append({
            "nodeid": str(item.nodeid),
            "requested_outputs": marker.args[0] if marker is not None and marker.args else 1,
            "session_outputs": session_outputs,
        })
    return entries


def _resource_snapshot() -> dict[str, Any]:
    values: dict[str, Any] = {}
    for scope, who in (("self", resource.RUSAGE_SELF), ("children", resource.RUSAGE_CHILDREN)):
        usage = resource.getrusage(who)
        values[scope] = {
            "user_cpu_seconds": usage.ru_utime,
            "system_cpu_seconds": usage.ru_stime,
            "max_rss": usage.ru_maxrss,
            "minor_page_faults": usage.ru_minflt,
            "major_page_faults": usage.ru_majflt,
            "block_input_operations": usage.ru_inblock,
            "block_output_operations": usage.ru_oublock,
        }
    return values


def _binary_sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as binary:
        for chunk in iter(lambda: binary.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _provenance() -> dict[str, Any]:
    revision_env = next((name for name in ("KREMA_E2E_SOURCE_REVISION", "GITHUB_SHA") if os.environ.get(name)), None)
    configured = os.environ.get("KREMA_E2E_BINARY", "krema")
    binary = shutil.which(configured) or configured
    environment = sys.modules.get("krema_e2e.env")
    qt_version = getattr(environment, "QT_VERSION", None)
    return {
        "source_revision": os.environ[revision_env] if revision_env else None,
        "source_revision_env": revision_env,
        "binary_path": binary,
        "binary_sha256": _observe(_binary_sha256, binary),
        "qt_runtime_version": list(qt_version) if qt_version is not None else None,
        "python_version": platform.python_version(),
        "system": platform.system(),
        "machine": platform.machine(),
    }


def _record_call(key: str, fn: Callable[..., Any], args: tuple[Any, ...], kwargs: dict[str, Any]) -> Any:
    frame = _observe(_STATE.begin_call)
    exception: BaseException | None = None
    try:
        return fn(*args, **kwargs)
    except BaseException as exc:
        exception = exc
        raise
    finally:
        if frame is not None:
            _observe(_STATE.finish_call, key, frame, exception)


def _wrap(
    fn: Callable[..., Any],
    key: str,
    key_factory: Callable[[tuple[Any, ...], dict[str, Any]], str] | None = None,
) -> Callable[..., Any]:
    if getattr(fn, "__profile_ops_wrapped__", False):
        return fn

    @functools.wraps(fn)
    def wrapped(*args: Any, **kwargs: Any) -> Any:
        actual_key = _observe(key_factory, args, kwargs) if key_factory is not None else key
        return _record_call(key if actual_key is None else actual_key, fn, args, kwargs)

    wrapped.__profile_ops_wrapped__ = True
    wrapped.__profile_ops_original__ = fn
    return wrapped


class _TimedContext(ContextDecorator):
    """Time a context's lifetime without delaying the original factory call."""

    def __init__(self, context: Any, key: str) -> None:
        self.context = context
        self.key = key
        self.frame: _Frame | None = None

    def _recreate_cm(self) -> Any:
        context = self.context._recreate_cm()
        timed = _observe(_TimedContext, context, self.key)
        return context if timed is None else timed

    def __enter__(self) -> Any:
        self.frame = _observe(_STATE.begin_call)
        try:
            return self.context.__enter__()
        except BaseException as exc:
            if self.frame is not None:
                _observe(_STATE.finish_call, self.key, self.frame, exc)
            self.frame = None
            raise

    def __exit__(self, exc_type: Any, exc: Any, traceback: Any) -> Any:
        exception = exc
        try:
            suppressed = self.context.__exit__(exc_type, exc, traceback)
            if suppressed:
                exception = None
            return suppressed
        except BaseException as exit_exc:
            exception = exit_exc
            raise
        finally:
            if self.frame is not None:
                _observe(_STATE.finish_call, self.key, self.frame, exception)
            self.frame = None


def _wrap_contextmanager(fn: Callable[..., Any], key: str) -> Callable[..., Any]:
    if getattr(fn, "__profile_ops_wrapped__", False):
        return fn

    @functools.wraps(fn)
    def wrapped(*args: Any, **kwargs: Any) -> Any:
        context = fn(*args, **kwargs)
        timed = _observe(_TimedContext, context, key)
        return context if timed is None else timed

    wrapped.__profile_ops_wrapped__ = True
    return wrapped


def _remote_command_key(args: tuple[Any, ...], kwargs: dict[str, Any]) -> str:
    command = kwargs.get("command")
    if command is None and len(args) > 1:
        command = args[1]
    return f"selenium.RemoteConnection.execute[{command}]"


def _patch_attribute(
    owner: Any,
    name: str,
    key: str,
    *,
    key_factory: Callable[[tuple[Any, ...], dict[str, Any]], str] | None = None,
    contextmanager_fn: bool = False,
) -> bool:
    try:
        original = getattr(owner, name)
        wrapped = _wrap_contextmanager(original, key) if contextmanager_fn else _wrap(original, key, key_factory)
        setattr(owner, name, wrapped)
        return True
    except BaseException as exc:
        _record_error(key, exc)
        _observe(_STATE.omitted.append, key)
        return False


def _patch_wait_aliases(waits: Any) -> None:
    # Package re-exports and helper modules may have imported waits before
    # sessionstart. Replace references by identity, without layering wrappers
    # on the same callable or attributing aliases to different operation keys.
    wrappers: dict[int, Callable[..., Any]] = {}
    for name in ("wait_until", "wait_stable"):
        current = getattr(waits, name)
        original = getattr(current, "__profile_ops_original__", current)
        wrapper = wrappers.get(id(original))
        if wrapper is None:
            wrapper = current if getattr(current, "__profile_ops_wrapped__", False) else _wrap(original, f"waits.{name}")
            wrappers[id(original)] = wrapper
        for module_name, module in tuple(sys.modules.items()):
            if module_name != "krema_e2e" and not module_name.startswith("krema_e2e."):
                continue
            for alias, value in tuple(vars(module).items()):
                value_original = getattr(value, "__profile_ops_original__", value)
                if value_original is original:
                    setattr(module, alias, wrapper)


def _install() -> None:
    if _STATE.installed:
        return
    _STATE.installed = True
    _STATE.started_ns = _NOW()
    _STATE.resource_start = _observe(_resource_snapshot) or {}
    for module_name, class_name, names in (
        ("krema", "Krema", (
            "__init__", "start", "stop", "restart", "is_running", "find", "find_all", "wait_for",
            "toolbar", "items", "item_names", "item", "wait_for_item", "item_accessible", "wait_for_no_item",
            "focused_item", "wait_keyboard_focus", "preview_popup", "preview_visible", "thumbnails", "settings",
            "page_source", "windows", "surface_rect", "to_screen", "screen_rect", "item_center",
            "settled_item_center", "hover_item", "click_item", "scroll_item", "move_away", "open_context_menu",
            "choose_context_menu_entry", "open_dock_context_menu", "open_dock_context_menu_keyboard", "open_settings",
            "screenshot",
        )),
        ("kwin", None, (
            "evaluate", "windows", "app_windows", "active_window", "activate", "set_minimized", "cursor_pos",
            "compositing_type", "can_capture", "screenshot",
        )),
        ("input", None, (
            "run_actions", "background_actions", "pointer_position", "hold_pointer_capability",
            "release_pointer_capability", "drop_pointer_capability", "move", "move_path", "click", "press",
            "release", "drag", "scroll", "key", "type_text",
        )),
    ):
        try:
            module = importlib.import_module(f"krema_e2e.{module_name}")
            owner = getattr(module, class_name) if class_name is not None else module
            prefix = f"{module_name}.{class_name}" if class_name is not None else module_name
            for name in names:
                _patch_attribute(owner, name, f"{prefix}.{name}", contextmanager_fn=(name == "background_actions"))
        except BaseException as exc:
            _record_error(f"install {module_name}", exc)
            _STATE.omitted.append(f"{module_name}.*")
            _STATE.install_error = type(exc).__name__
    try:
        waits = importlib.import_module("krema_e2e.waits")
        _patch_wait_aliases(waits)
    except BaseException as exc:
        _record_error("install waits", exc)
        _STATE.omitted.append("waits.*")
        _STATE.install_error = type(exc).__name__
    try:
        remote = importlib.import_module("selenium.webdriver.remote.remote_connection")
        _patch_attribute(remote.RemoteConnection, "execute", "selenium.RemoteConnection.execute", key_factory=_remote_command_key)
    except BaseException as exc:
        _record_error("install selenium", exc)
        _STATE.omitted.append("selenium.RemoteConnection.execute")
        _STATE.install_error = type(exc).__name__
    _STATE.provenance = _observe(_provenance) or {}


def _flush_at_exit() -> None:
    _observe(_STATE.flush)


_observe(atexit.register, _flush_at_exit)


def pytest_sessionstart(session: Any) -> None:
    _observe(_install)


@pytest.hookimpl(hookwrapper=True, tryfirst=True)
def pytest_collection_modifyitems(session: Any, config: Any, items: Any) -> Any:
    # Enter before pytest's -k/-m deselection and the harness shard hook.
    _observe(_STATE.collect_manifest, items)
    yield


def pytest_collection_finish(session: Any) -> None:
    _observe(_STATE.manifest, session)


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_protocol(item: Any, nextitem: Any) -> Any:
    previous = _observe(_STATE.enter_node, item)
    try:
        yield
    finally:
        _observe(_STATE.leave_node, previous)


def pytest_runtest_logreport(report: Any) -> None:
    _observe(_STATE.phase_report, report)


def pytest_sessionfinish(session: Any, exitstatus: int) -> None:
    _observe(_STATE.flush, exitstatus)
