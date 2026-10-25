# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Real-session notification watcher badge coverage."""

from __future__ import annotations

import shutil
import subprocess
import sys
import uuid
from pathlib import Path

import pytest

from krema_e2e import config, env
from krema_e2e.krema import Krema
from krema_e2e.waits import wait_until
from krema_e2e.windows import TestWindows

STIMULUS = Path(__file__).resolve().parents[1] / "notification" / "notify.py"

def _start_server(tmp_path: Path, *, with_slack_sni: bool = False) -> subprocess.Popen[str]:
    ready_file = tmp_path / "notification-server.ready"
    watcher_ready_file = tmp_path / "notification-watcher.ready"
    command = [
        sys.executable,
        str(STIMULUS),
        "serve",
        "--ready-file",
        str(ready_file),
        "--watcher-ready-file",
        str(watcher_ready_file),
    ]
    if with_slack_sni:
        command.append("--with-slack-sni")
    server = subprocess.Popen(
        command,
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


def _notify(token: str, label: str, *, desktop_entry: str = env.TEST_APP_ID) -> int:
    raw_id = _stimulate(
        "notify",
        "--desktop-entry",
        desktop_entry,
        "--summary",
        f"{token}-{label}",
        "--body",
        f"External notification {token}-{label}",
    )
    assert raw_id.isdigit(), f"headless stimulus returned notification id {raw_id!r}"
    return int(raw_id)


def _slack_sni(state: str, tooltip_title: str, tooltip_body: str = "") -> None:
    _stimulate(
        "slack-sni",
        "--state",
        state,
        "--tooltip-title",
        tooltip_title,
        "--tooltip-body",
        tooltip_body,
    )

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


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher("com.slack.Slack.desktop")]})
def test_not002_slack_sni_chat_badge_lifecycle(
    krema: Krema, apps: TestWindows, tmp_path: Path
) -> None:
    """Slack dock badge follows SNI unread/highlight/read state and ignores desktop toast count."""
    apps_dir = krema.home / "data" / "applications"
    apps_dir.mkdir(parents=True, exist_ok=True)
    (apps_dir / "com.slack.Slack.desktop").write_text(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Slack\n"
        "Exec=krema-test-window --app-id com.slack.Slack --title Slack\n"
        "Icon=utilities-terminal\n",
        encoding="utf-8",
    )
    if shutil.which("kbuildsycoca6"):
        subprocess.run(
            ["kbuildsycoca6", "--noincremental"],
            env=krema.environment(),
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    krema.start()
    apps.open("Slack", app_id="com.slack.Slack")
    krema.wait_for_item("Slack")
    token = uuid.uuid4().hex[:10]
    tracked_ids: list[int] = []
    server: subprocess.Popen[str] | None = None

    try:
        server = _start_server(tmp_path, with_slack_sni=True)

        def description() -> str:
            item = krema.item_accessible("Slack")
            if item is None:
                raise RuntimeError("Slack dock item is not available for notification oracle")
            item.clear_cache()
            return str(item.description or "")

        wait_until(
            lambda: "No unread messages" in description()
            and "Unread messages" not in description()
            and "notifications" not in description(),
            message=lambda: f"Slack dock item to expose rest SNI tooltip (got {description()!r})",
        )

        _slack_sni("unread", "You have unread messages")
        toast_id = _notify(token, "slack-toast", desktop_entry="com.slack.Slack")
        tracked_ids.append(toast_id)
        wait_until(
            lambda: "Unread messages" in description()
            and "You have unread messages" in description()
            and "notifications" not in description(),
            message=lambda: f"Slack dock item to expose unread state while suppressing toast count (got {description()!r})",
        )

        _slack_sni("highlight", "5개의 알림이 있습니다.")
        wait_until(
            lambda: "5 notifications" in description()
            and "5개의 알림이 있습니다." in description(),
            message=lambda: f"Slack dock item to expose localized SNI highlight count (got {description()!r})",
        )

        _slack_sni("rest", "읽지 않은 메시지 없음")
        wait_until(
            lambda: "읽지 않은 메시지 없음" in description()
            and "notifications" not in description()
            and "Unread messages" not in description(),
            message=lambda: f"Slack dock item badge to clear immediately when read in-app (got {description()!r})",
        )
    finally:
        scenario_error = sys.exc_info()[1]
        cleanup_errors: list[Exception] = []
        for notification_id in tracked_ids:
            try:
                _close(notification_id)
            except (OSError, subprocess.CalledProcessError):
                pass
        if server is not None:
            try:
                _stop_server(server)
            except Exception as error:
                cleanup_errors.append(error)
        if cleanup_errors and scenario_error is None:
            raise cleanup_errors[0]
