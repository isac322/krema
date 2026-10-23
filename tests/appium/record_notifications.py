# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Drive the real notification lifecycle while an external recorder runs.

This file is deliberately outside ``test_*.py``.  It reuses the notification
stimulus/server and inbox helpers from ``test_12_notifications`` but keeps the
visual-evidence flow separate from the permanent functional test.

The evidence runner is intentionally restricted to the existing X11
``ffmpeg -f x11grab -draw_mouse 1`` route: ScreenShot2 does not include the
hardware cursor, while this clip requires a visible cursor.
"""

from __future__ import annotations

import dbus
import importlib.util
import json
import os
import sys
import subprocess
import time
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

import pytest

from krema_e2e import env, input as inp, kwin
from krema_e2e.krema import Krema, context_menu_entries, has_state
from krema_e2e.waits import wait_until
from krema_e2e.windows import TestWindows
from test_06_settings import SIDEBAR
from test_12_notifications import (
    STIMULUS,
    _card_xpath,
    _close,
    _notify,
    _stop_server,
    _visible_state,
)

CONFIG_WRITER = Path("/build/bin/krema_notification_config_writer")

def _fresh_server(tmp_path: Path, generation: str, *, replace_existing: bool = False):
    directory = tmp_path / generation
    directory.mkdir()
    ready_file = directory / "notification-server.ready"
    watcher_ready_file = directory / "notification-watcher.ready"
    command = [
        sys.executable,
        str(STIMULUS),
        "serve",
        "--ready-file",
        str(ready_file),
        "--watcher-ready-file",
        str(watcher_ready_file),
        "--allow-replacement",
    ]
    if replace_existing:
        command.append("--replace-existing")
    server = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True)
    try:
        wait_until(
            lambda: server.poll() is None and ready_file.exists() and watcher_ready_file.exists(),
            message="Krema to register with the new notification server",
        )
    except BaseException:
        _stop_server(server)
        raise
    return server


def _ease_path(start: tuple[int, int], end: tuple[int, int], steps: int = 12) -> list[tuple[int, int]]:
    """Return a slow-in/slow-out pointer path for a visible real input."""
    points = [start]
    for index in range(1, steps + 1):
        t = index / steps
        eased = t * t * (3.0 - 2.0 * t)
        points.append((round(start[0] + (end[0] - start[0]) * eased), round(start[1] + (end[1] - start[1]) * eased)))
    return points


def _glide_to(point: tuple[int, int]) -> None:
    """Move the real X cursor and Wayland fake input along an eased path."""
    current = kwin.cursor_pos()
    path = _ease_path(current, point)
    scale = int(os.environ.get("KREMA_RECORD_X11_SCALE", "1"))
    for x, y in path:
        subprocess.run(["xdotool", "mousemove", str(x * scale), str(y * scale)], check=True)
        time.sleep(0.045)
    inp.move_path(path, step_ms=45)


def _click(krema: Krema, point: tuple[int, int], button: str = "left") -> None:
    _glide_to(point)
    inp.click(button=button)


def _description(krema: Krema) -> str:
    item = krema.item_accessible("Alpha")
    if item is None:
        raise RuntimeError("Alpha dock item is unavailable")
    item.clear_cache()
    return str(item.description or "")


def _start_sni(krema: Krema):
    """Start the installed watcher module and reuse the debugger SNI peer."""
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    kded = None
    sni = None
    try:
        if not bus.name_has_owner("org.kde.StatusNotifierWatcher"):
            kded = subprocess.Popen(
                ["kded6"],
                env=krema.environment(),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            wait_until(
                lambda: bus.name_has_owner("org.kde.kded6"),
                message="installed kded6 service",
            )
            kded_iface = dbus.Interface(bus.get_object("org.kde.kded6", "/kded"), "org.kde.kded6")
            if not kded_iface.loadModule("statusnotifierwatcher"):
                raise RuntimeError("kded6 did not load the installed statusnotifierwatcher module")
            wait_until(
                lambda: bus.name_has_owner("org.kde.StatusNotifierWatcher"),
                timeout=10,
                message="StatusNotifierWatcher service",
            )
        source = Path(__file__).resolve().parents[2] / "tools" / "notification-debugger.py"
        spec = importlib.util.spec_from_file_location("krema_notification_debugger", source)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"cannot load SNI peer source {source}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        sni = module.StatusNotifierItemService(bus)
        sni.set_property("Id", env.TEST_APP_ID)
        sni.register_with_watcher()
        return sni, kded
    except BaseException:
        if sni is not None:
            try:
                if not sni.unregister_with_watcher():
                    print("SNI setup cleanup failed to release the owned service name", file=sys.stderr)
            except Exception as error:
                print(f"SNI setup name release failed: {type(error).__name__}: {error}", file=sys.stderr)
            try:
                sni.remove_from_connection()
            except Exception as error:
                print(f"SNI setup object removal failed: {type(error).__name__}: {error}", file=sys.stderr)
        if kded is not None:
            try:
                _stop_server(kded)
            except Exception as error:
                print(f"SNI setup cleanup failed: {type(error).__name__}: {error}", file=sys.stderr)
        raise


def _pump_dbus() -> None:
    context = GLib.MainContext.default()
    while context.iteration(False):
        pass


def _set_dnd_until(krema: Krema, until: datetime | None) -> None:
    args = [str(CONFIG_WRITER)]
    if until is None:
        args.append("--clear")
    else:
        args.extend(["--until", until.astimezone(timezone.utc).isoformat(timespec="milliseconds")])
    environment = krema.environment()
    config_path = Path(environment["XDG_CONFIG_HOME"]) / "plasmanotifyrc"
    trace = env.artifact_path("visual-evidence/notifications/dnd-config.jsonl")
    bus = dbus.SessionBus()

    def signal_received(*values, **details):
        with trace.open("a", encoding="utf-8") as stream:
            stream.write(
                json.dumps(
                    {
                        "event": "dbus-signal",
                        "timestamp_ns": time.monotonic_ns(),
                        **details,
                        "values": repr(values),
                    },
                    sort_keys=True,
                )
                + "\n"
            )

    before = {
        "event": "config-write",
        "timestamp_ns": time.monotonic_ns(),
        "utc_before": datetime.now(timezone.utc).isoformat(),
        "args": args,
        "xdg_config_home": environment["XDG_CONFIG_HOME"],
        "config_path": str(config_path),
        "before": config_path.read_text() if config_path.exists() else None,
    }
    receiver = bus.add_signal_receiver(
        signal_received,
        path_keyword="path",
        interface_keyword="interface",
        member_keyword="member",
        sender_keyword="sender",
    )
    try:
        try:
            result = subprocess.run(args, env=environment, check=True, capture_output=True, text=True)
        except FileNotFoundError as error:
            raise FileNotFoundError(
                f"Notification QA config writer is missing: {CONFIG_WRITER}; "
                "build the krema_notification_config_writer target"
            ) from error
        _pump_dbus()
        before.update(
            {
                "utc_after": datetime.now(timezone.utc).isoformat(),
                "after": config_path.read_text() if config_path.exists() else None,
                "stdout": result.stdout,
                "stderr": result.stderr,
            }
        )
        with trace.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(before, sort_keys=True) + "\n")
    finally:
        receiver.remove()


def _open_settings(krema: Krema) -> None:
    entries = context_menu_entries(pinned=False, is_window=True, has_notifications=True)
    before_windows = {window.internal_id for window in krema.windows()}
    _click(krema, krema.settled_item_center("Alpha"), button="right")
    menu = wait_until(
        lambda: next(
            (
                window
                for window in krema.windows()
                if window.internal_id not in before_windows and not window.normal_window
            ),
            None,
        ),
        message="native dock context menu mapped",
    )
    location = subprocess.check_output(["xdotool", "getmouselocation", "--shell"], text=True)
    coordinates = dict(line.split("=", 1) for line in location.splitlines())
    host_window = coordinates["WINDOW"]
    subprocess.run(["xdotool", "windowfocus", "--sync", host_window], check=True)
    focus = subprocess.check_output(["xdotool", "getwindowfocus"], text=True).strip()
    title_before = subprocess.check_output(["xdotool", "getwindowname", host_window], text=True).strip()
    if "to grab input" in title_before.lower():
        subprocess.run(["xdotool", "key", "Control_R"], check=True)
    title_after = wait_until(
        lambda: (
            title
            if "ungrab" in (title := subprocess.check_output(["xdotool", "getwindowname", host_window], text=True).strip()).lower()
            else None
        ),
        message="nested KWin window to report grabbed input",
    )
    env.artifact_path("visual-evidence/notifications/x11-focus.json").write_text(
        json.dumps(
            {
                "pointer_window": host_window,
                "focused_window": focus,
                "title_before_grab": title_before,
                "title_after_grab": title_after,
                "menu_window": menu.internal_id,
                "menu_geometry": menu.geometry,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    for _ in range(entries.index("Settings...") + 1):
        subprocess.run(["xdotool", "key", "--clearmodifiers", "Down"], check=True)
        time.sleep(0.08)
    subprocess.run(["xdotool", "key", "--clearmodifiers", "Return"], check=True)
    wait_until(
        lambda: next(
            (
                window
                for window in krema.windows()
                if window.normal_window and not window.skip_taskbar and window.title.startswith("Settings")
            ),
            None,
        ),
        message="Settings window",
    )


def _open_notifications_page(krema: Krema) -> None:
    page = krema.wait_for(f"{SIDEBAR}[@name='Notifications']")
    if not has_state(page, "checked"):
        _click(krema, krema.screen_rect(page, "settings").center)
    wait_until(
        lambda: any(has_state(item, "checked") for item in krema.find_all(f"{SIDEBAR}[@name='Notifications']")),
        message="Notifications page selected",
    )


@pytest.mark.kremarc({"PinnedLaunchers": [], "AttentionAnimation": 1, "AttentionAnimationDuration": 10})
def test_record_notifications(krema: Krema, apps: TestWindows, tmp_path: Path) -> None:
    """Record badge 2→1→0, reconnect, and local inbox read/dismiss states."""
    if os.environ.get("KREMA_RECORD_EXTERNAL") != "1":
        pytest.fail("This recorder requires external X11 capture with -draw_mouse 1")
    evidence = env.artifact_path("visual-evidence/notifications")
    evidence.mkdir(parents=True, exist_ok=True)
    events: list[dict[str, Any]] = []
    server = None
    servers = []
    token = uuid.uuid4().hex[:10]
    tracked: list[int] = []
    scenario_completed = False
    sni = None
    kded = None

    def mark(stage: str, **details: Any) -> None:
        events.append({"stage": stage, "timestamp_ns": time.monotonic_ns(), **details})

    try:
        apps.open("Alpha")
        krema.wait_for_item("Alpha")
        server = _fresh_server(tmp_path, "server-1")
        servers.append(server)
        mark("ready", capture="x11grab")

        first = _notify(token, "first")
        second = _notify(token, "second")
        tracked.extend((first, second))
        wait_until(lambda: "2 notifications" in _description(krema), message="badge count 2")
        mark("badge-2")
        time.sleep(2.0)

        _close(first)
        wait_until(lambda: "1 notifications" in _description(krema), message="badge count 1")
        mark("badge-1")
        time.sleep(2.0)

        _close(second)
        wait_until(lambda: "notifications" not in _description(krema), message="badge count 0")
        mark("badge-0")
        time.sleep(2.0)

        _stop_server(server)
        server = _fresh_server(tmp_path, "server-2")
        servers.append(server)
        mark("notification-server-reconnected")
        reconnect_id = _notify(token, "reconnected")
        tracked.append(reconnect_id)
        wait_until(lambda: "1 notifications" in _description(krema), message="badge after server reconnect")
        mark("reconnected-badge-1")
        time.sleep(2.0)
        _close(reconnect_id)
        wait_until(lambda: "notifications" not in _description(krema), message="reconnected badge closed")
        mark("reconnected-badge-0")

        DBusGMainLoop(set_as_default=True)
        bus = dbus.SessionBus()
        owner_before = str(bus.get_name_owner("org.freedesktop.Notifications"))
        owner_changes: list[tuple[str, str]] = []

        def owner_changed(name, old_owner, new_owner):
            if str(name) == "org.freedesktop.Notifications":
                owner_changes.append((str(old_owner), str(new_owner)))

        owner_receiver = bus.add_signal_receiver(
            owner_changed,
            signal_name="NameOwnerChanged",
            dbus_interface="org.freedesktop.DBus",
        )
        try:
            server = _fresh_server(tmp_path, "server-3", replace_existing=True)
            servers.append(server)
            owner_after = str(bus.get_name_owner("org.freedesktop.Notifications"))
            wait_until(
                lambda: (_pump_dbus(), (owner_before, owner_after) in owner_changes)[1],
                message="atomic notification owner handover with both owners nonempty",
            )
            assert owner_before and owner_after and owner_before != owner_after
            registrations = (tmp_path / "server-3" / "notification-watcher.ready").read_text().splitlines()
            assert len(registrations) == 1, registrations
            mark("notification-server-owner-handover", old_owner=owner_before, new_owner=owner_after)
        finally:
            owner_receiver.remove()
        handover_id = _notify(token, "handover")
        tracked.append(handover_id)
        wait_until(lambda: "1 notifications" in _description(krema), message="badge after atomic owner handover")
        mark("handover-badge-1")
        time.sleep(2.0)
        _close(handover_id)
        wait_until(lambda: "notifications" not in _description(krema), message="handover badge closed")
        registrations = (tmp_path / "server-3" / "notification-watcher.ready").read_text().splitlines()
        assert len(registrations) == 1, registrations
        mark("handover-badge-0", watcher_registrations=len(registrations))

        sni, kded = _start_sni(krema)
        sni.set_status("NeedsAttention")
        wait_until(
            lambda: (_pump_dbus(), "Attention requested" in _description(krema))[1],
            message="SNI NeedsAttention to reach the dock",
        )
        mark("sni-needs-attention")
        time.sleep(2.0)
        sni.set_status("Active")
        wait_until(
            lambda: (_pump_dbus(), "Attention requested" not in _description(krema))[1],
            message="SNI Active to clear dock attention",
        )
        mark("sni-active")
        time.sleep(2.0)

        sni.set_status("NeedsAttention")
        wait_until(
            lambda: (_pump_dbus(), "Attention requested" in _description(krema))[1],
            message="SNI attention before timed DND",
        )
        mark("dnd-pre-attention")
        time.sleep(1.0)
        log_offset = krema.log_path.stat().st_size
        _set_dnd_until(krema, datetime.now(timezone.utc) + timedelta(days=30))
        mark("dnd-long-until-set", limitation="safe timer arming is asserted separately by the Qt test")
        wait_until(
            lambda: "DND state changed: true" in krema.log_path.read_text()[log_offset:],
            timeout=2.0,
            message="tracker to observe a 30-day DND deadline",
        )
        mark("dnd-active-observed")
        time.sleep(2.0)
        assert "DND state changed: false" not in krema.log_path.read_text()[log_offset:]
        mark("dnd-long-active-observed")
        _set_dnd_until(krema, datetime.now(timezone.utc) + timedelta(seconds=3))
        mark("dnd-short-until-rewritten", clock="unchanged real wall clock")
        wait_until(
            lambda: "DND state changed: false" in krema.log_path.read_text()[log_offset:],
            timeout=5.0,
            message="tracker to observe DND deadline expiry",
        )
        mark("dnd-deadline-elapsed", limitation="verify resumed animation in real video")
        time.sleep(2.0)
        sni.set_status("Active")
        _set_dnd_until(krema, None)

        read_id = _notify(token, "read")
        dismiss_id = _notify(token, "dismiss")
        tracked.extend((read_id, dismiss_id))
        read_summary = f"{token}-read"
        dismiss_summary = f"{token}-dismiss"
        _open_settings(krema)
        _open_notifications_page(krema)
        read_card = _card_xpath(read_summary)
        dismiss_card = _card_xpath(dismiss_summary)
        wait_until(
            lambda: _visible_state(krema, read_card, "Unread") and _visible_state(krema, dismiss_card, "Unread"),
            message="inbox cards unread",
        )
        mark("inbox-unread")
        time.sleep(2.0)

        read_button = krema.wait_for(f"{read_card}//button[@name='Mark read']")
        _click(krema, krema.screen_rect(read_button, "settings").center)
        wait_until(lambda: _visible_state(krema, read_card, "Read"), message="inbox card read")
        mark("inbox-read")
        time.sleep(2.0)

        dismiss_button = krema.wait_for(f"{dismiss_card}//button[@name='Dismiss']")
        _click(krema, krema.screen_rect(dismiss_button, "settings").center)
        wait_until(lambda: _visible_state(krema, dismiss_card, "Dismissed"), message="inbox card dismissed")
        mark("inbox-dismissed")
        time.sleep(2.0)
        scenario_completed = True
    finally:
        scenario_error = sys.exc_info()[1]
        cleanup_errors: list[dict[str, str]] = []

        def cleanup_error(stage: str, error: Exception) -> None:
            details = {"stage": stage, "error": f"{type(error).__name__}: {error}"}
            cleanup_errors.append(details)
            mark("cleanup-error", step=stage, error=details["error"])
            print(f"Notification recording cleanup failed ({stage}): {details['error']}", file=sys.stderr)

        try:
            _set_dnd_until(krema, None)
        except Exception as error:
            cleanup_error("dnd-reset", error)
        if sni is not None:
            try:
                sni.unregister_with_watcher()
            except Exception as error:
                cleanup_error("sni-release", error)
        if kded is not None:
            try:
                kded.terminate()
            except Exception as error:
                cleanup_error("kded-terminate", error)
            try:
                kded.wait(timeout=5)
            except Exception as error:
                cleanup_error("kded-wait", error)
                try:
                    kded.kill()
                except Exception as kill_error:
                    cleanup_error("kded-kill", kill_error)
                try:
                    kded.wait(timeout=5)
                except Exception as reap_error:
                    cleanup_error("kded-reap", reap_error)
        for notification_id in tracked:
            try:
                _close(notification_id)
            except Exception as error:
                cleanup_error(f"close-notification-{notification_id}", error)
        for owned_server in servers:
            try:
                _stop_server(owned_server)
            except Exception as error:
                cleanup_error("notification-server-stop", error)
        try:
            krema.move_away()
        except Exception as error:
            cleanup_error("move-away", error)
        try:
            evidence.joinpath("events.jsonl").write_text(
                "".join(json.dumps(event, sort_keys=True) + "\n" for event in events), encoding="utf-8"
            )
        except Exception as error:
            cleanup_error("events-write", error)
        try:
            evidence.joinpath("metadata.json").write_text(
                json.dumps(
                    {
                        "scenario": "record_notifications",
                        "outcome": "passed" if scenario_completed and not cleanup_errors else "failed",
                        "scenario_outcome": "passed" if scenario_completed else "failed",
                        "scenario_error": None if scenario_error is None else f"{type(scenario_error).__name__}: {scenario_error}",
                        "cleanup_outcome": "failed" if cleanup_errors else "passed",
                        "cleanup_errors": cleanup_errors,
                        "capture": "x11grab",
                        "pointer": "continuous eased fake-input path; x11grab uses -draw_mouse 1",
                        "git_revision": os.environ.get("GITHUB_SHA"),
                        "unverified": ["DND suppression and timed expiry require visual review of the captured animation"],
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
        except Exception as error:
            cleanup_error("metadata-write", error)
        if cleanup_errors and scenario_error is None:
            raise RuntimeError(f"Notification recording cleanup failed: {cleanup_errors}")
