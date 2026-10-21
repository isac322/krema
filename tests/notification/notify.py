#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Send/close notifications, or host a private fd.o notification server."""

from __future__ import annotations

import argparse
import os
from pathlib import Path

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib


NOTIFICATION_SERVICE = "org.freedesktop.Notifications"
NOTIFICATION_PATH = "/org/freedesktop/Notifications"
NOTIFICATION_INTERFACE = "org.freedesktop.Notifications"
WATCHER_INTERFACE = "org.kde.NotificationWatcher"
MANAGER_INTERFACE = "org.kde.NotificationManager"


def notification_interface():
    bus = dbus.SessionBus()
    proxy = bus.get_object(NOTIFICATION_SERVICE, NOTIFICATION_PATH)
    return dbus.Interface(proxy, NOTIFICATION_INTERFACE)


def send(args: argparse.Namespace) -> None:
    hints = {"desktop-entry": dbus.String(args.desktop_entry)}
    notification_id = notification_interface().Notify(
        dbus.String(args.app_name),
        dbus.UInt32(args.replaces_id),
        dbus.String(args.icon),
        dbus.String(args.summary),
        dbus.String(args.body),
        dbus.Array([], signature="s"),
        dbus.Dictionary(hints, signature="sv"),
        dbus.Int32(args.timeout),
    )
    print(int(notification_id))


def close(args: argparse.Namespace) -> None:
    notification_interface().CloseNotification(dbus.UInt32(args.notification_id))


class NotificationServer(dbus.service.Object):
    """Minimal fd.o server plus KDE watcher registration for a private session."""

    def __init__(
        self,
        bus: dbus.Bus,
        ready_file: Path,
        watcher_ready_file: Path,
        *,
        allow_replacement: bool = False,
        replace_existing: bool = False,
    ) -> None:
        self._bus = bus
        self._next_id = 1
        self._watchers: set[str] = set()
        self._watcher_ready_file = watcher_ready_file
        self._notifications: set[int] = set()
        self._loop = GLib.MainLoop()
        name_options = {
            "bus": bus,
            "allow_replacement": allow_replacement,
            "replace_existing": replace_existing,
            "do_not_queue": True,
        }
        self._bus_name = dbus.service.BusName(NOTIFICATION_SERVICE, **name_options)
        self._manager_name = dbus.service.BusName("org.kde.NotificationManager", **name_options)
        super().__init__(self._bus_name, NOTIFICATION_PATH)
        ready_file.parent.mkdir(parents=True, exist_ok=True)
        ready_file.write_text(f"{os.getpid()}\n", encoding="utf-8")

    def run(self) -> None:
        self._loop.run()

    def stop(self) -> None:
        if self._loop.is_running():
            self._loop.quit()

    def _forward(self, method: str, signature: str, *args) -> None:
        for sender in tuple(self._watchers):
            try:
                proxy = self._bus.get_object(sender, "/NotificationWatcher")
                iface = dbus.Interface(proxy, WATCHER_INTERFACE)
                getattr(iface, method)(*args)
            except dbus.DBusException:
                self._watchers.discard(sender)

    @dbus.service.method(
        NOTIFICATION_INTERFACE,
        in_signature="susssasa{sv}i",
        out_signature="u",
    )
    def Notify(
        self,
        app_name,
        replaces_id,
        app_icon,
        summary,
        body,
        actions,
        hints,
        expire_timeout,
    ):
        notification_id = self._next_id
        self._next_id += 1
        if int(replaces_id) > 0:
            self._notifications.discard(int(replaces_id))
        self._notifications.add(notification_id)
        self._forward(
            "Notify",
            "ususssasa{sv}i",
            dbus.UInt32(notification_id),
            dbus.String(app_name),
            dbus.UInt32(replaces_id),
            dbus.String(app_icon),
            dbus.String(summary),
            dbus.String(body),
            dbus.Array(actions, signature="s"),
            dbus.Dictionary(hints, signature="sv"),
            dbus.Int32(expire_timeout),
        )
        return dbus.UInt32(notification_id)

    @dbus.service.method(NOTIFICATION_INTERFACE, in_signature="u")
    def CloseNotification(self, notification_id):
        notification_id = int(notification_id)
        if notification_id not in self._notifications:
            return
        self._notifications.discard(notification_id)
        self._forward("CloseNotification", "u", dbus.UInt32(notification_id))
        self.NotificationClosed(dbus.UInt32(notification_id), dbus.UInt32(3))

    @dbus.service.signal(NOTIFICATION_INTERFACE, signature="uu")
    def NotificationClosed(self, notification_id, reason):
        pass

    @dbus.service.method(
        MANAGER_INTERFACE,
        sender_keyword="sender",
    )
    def RegisterWatcher(self, sender=None):
        if sender:
            self._watchers.add(str(sender))
            self._watcher_ready_file.parent.mkdir(parents=True, exist_ok=True)
            with self._watcher_ready_file.open("a", encoding="utf-8") as stream:
                stream.write(f"{sender}\n")

    @dbus.service.method(
        MANAGER_INTERFACE,
        sender_keyword="sender",
    )
    def UnRegisterWatcher(self, sender=None):
        if sender:
            self._watchers.discard(str(sender))


def serve(args: argparse.Namespace) -> None:
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    server = NotificationServer(
        bus,
        Path(args.ready_file),
        Path(args.watcher_ready_file),
        allow_replacement=args.allow_replacement,
        replace_existing=args.replace_existing,
    )
    try:
        server.run()
    finally:
        server.stop()


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    commands = root.add_subparsers(dest="command", required=True)

    notify = commands.add_parser("notify", help="send a persistent notification")
    notify.add_argument("--desktop-entry", required=True)
    notify.add_argument("--summary", required=True)
    notify.add_argument("--body", required=True)
    notify.add_argument("--app-name", default="Krema E2E external stimulus")
    notify.add_argument("--icon", default="dialog-information")
    notify.add_argument("--replaces-id", type=int, default=0)
    notify.add_argument("--timeout", type=int, default=-1)
    notify.set_defaults(handler=send)

    dismiss = commands.add_parser("close", help="close a notification by server ID")
    dismiss.add_argument("notification_id", type=int)
    dismiss.set_defaults(handler=close)

    server = commands.add_parser("serve", help="own fd.o notifications for a private E2E session")
    server.add_argument("--ready-file", required=True)
    server.add_argument("--watcher-ready-file", required=True)
    server.add_argument("--allow-replacement", action="store_true")
    server.add_argument("--replace-existing", action="store_true")
    server.set_defaults(handler=serve)
    return root


def main() -> None:
    args = parser().parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()
