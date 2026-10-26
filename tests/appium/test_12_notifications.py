# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Real-session notification watcher badge coverage."""

from __future__ import annotations

import shutil
import subprocess
import sys
import time
import uuid
from pathlib import Path

import pytest

from krema_e2e import config, env
from krema_e2e.krema import Krema
from krema_e2e.waits import wait_until
from krema_e2e.windows import TestWindows

STIMULUS = Path(__file__).resolve().parents[1] / "notification" / "notify.py"

def _start_server(
    tmp_path: Path,
    *,
    with_slack_sni: bool = False,
    chat_sni: tuple[str, str] | None = None,
) -> subprocess.Popen[str]:
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
    if chat_sni is not None:
        command += ["--chat-sni-id", chat_sni[0], "--chat-sni-title", chat_sni[1]]
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


def _expire(notification_id: int) -> None:
    _stimulate("expire", str(notification_id))


def _chat_sni(tooltip_title: str, *, badge: str = "rest", icon_name: str = "", tooltip_body: str = "") -> None:
    _stimulate(
        "chat-sni",
        "--badge",
        badge,
        "--icon-name",
        icon_name,
        "--tooltip-title",
        tooltip_title,
        "--tooltip-body",
        tooltip_body,
    )


def _install_app(krema: Krema, desktop_id: str, name: str, app_id: str) -> None:
    """Give the dock a real launcher for a chat app under test."""
    apps_dir = krema.home / "data" / "applications"
    apps_dir.mkdir(parents=True, exist_ok=True)
    (apps_dir / desktop_id).write_text(
        "[Desktop Entry]\n"
        "Type=Application\n"
        f"Name={name}\n"
        f"Exec=krema-test-window --app-id {app_id} --title {name}\n"
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


def _dock_description(krema: Krema, name: str):
    def description() -> str:
        item = krema.item_accessible(name)
        if item is None:
            raise RuntimeError(f"{name} dock item is not available for notification oracle")
        item.clear_cache()
        return str(item.description or "")

    return description


def _holds(predicate, seconds: float = 1.5) -> bool:
    """True when ``predicate`` stays true for the whole window (a negative wait)."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if not predicate():
            return False
        time.sleep(0.1)
    return True


def _finish(tracked_ids: list[int], server: subprocess.Popen[str] | None) -> None:
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


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher("discord.desktop")]})
def test_not003_discord_keeps_expired_popup_count_until_read(
    krema: Krema, apps: TestWindows, tmp_path: Path
) -> None:
    """Discord: a timed-out popup stays counted while the tray shows unread, then clears when read."""
    _install_app(krema, "discord.desktop", "Discord", "discord")
    krema.start()
    apps.open("Discord", app_id="discord")
    krema.wait_for_item("Discord")
    description = _dock_description(krema, "Discord")
    token = uuid.uuid4().hex[:10]
    tracked_ids: list[int] = []
    server: subprocess.Popen[str] | None = None
    try:
        server = _start_server(tmp_path, chat_sni=("discord_status_icon_1", "Discord"))
        wait_until(
            lambda: "Unread messages" not in description() and "notifications" not in description(),
            message=lambda: f"Discord dock item to start at rest (got {description()!r})",
        )

        _chat_sni("Discord", badge="235,55,55@top-right")
        wait_until(
            lambda: "Unread messages" in description(),
            message=lambda: f"Discord tray unread badge to show as unread (got {description()!r})",
        )

        toast_id = _notify(token, "discord-dm", desktop_entry="discord")
        tracked_ids.append(toast_id)
        wait_until(
            lambda: "1 notifications" in description(),
            message=lambda: f"Discord DM popup to add a count (got {description()!r})",
        )

        _expire(toast_id)
        assert _holds(lambda: "1 notifications" in description()), (
            f"Discord count must survive the popup timing out while unread (got {description()!r})"
        )

        _chat_sni("Discord")
        wait_until(
            lambda: "notifications" not in description() and "Unread messages" not in description(),
            message=lambda: f"Discord badge to clear once read in the app (got {description()!r})",
        )
    finally:
        _finish(tracked_ids, server)


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher("webcord.desktop")]})
def test_not004_webcord_tray_tooltip_mentions_without_window(
    krema: Krema, apps: TestWindows, tmp_path: Path
) -> None:
    """WebCord closed to the tray: the SNI tooltip mention count drives the pinned launcher badge."""
    _install_app(krema, "webcord.desktop", "WebCord", "webcord")
    krema.start()
    krema.wait_for_item("WebCord")
    description = _dock_description(krema, "WebCord")
    server: subprocess.Popen[str] | None = None
    try:
        server = _start_server(tmp_path, chat_sni=("WebCord_status_icon_1", "WebCord"))
        _chat_sni("WebCord - 4 mentions")
        wait_until(
            lambda: "4 notifications" in description(),
            message=lambda: f"WebCord tooltip mention count to reach the launcher (got {description()!r})",
        )

        _chat_sni("WebCord")
        wait_until(
            lambda: "notifications" not in description() and "Mention notification" not in description(),
            message=lambda: f"WebCord launcher badge to clear when the tooltip is back at rest (got {description()!r})",
        )
    finally:
        _finish([], server)


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher("mattermost.desktop")]})
def test_not005_mattermost_mention_count_clears_when_mentions_are_read(
    krema: Krema, apps: TestWindows, tmp_path: Path
) -> None:
    """Mattermost: a mention popup stays counted after timing out and clears when only unread remains."""
    _install_app(krema, "mattermost.desktop", "Mattermost", "mattermost")
    krema.start()
    apps.open("Mattermost", app_id="mattermost")
    krema.wait_for_item("Mattermost")
    description = _dock_description(krema, "Mattermost")
    token = uuid.uuid4().hex[:10]
    tracked_ids: list[int] = []
    server: subprocess.Popen[str] | None = None
    try:
        server = _start_server(tmp_path, chat_sni=("mattermost", "Mattermost"))
        _chat_sni("Mattermost", badge="230,40,40@bottom-right")
        wait_until(
            lambda: "Mention notification" in description(),
            message=lambda: f"Mattermost red tray badge to show a mention (got {description()!r})",
        )

        toast_id = _notify(token, "mattermost-mention", desktop_entry="mattermost")
        tracked_ids.append(toast_id)
        wait_until(
            lambda: "1 notifications" in description(),
            message=lambda: f"Mattermost mention popup to add a count (got {description()!r})",
        )
        _expire(toast_id)
        assert _holds(lambda: "1 notifications" in description()), (
            f"Mattermost mention count must survive the popup timing out (got {description()!r})"
        )

        _chat_sni("Mattermost", badge="40,150,240@bottom-right")
        wait_until(
            lambda: "Unread messages" in description() and "notifications" not in description(),
            message=lambda: f"Mattermost count to clear once mentions are read, keeping unread (got {description()!r})",
        )

        _chat_sni("Mattermost")
        wait_until(
            lambda: "Unread messages" not in description() and "notifications" not in description(),
            message=lambda: f"Mattermost badge to clear at rest (got {description()!r})",
        )
    finally:
        _finish(tracked_ids, server)
