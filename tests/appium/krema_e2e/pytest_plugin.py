# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""pytest fixtures: fresh isolated krema + fixture windows per test, failure
artifacts (screenshot, AT-SPI tree, kremarc, KWin window list)."""

from __future__ import annotations

import json
import os
import re
from dataclasses import asdict
from pathlib import Path
from typing import Any, Iterator

import pytest

from . import env, kwin
from . import input as inp
from .krema import Krema
from .windows import TestWindows


def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("krema_e2e", "Krema AT-SPI E2E options")
    group.addoption(
        "--only-matching-outputs",
        action="store_true",
        default=False,
        help=(
            "Deselect tests whose @pytest.mark.outputs(n) count (default 1) "
            "differs from KREMA_E2E_OUTPUT_COUNT instead of skipping them."
        ),
    )
    group.addoption(
        "--list-multi-outputs",
        metavar="PATH",
        default=None,
        help=(
            "Write each collected @pytest.mark.outputs(n) count >= 2 "
            "(ascending, one per line) to PATH and exit without running tests."
        ),
    )


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line(
        "markers",
        "kremarc(settings): kremarc written before krema starts "
        "(same format as krema_e2e.config.write_kremarc; {} = krema defaults)",
    )
    config.addinivalue_line("markers", "no_krema_autostart: the krema fixture does not start krema")
    config.addinivalue_line(
        "markers",
        "outputs(n): needs a session with n virtual outputs (KREMA_E2E_OUTPUT_COUNT=n); "
        "unmarked tests need exactly one",
    )


def _shard() -> tuple[int, int] | None:
    """Parse ``KREMA_E2E_SHARD=<index>/<count>``; ``None`` when unset or 1."""
    spec = os.environ.get("KREMA_E2E_SHARD")
    if spec is None:
        return None
    match = re.fullmatch(r"(\d+)/(\d+)", spec.strip())
    if match is None:
        raise pytest.UsageError(
            f"KREMA_E2E_SHARD must be <index>/<count>, e.g. 1/3 (got {spec!r})"
        )
    index, count = int(match[1]), int(match[2])
    if count <= 1:
        return None
    if index >= count:
        raise pytest.UsageError(
            f"KREMA_E2E_SHARD index {index} out of range for {count} shard(s) "
            f"(valid: 0..{count - 1})"
        )
    return index, count


def _wanted_outputs(item: pytest.Item) -> int:
    marker = item.get_closest_marker("outputs")
    return int(marker.args[0]) if marker is not None else 1


@pytest.hookimpl(trylast=True)
def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    """Filter or shard collected tests, then skip tests whose output count
    differs from this session's when ``--only-matching-outputs`` is not set."""
    list_path = config.getoption("--list-multi-outputs")
    if list_path:
        counts = sorted({n for item in items if (n := _wanted_outputs(item)) >= 2})
        out_file = Path(list_path)
        out_file.parent.mkdir(parents=True, exist_ok=True)
        out_file.write_text("".join(f"{n}\n" for n in counts), encoding="utf-8")
        return

    if config.getoption("--only-matching-outputs"):
        matching: list[pytest.Item] = []
        mismatched: list[pytest.Item] = []
        for item in items:
            (matching if _wanted_outputs(item) == env.OUTPUT_COUNT else mismatched).append(item)
        if mismatched:
            config.hook.pytest_deselected(items=mismatched)
            items[:] = matching

    shard = _shard()
    workqueue = os.environ.get("KREMA_E2E_WORKQUEUE")
    if shard is not None and not workqueue:
        index, count = shard
        # Static fallback when KREMA_E2E_WORKQUEUE is unset (e.g. reproducing a
        # standalone shard locally): deterministic round-robin over post-filter items.
        keep: list[pytest.Item] = []
        deselected: list[pytest.Item] = []
        for pos, item in enumerate(items):
            (keep if pos % count == index else deselected).append(item)
        if deselected:
            config.hook.pytest_deselected(items=deselected)
            items[:] = keep

    for item in items:
        wanted = _wanted_outputs(item)
        if wanted != env.OUTPUT_COUNT:
            item.add_marker(
                pytest.mark.skip(
                    reason=f"needs {wanted} output(s), session has {env.OUTPUT_COUNT}; "
                    f"run with KREMA_E2E_OUTPUT_COUNT={wanted}"
                )
            )


class _SessionKeepAlive:
    """Sentinel ``nextitem`` keeping ``scope='session'`` fixtures alive while
    tearing down function, class, and module fixtures after each claimed item."""

    def __init__(self, session: pytest.Session) -> None:
        self._chain = [session]

    def listchain(self) -> list[Any]:
        return self._chain


