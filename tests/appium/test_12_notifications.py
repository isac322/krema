# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Real-session notification watcher and Settings inbox coverage."""

from __future__ import annotations

import subprocess
import sys
import uuid
from pathlib import Path

import pytest

from krema_e2e import env
from krema_e2e import input as inp
from krema_e2e.krema import Krema, context_menu_entries, has_state
from krema_e2e.waits import wait_until
from krema_e2e.windows import TestWindows
from test_06_settings import (
    SETTINGS,
    close_settings,
    hover_ready,
    open_page,
    scroll_into_view,
)


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


def _open_notification_settings(krema: Krema) -> None:
    hover_ready(krema, "Alpha")
    entries = context_menu_entries(pinned=False, is_window=True, has_notifications=True)
    krema.open_settings("Alpha", entries)
    krema.move_away()


def _card_xpath(summary: str) -> str:
    return f"{SETTINGS}//list_item[@name='{summary}']"
def _preview_card_xpath(summary: str) -> str:
    return f"//popup_menu//list_item[@name='{summary}']"




def _visible_state(krema: Krema, card_xpath: str, state: str) -> bool:
    return any(
        has_state(element, "visible") or has_state(element, "showing")
        for element in krema.find_all(f"{card_xpath}//*[contains(@name, '{state}')]")
    )


def _button_visible(krema: Krema, xpath: str) -> bool:
    return any(
        has_state(element, "visible") or has_state(element, "showing")
        for element in krema.find_all(xpath)
    )



@pytest.mark.kremarc({"PinnedLaunchers": []})
def test_not001_external_watcher_badge_close_and_local_inbox_lifecycle(
    krema: Krema, apps: TestWindows, tmp_path: Path
) -> None:
    """A real external Notify reaches the dock and the Settings inbox."""
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
        hover_ready(krema, "Alpha")
        preview_card = _preview_card_xpath(f"{token}-first")
        wait_until(
            lambda: krema.preview_visible() and krema.find(preview_card) is not None,
            message="hover popup to expose the app-scoped unread card",
        )
        card = krema.find(preview_card)
        assert card is not None
        assert has_state(card, "showing") or has_state(card, "visible")
        assert krema.find(
            f"{preview_card}//*[contains(@name, 'External notification {token}-first')]"
        ) is not None
        assert krema.find(f"{preview_card}//button[@name='Mark read']") is not None
        assert krema.find(f"{preview_card}//button[@name='Dismiss']") is not None
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

        read_id = _notify(token, "read")
        tracked_ids.append(read_id)
        dismiss_id = _notify(token, "dismiss")
        tracked_ids.append(dismiss_id)
        read_summary = f"{token}-read"
        dismiss_summary = f"{token}-dismiss"
        _open_notification_settings(krema)
        open_page(krema, "Notifications")

        sync_notice = krema.wait_for(
            f"{SETTINGS}//*[contains(@name, 'Read and dismissed state is local to Krema')]"
        )
        assert has_state(sync_notice, "visible")

        read_card = _card_xpath(read_summary)
        dismiss_card = _card_xpath(dismiss_summary)
        wait_until(
            lambda: _visible_state(krema, read_card, "Unread")
            and _visible_state(krema, dismiss_card, "Unread"),
            message="new external notifications to appear as unread inbox cards",
        )

        inp.click(
            *krema.screen_rect(
                scroll_into_view(krema, f"{read_card}//button[@name='Mark read']"),
                "settings",
            ).center
        )
        wait_until(
            lambda: _visible_state(krema, read_card, "Read")
            and not _button_visible(krema, f"{read_card}//button[@name='Mark read']"),
            message="Settings inbox to mark one notification read locally",
        )
        assert _button_visible(krema, f"{read_card}//button[@name='Dismiss']"), (
            "a locally read notification remains active; no server read sync is implied"
        )
        wait_until(
            lambda: "1 notifications" in description(),
            message="dock unread count to exclude the locally read notification",
        )

        dismiss_button = scroll_into_view(krema, f"{dismiss_card}//button[@name='Dismiss']")
        inp.click(*krema.screen_rect(dismiss_button, "settings").center)
        wait_until(
            lambda: _visible_state(krema, dismiss_card, "Dismissed")
            and "notifications" not in description(),
            message="Settings inbox to dismiss the remaining notification locally",
        )
        assert _button_visible(krema, f"{dismiss_card}//button[@name='Dismiss']"), (
            "a dismissed notification remains active until the server closes it"
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
        try:
            if krema.settings() is not None:
                close_settings(krema)
        except Exception as error:
            cleanup_failed("settings-close", error)
        if server is not None:
            try:
                _stop_server(server)
            except Exception as error:
                cleanup_failed("notification-server-stop", error)
        if cleanup_errors and scenario_error is None:
            raise cleanup_errors[0]
