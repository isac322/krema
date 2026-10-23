# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Opt-in NOT-001 Settings-inbox cleanup probe; load explicitly with ``-p``.

The fault comes from the real selenium-webdriver-at-spi server: the test's first
``krema.settings()`` lookup is sent under a session id that server never
registered, so the server rejects it (its session table raises ``KeyError``)
and the real client raises. ``DELETE /session`` cannot be used; that server
keeps attached-by-pid sessions answering. Cleanup (``_close``,
``close_settings``, ``_stop_server``) is never replaced. Expected failures count
as probe passes only after the owned server's SIGTERM exit and the exact
original exception are verified; the oracle is frozen before any probe-only
rescue. This module is neither a test module nor a default pytest plugin.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import signal
import uuid

import pytest


class ScenarioFailure(AssertionError):
    pass


MODES = {"healthy", "cleanup-fault", "original-error"}
MODE = os.environ.get("KREMA_INBOX_CLEANUP_MODE", "")
RUN_DIR = Path(os.environ.get("KREMA_INBOX_CLEANUP_RUN_DIR", ""))
NODE = "test_not001_external_watcher_badge_close_and_local_inbox_lifecycle"
STATE = {
    "item": None,
    "servers": [],
    "scenario_error": None,
    "settings_calls": 0,
    "session_fault": None,
    "settings_error": None,
    "settings_result": None,
}


def _describe(error):
    return None if error is None else f"{type(error).__module__}.{type(error).__qualname__}: {error}"


def pytest_collection_modifyitems(items):
    if MODE not in MODES:
        raise pytest.UsageError(f"KREMA_INBOX_CLEANUP_MODE must be one of {sorted(MODES)}, got {MODE!r}")
    if not RUN_DIR.is_absolute():
        raise pytest.UsageError("KREMA_INBOX_CLEANUP_RUN_DIR must be a NEW absolute directory")
    selected = [item for item in items if item.name == NODE]
    if len(selected) != 1 or len(items) != 1:
        raise pytest.UsageError(f"notification_inbox_cleanup_probe requires exactly one collected item: {NODE}")
    RUN_DIR.mkdir(parents=True, exist_ok=False)
    STATE["item"] = item = selected[0]
    module = item.module
    real_start = module._start_server
    real_open_page = module.open_page

    def start_server(*args, **kwargs):
        server = real_start(*args, **kwargs)
        STATE["servers"].append(server)
        return server

    def open_page(*args, **kwargs):
        result = real_open_page(*args, **kwargs)
        if MODE == "original-error" and STATE["scenario_error"] is None:
            STATE["scenario_error"] = ScenarioFailure("not001-probe-original-scenario-failure")
            raise STATE["scenario_error"]
        return result

    module._start_server, module.open_page = start_server, open_page


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_call(item):
    if item is STATE["item"]:
        krema = item.funcargs["krema"]
        real_settings = krema.settings

        def settings():
            STATE["settings_calls"] += 1
            if MODE != "healthy" and STATE["settings_calls"] == 1:
                real_id = krema.driver.session_id
                unregistered = f"krema-probe-unregistered-{uuid.uuid4()}"
                STATE["session_fault"] = {"real_session_id": real_id, "unregistered_session_id": unregistered}
                krema.driver.session_id = unregistered
                try:
                    STATE["settings_result"] = repr(result := real_settings())
                except Exception as error:
                    STATE["settings_error"] = error
                    raise
                finally:
                    krema.driver.session_id = real_id
                return result
            return real_settings()

        krema.settings = settings
    yield


