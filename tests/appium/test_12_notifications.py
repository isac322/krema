# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Real-session notification watcher badge coverage."""

from __future__ import annotations

import subprocess
import sys
import uuid
from pathlib import Path

import pytest

from krema_e2e import env
from krema_e2e.krema import Krema
from krema_e2e.waits import wait_until
from krema_e2e.windows import TestWindows


STIMULUS = Path(__file__).resolve().parents[1] / "notification" / "notify.py"

def _start_server(tmp_path: Path) -> subprocess.Popen[str]:
    ready_file = tmp_path / "notification-server.ready"
    watcher_ready_file = tmp_path / "notification-watcher.ready"
    server = subprocess.Popen(
        [
            sys.executable,
            str(STIMULUS),
            "serve",
            "--ready-file",
            str(ready_file),
            "--watcher-ready-file",
            str(watcher_ready_file),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    try:
        wait_until(
            lambda: server.poll() is None and ready_file.exists(),
            message="test-owned notification server to own its D-Bus name",
        )
        wait_until(
            lambda: server.poll() is None and watcher_ready_file.exists(),
            message="Krema to register its /NotificationWatcher callback",
        )
    except BaseException:
        _stop_server(server)
        raise
    return server


def _stop_server(server: subprocess.Popen[str]) -> None:
    if server.poll() is None:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)


def _stimulate(*args: str) -> str:
    result = subprocess.run(
        [sys.executable, str(STIMULUS), *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def _notify(token: str, label: str) -> int:
    raw_id = _stimulate(
        "notify",
        "--desktop-entry",
        env.TEST_APP_ID,
        "--summary",
        f"{token}-{label}",
        "--body",
        f"External notification {token}-{label}",
    )
    assert raw_id.isdigit(), f"headless stimulus returned notification id {raw_id!r}"
    return int(raw_id)


def _close(notification_id: int) -> None:
    _stimulate("close", str(notification_id))



@pytest.mark.kremarc({"PinnedLaunchers": []})
def test_not001_external_watcher_badge_close(
    krema: Krema, apps: TestWindows, tmp_path: Path
) -> None:
    """A real external Notify reaches the dock badge."""
    apps.open("Alpha")
    krema.wait_for_item("Alpha")
    token = uuid.uuid4().hex[:10]
    tracked_ids: list[int] = []
    server: subprocess.Popen[str] | None = None


    try:
        server = _start_server(tmp_path)

        first = _notify(token, "first")
        tracked_ids.append(first)
        second = _notify(token, "second")
        tracked_ids.append(second)

        def description() -> str:
            item = krema.item_accessible("Alpha")
            if item is None:
                raise RuntimeError("Alpha dock item is not available for notification oracle")
            item.clear_cache()
            return str(item.description or "")

        wait_until(
            lambda: "2 notifications" in description(),
            message="dock item to expose two watcher notifications",
        )
        _close(first)
        wait_until(
            lambda: "1 notifications" in description(),
            message="dock item count to drop after external CloseNotification",
        )
        _close(second)
        wait_until(
            lambda: "notifications" not in description(),
            message="dock item badge to clear after the second close",
        )

    finally:
        scenario_error = sys.exc_info()[1]
        cleanup_errors: list[Exception] = []

        def cleanup_failed(stage: str, error: Exception) -> None:
            cleanup_errors.append(error)
            print(f"NOT-001 cleanup failed ({stage}): {type(error).__name__}: {error}", file=sys.stderr)

        for notification_id in tracked_ids:
            try:
                _close(notification_id)
            except (OSError, subprocess.CalledProcessError):
                pass
        if server is not None:
            try:
                _stop_server(server)
            except Exception as error:
                cleanup_failed("notification-server-stop", error)
        if cleanup_errors and scenario_error is None:
            raise cleanup_errors[0]