@pytest.hookimpl(tryfirst=True)
def pytest_runtestloop(session: pytest.Session) -> bool | None:
    if session.config.getoption("--list-multi-outputs"):
        if session.testsfailed and not session.config.option.continue_on_collection_errors:
            raise session.Interrupted(f"{session.testsfailed} error during collection")
        return True

    queue_env = os.environ.get("KREMA_E2E_WORKQUEUE")
    if not queue_env:
        return None
    if session.testsfailed and not session.config.option.continue_on_collection_errors:
        raise session.Interrupted(f"{session.testsfailed} error during collection")
    if session.config.option.collectonly:
        return True

    queue_dir = Path(queue_env)
    if not queue_dir.is_dir():
        raise pytest.UsageError(f"KREMA_E2E_WORKQUEUE directory does not exist: {queue_dir}")

    shard_label = os.environ.get("KREMA_E2E_SHARD", "0/1")
    all_items = list(session.items)
    claimed: list[pytest.Item] = []
    keep_session = _SessionKeepAlive(session)
    scan_from = 0
    total = len(all_items)

    try:
        while scan_from < total:
            claimed_idx: int | None = None
            for idx in range(scan_from, total):
                claim_path = queue_dir / f"claim-{idx:05d}"
                try:
                    fd = os.open(claim_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o666)
                except FileExistsError:
                    continue
                try:
                    os.write(fd, f"{shard_label}\t{all_items[idx].nodeid}\n".encode())
                finally:
                    os.close(fd)
                claimed_idx = idx
                scan_from = idx + 1
                break
            if claimed_idx is None:
                break
            item = all_items[claimed_idx]
            claimed.append(item)
            item.config.hook.pytest_runtest_protocol(item=item, nextitem=keep_session)
            if session.shouldfail:
                raise session.Failed(session.shouldfail)
            if session.shouldstop:
                raise session.Interrupted(session.shouldstop)
    finally:
        session._setupstate.teardown_exact(None)
        session.items[:] = claimed
    return True

@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item: pytest.Item, call: pytest.CallInfo) -> Iterator[None]:
    outcome = yield
    rep = outcome.get_result()
    setattr(item, f"rep_{rep.when}", rep)


def _artifact_name(nodeid: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", nodeid).strip("_")


#: Pointer rest position between tests: mid-screen, away from the dock.
POINTER_PARK = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)


@pytest.fixture(scope="session", autouse=True)
def _pointer_capability() -> Iterator[None]:
    """Keep KWin's seat pointer capability for the whole session (see
    krema_e2e.input.hold_pointer_capability)."""
    inp.hold_pointer_capability(POINTER_PARK)
    yield
    # Also ends a keep-alive a test started after drop_pointer_capability().
    inp.release_pointer_capability()


@pytest.fixture
def apps() -> Iterator[TestWindows]:
    """Opens krema-test-window fixture windows; closes leftovers afterwards."""
    manager = TestWindows()
    yield manager
    manager.close_all()


@pytest.fixture
def krema(request: pytest.FixtureRequest, tmp_path_factory: pytest.TempPathFactory, apps: TestWindows) -> Iterator[Krema]:
    """A started krema with private XDG dirs.

    Config precedence: ``@pytest.mark.kremarc({...})`` > class attribute
    ``kremarc`` > krema_e2e.krema.DEFAULT_CONFIG. Mark the test
    ``no_krema_autostart`` to call ``krema.start()`` yourself.

    The pointer is parked at :data:`POINTER_PARK` first, so no test starts
    with it resting where the previous one left it (e.g. on the spot where
    this test's dock item will appear, which would start the test hovered).
    """
    inp.move(*POINTER_PARK)
    marker = request.node.get_closest_marker("kremarc")
    settings: Any = None
    if marker is not None:
        settings = marker.args[0]
    elif request.cls is not None and getattr(request.cls, "kremarc", None) is not None:
        settings = request.cls.kremarc
    name = _artifact_name(request.node.nodeid)
    instance = Krema(tmp_path_factory.mktemp("krema-home"), settings, name=name)
    if request.node.get_closest_marker("no_krema_autostart") is None:
        instance.start()
    yield instance
    rep = getattr(request.node, "rep_call", None)
    if rep is not None and rep.failed:
        _collect_failure_artifacts(instance, name)
    crashed = instance.process is not None and instance.process.poll() is not None
    code = instance.process.returncode if crashed else None
    instance.stop()
    if crashed:
        pytest.fail(f"krema exited during the test (code {code}); log: {instance.log_path}")


def _collect_failure_artifacts(instance: Krema, name: str) -> None:
    try:
        kwin.screenshot(env.artifact_path(f"{name}/failure.png"))
    except Exception as e:  # noqa: BLE001 - best effort
        env.artifact_path(f"{name}/failure-screenshot-error.txt").write_text(repr(e))
    try:
        if instance.driver is not None:
            env.artifact_path(f"{name}/atspi-tree.xml").write_text(instance.page_source())
    except Exception as e:  # noqa: BLE001
        env.artifact_path(f"{name}/atspi-tree-error.txt").write_text(repr(e))
    try:
        env.artifact_path(f"{name}/kwin-windows.json").write_text(json.dumps([asdict(w) for w in kwin.windows()], indent=2))
    except Exception as e:  # noqa: BLE001
        env.artifact_path(f"{name}/kwin-windows-error.txt").write_text(repr(e))
    if instance.config_path.exists():
        env.artifact_path(f"{name}/kremarc").write_text(instance.config_path.read_text())


class KremaTest:
    """Optional base class: ``self.krema`` and ``self.apps`` are set for every
    test method; set the class attribute ``kremarc`` for a shared config."""

    kremarc: dict | None = None
    krema: Krema
    apps: TestWindows

    @pytest.fixture(autouse=True)
    def _krema_e2e_inject(self, krema: Krema, apps: TestWindows) -> None:
        self.krema = krema
        self.apps = apps