def _verify(item, call):
    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)

    from test_06_settings import settings_windows
    from selenium.common.exceptions import WebDriverException

    error = call.excinfo.value if call.excinfo else None
    servers = STATE["servers"]
    codes = [server.poll() for server in servers]
    krema = item.funcargs["krema"]
    try:
        windows = [window.title for window in settings_windows(krema)]
    except Exception as window_error:
        windows = f"unavailable: {_describe(window_error)}"
    check(len(servers) == 1, f"expected one real test-owned notification server, saw {len(servers)}")
    check(all(code == -signal.SIGTERM for code in codes), f"owned server not stopped by _stop_server SIGTERM: {codes}")
    check(STATE["settings_calls"] == 1, f"krema.settings() cleanup calls: {STATE['settings_calls']}")
    if MODE == "healthy":
        check(error is None, f"healthy run raised {_describe(error)}")
        check(windows == [], f"Settings window not closed: {windows}")
    else:
        fault = STATE["settings_error"]
        unregistered = (STATE["session_fault"] or {}).get("unregistered_session_id")
        # selenium-webdriver-at-spi: unknown_error() returns errorFromException(), whose
        # JSON message is str(KeyError(session_id)); the client puts it in str(fault).
        check(isinstance(fault, WebDriverException) and unregistered is not None and unregistered in str(fault),
              f"settings fault is not the server's rejection of {unregistered}: {_describe(fault)}")
        check(fault is not None, f"real settings lookup did not fail (returned {STATE['settings_result']})")
        expected = fault if MODE == "cleanup-fault" else STATE["scenario_error"]
        check(expected is not None and error is expected,
              f"raised {_describe(error)} is not the expected original {_describe(expected)}")
    return {
        "mode": MODE,
        "call_outcome": "failed" if error else "passed",
        "raised": _describe(error),
        "scenario_error": _describe(STATE["scenario_error"]),
        "raised_is_scenario_error": error is not None and error is STATE["scenario_error"],
        "settings_fault": _describe(STATE["settings_error"]),
        "settings_fault_type": None if STATE["settings_error"] is None else
        f"{type(STATE['settings_error']).__module__}.{type(STATE['settings_error']).__qualname__}",
        "raised_is_settings_fault": error is not None and error is STATE["settings_error"],
        "session_fault": STATE["session_fault"],
        "settings_calls": STATE["settings_calls"],
        "settings_windows_at_oracle": windows,
        "server_pids": [server.pid for server in servers],
        "server_returncodes_at_oracle": codes,
        "verification_outcome": "failed" if failures else "passed",
        "failures": failures,
    }


def _rescue():
    """Probe-only isolation after the frozen oracle; never counted as test cleanup."""
    actions = []
    for server in STATE["servers"]:
        if server.poll() is not None:
            continue
        action = {"role": "notification-server", "pid": server.pid, "action": "probe SIGKILL+reap"}
        try:
            server.kill()
            action["returncode"] = server.wait(timeout=5)
        except Exception as error:
            action["error"] = _describe(error)
        actions.append(action)
    return {"scope": "probe isolation only; not test cleanup", "actions": actions}


def _write(name, data):
    (RUN_DIR / name).write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if item is not STATE["item"]:
        return
    if call.when == "teardown":
        _write("teardown.json", {"outcome": report.outcome,
                                 "longrepr": str(report.longrepr) if report.longrepr else None})
        path = RUN_DIR / "verification.json"
        if path.exists():
            result = json.loads(path.read_text(encoding="utf-8"))
        else:
            result = {"mode": MODE, "verification_outcome": "failed",
                      "failures": ["No call-phase verification snapshot was written; see pytest reports"]}
        result["teardown_outcome"] = report.outcome
        if report.outcome != "passed":
            result["verification_outcome"] = "failed"
            result["failures"].append(f"fixture teardown {report.outcome}")
        _write("verification.json", result)
        return
    if call.when != "call":
        return
    try:
        result = _verify(item, call)
    except Exception as error:
        result = {"mode": MODE, "verification_outcome": "failed", "failures": [f"verification failed: {_describe(error)}"]}
    try:
        _write("verification.json", result)
    finally:
        rescue = _rescue()
        _write("probe-rescue.json", rescue)
    if rescue["actions"] and result["verification_outcome"] == "passed":
        result["verification_outcome"] = "failed"
        result["failures"].append("probe rescue found a live owned server after a passing oracle")
        _write("verification.json", result)
    if result["verification_outcome"] == "passed":
        report.outcome, report.longrepr = "passed", None
        report.sections.append(("not001 cleanup probe", f"Expected outcome verified: {RUN_DIR / 'verification.json'}"))
    else:
        original = str(report.longrepr) if report.longrepr else "Test call passed."
        report.outcome = "failed"
        report.longrepr = original + "\nNOT-001 cleanup probe verification:\n" + "\n".join(result["failures"])
