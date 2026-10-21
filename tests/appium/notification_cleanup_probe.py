# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Opt-in real-recorder cleanup probes; load explicitly with ``-p``.

Expected recording failures count as probe passes only after their original
exception, real resource cleanup, and honest failed metadata are verified.
This module is neither a test module nor a default pytest plugin.
"""

from __future__ import annotations

import inspect
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

import pytest


class ScenarioFailure(AssertionError):
    pass


MODE = os.environ.get("KREMA_CLEANUP_MODE", "healthy")
RUN_DIR = Path(os.environ.get("KREMA_CLEANUP_RUN_DIR", ""))
STATE = {
    "cleanup": False,
    "servers": [],
    "live_server_pids": [],
    "stopped_server_pids": [],
    "close_calls": 0,
    "release_calls": 0,
    "move_calls": 0,
    "kded": None,
    "terminate_calls": 0,
    "wait_calls": 0,
    "fault": None,
    "scenario_error": None,
    "module_reply": None,
    "item": None,
}


def pytest_collection_modifyitems(items):
    if MODE not in {"healthy", "dnd-reset", "kded-wait", "original-error", "events-write", "sni-setup"}:
        raise pytest.UsageError(f"Unknown KREMA_CLEANUP_MODE: {MODE}")
    if not RUN_DIR.is_absolute():
        raise pytest.UsageError("KREMA_CLEANUP_RUN_DIR must be a NEW absolute directory")
    selected = [item for item in items if item.name == "test_record_notifications"]
    if len(selected) != 1:
        raise pytest.UsageError("notification_cleanup_probe requires exactly one test_record_notifications item")
    RUN_DIR.mkdir(parents=True, exist_ok=False)
    item = selected[0]
    recorder = item.module
    recorder.env.ARTIFACTS_DIR = RUN_DIR / "recorder"
    STATE["item"] = item
    STATE["evidence"] = recorder.env.artifact_path("visual-evidence/notifications")
    _instrument(recorder)


def _instrument(recorder):
    real_fresh = recorder._fresh_server
    real_stop = recorder._stop_server
    real_close = recorder._close
    real_sni = recorder._start_sni
    real_dnd = recorder._set_dnd_until

    def track_kded(process):
        STATE["kded"] = process
        if process is None:
            return
        real_terminate, real_wait = process.terminate, process.wait

        def terminate():
            STATE["terminate_calls"] += 1
            if MODE == "kded-wait":
                os.kill(process.pid, signal.SIGSTOP)
                pid, status = os.waitpid(process.pid, os.WUNTRACED)
                if pid != process.pid or not os.WIFSTOPPED(status):
                    raise RuntimeError("owned kded did not enter SIGSTOP")
            return real_terminate()

        def wait(timeout=None):
            STATE["wait_calls"] += 1
            if MODE != "kded-wait":
                return real_wait(timeout=timeout)
            try:
                return real_wait(timeout=0.05)
            except subprocess.TimeoutExpired as error:
                STATE["fault"] = f"{type(error).__name__}: {error}"
                raise
            finally:
                if process.poll() is None:
                    os.kill(process.pid, signal.SIGCONT)
                real_wait(timeout=5)

        process.terminate, process.wait = terminate, wait

    def fresh(*args, **kwargs):
        process = real_fresh(*args, **kwargs)
        STATE["servers"].append(process)
        return process

    def stop(process):
        if STATE["cleanup"]:
            STATE["stopped_server_pids"].append(process.pid)
        return real_stop(process)

    def close(notification_id):
        if STATE["cleanup"]:
            STATE["close_calls"] += 1
        return real_close(notification_id)

    def failing_sni_setup(*args, **kwargs):
        real_popen, real_interface = recorder.subprocess.Popen, recorder.dbus.Interface

        def popen(command, *popen_args, **popen_kwargs):
            process = real_popen(command, *popen_args, **popen_kwargs)
            if command[0] == "kded6":
                track_kded(process)
            return process

        def interface(proxy, dbus_interface=None, **interface_kwargs):
            result = real_interface(proxy, dbus_interface, **interface_kwargs)
            if dbus_interface == "org.kde.kded6":
                real_load = result.loadModule

                def load_module(_requested):
                    reply = real_load("krema_cleanup_regression_missing_module")
                    STATE["module_reply"] = bool(reply)
                    STATE["fault"] = f"real kded invalid-module reply: {bool(reply)}"
                    return reply

                result.loadModule = load_module
            return result

        recorder.subprocess.Popen, recorder.dbus.Interface = popen, interface
        try:
            return real_sni(*args, **kwargs)
        except BaseException as error:
            STATE["scenario_error"] = error
            raise
        finally:
            recorder.subprocess.Popen, recorder.dbus.Interface = real_popen, real_interface

    def start_sni(*args, **kwargs):
        if MODE == "sni-setup":
            return failing_sni_setup(*args, **kwargs)
        sni, kded = real_sni(*args, **kwargs)
        track_kded(kded)
        real_release = sni.unregister_with_watcher

        def release():
            STATE["release_calls"] += 1
            return real_release()

        sni.unregister_with_watcher = release
        return sni, kded

    def set_dnd(krema, until):
        caller = inspect.currentframe().f_back
        cleanup = until is None and (
            caller.f_locals.get("scenario_completed") is True or sys.exc_info()[1] is not None
        )
        if MODE == "original-error" and until is not None:
            STATE["scenario_error"] = ScenarioFailure("cleanup-probe-original-scenario-failure")
            raise STATE["scenario_error"]
        if not cleanup:
            return real_dnd(krema, until)
        STATE["cleanup"] = True
        STATE["live_server_pids"] = [process.pid for process in STATE["servers"] if process.poll() is None]
        if MODE == "events-write":
            path = STATE["evidence"] / "events.jsonl"
            path.mkdir()
            STATE["fault"] = f"real directory collision: {path}"
        if MODE not in {"dnd-reset", "original-error"}:
            return real_dnd(krema, until)
        writer = recorder.CONFIG_WRITER
        recorder.CONFIG_WRITER = RUN_DIR / "missing-config-writer"
        try:
            return real_dnd(krema, until)
        except FileNotFoundError as error:
            STATE["fault"] = f"{type(error).__name__}: {error}"
            raise
        finally:
            recorder.CONFIG_WRITER = writer

    recorder._fresh_server, recorder._stop_server, recorder._close = fresh, stop, close
    recorder._start_sni, recorder._set_dnd_until = start_sni, set_dnd


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_call(item):
    if item is STATE["item"]:
        krema = item.funcargs["krema"]
        real_move = krema.move_away

        def move(*args, **kwargs):
            if STATE["cleanup"]:
                STATE["move_calls"] += 1
            return real_move(*args, **kwargs)

        krema.move_away = move
    yield


def _verify(call):
    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)

    error = call.excinfo.value if call.excinfo else None
    try:
        metadata = json.loads((STATE["evidence"] / "metadata.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as artifact_error:
        failures.append(f"metadata unavailable: {artifact_error}")
        metadata = {}
    events = STATE["evidence"] / "events.jsonl"
    check(events.is_dir() if MODE == "events-write" else events.is_file(), "incorrect event artifact")
    check(STATE["cleanup"], "recorder cleanup was not reached")
    check(STATE["release_calls"] == (0 if MODE == "sni-setup" else 1), "incorrect SNI release count")
    check(STATE["close_calls"] > 0, "tracked notification cleanup was skipped")
    check(STATE["move_calls"] == 1, "pointer cleanup was skipped or duplicated")
    processes = STATE["servers"]
    check(bool(processes), "no real notification servers were observed")
    check(all(process.poll() is not None for process in processes), "owned notification server remains alive")
    check(set(STATE["live_server_pids"]) <= set(STATE["stopped_server_pids"]), "server cleanup was skipped")
    kded = STATE["kded"]
    if MODE in {"kded-wait", "sni-setup"}:
        check(kded is not None, "probe requires a real recorder-owned kded child")
    if kded is not None:
        check(STATE["terminate_calls"] == 1, "kded termination was skipped or duplicated")
        check(STATE["wait_calls"] == 1, "kded wait was skipped or duplicated")
        check(kded.poll() is not None, "owned kded remains alive")
    failed_scenario = MODE in {"original-error", "sni-setup"}
    failed_cleanup = MODE not in {"healthy", "sni-setup"}
    check(metadata.get("outcome") == ("passed" if MODE == "healthy" else "failed"), "dishonest overall outcome")
    check(metadata.get("scenario_outcome") == ("failed" if failed_scenario else "passed"), "incorrect scenario outcome")
    check(metadata.get("cleanup_outcome") == ("failed" if failed_cleanup else "passed"), "incorrect cleanup outcome")
    if MODE == "healthy":
        check(error is None, f"healthy recorder raised {type(error).__name__}: {error}")
    else:
        check(STATE["fault"] is not None, "real requested fault was not observed")
        if failed_scenario:
            check(error is STATE["scenario_error"] and error is not None, "original scenario exception was replaced")
            check(metadata.get("scenario_error") == f"{type(error).__name__}: {error}", "metadata lost original exception")
        else:
            check(isinstance(error, RuntimeError) and "Notification recording cleanup failed:" in str(error),
                  f"unexpected primary recording failure: {type(error).__name__}: {error}")
    if MODE == "sni-setup":
        check(STATE["module_reply"] is False, "real kded did not reject the nonexistent module")
        check(isinstance(error, RuntimeError) and str(error) ==
              "kded6 did not load the installed statusnotifierwatcher module", "incorrect SNI setup failure")
    if failed_cleanup:
        stage, kind = {
            "dnd-reset": ("dnd-reset", "FileNotFoundError"),
            "original-error": ("dnd-reset", "FileNotFoundError"),
            "kded-wait": ("kded-wait", "TimeoutExpired"),
            "events-write": ("events-write", "IsADirectoryError"),
        }[MODE]
        check(any(entry.get("stage") == stage and kind in entry.get("error", "")
                  for entry in metadata.get("cleanup_errors", [])), f"metadata lacks {stage}/{kind}")
    else:
        check(metadata.get("cleanup_errors") == [], "unexpected cleanup errors")
    return {
        "mode": MODE,
        "recording_call_outcome": "failed" if error else "passed",
        "recording_exception": None if error is None else f"{type(error).__name__}: {error}",
        "original_scenario_exception_preserved": error is STATE["scenario_error"] if failed_scenario else None,
        "fault_observed": STATE["fault"],
        "sni_module_reply": STATE["module_reply"],
        "notification_server_pids": [process.pid for process in processes],
        "notification_servers_stopped": [process.poll() is not None for process in processes],
        "kded_pid": kded.pid if kded is not None else None,
        "kded_stopped": kded.poll() is not None if kded is not None else None,
        "recording_metadata": metadata,
        "sni_release_calls": STATE["release_calls"],
        "notification_close_calls": STATE["close_calls"],
        "kded_terminate_calls": STATE["terminate_calls"],
        "kded_wait_calls": STATE["wait_calls"],
        "move_away_calls": STATE["move_calls"],
        "verification_outcome": "failed" if failures else "passed",
        "failures": failures,
    }


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if item is not STATE["item"] or call.when != "call":
        return
    try:
        result = _verify(call)
    except Exception as error:
        result = {"mode": MODE, "verification_outcome": "failed", "failures": [f"verification failed: {error}"]}
    path = RUN_DIR / "verification.json"
    path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if result["verification_outcome"] == "passed":
        report.outcome, report.longrepr = "passed", None
        report.sections.append(("cleanup probe", f"Expected recorder outcome verified: {path}"))
    else:
        original = str(report.longrepr) if report.longrepr else "Recorder call passed."
        report.outcome = "failed"
        report.longrepr = original + "\nCleanup probe verification:\n" + "\n".join(result["failures"])
