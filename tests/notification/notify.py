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
SNI_WATCHER_SERVICE = "org.kde.StatusNotifierWatcher"
SNI_WATCHER_PATH = "/StatusNotifierWatcher"
SNI_WATCHER_INTERFACE = "org.kde.StatusNotifierWatcher"
SNI_ITEM_INTERFACE = "org.kde.StatusNotifierItem"
DBUS_PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties"
SLACK_SNI_SERVICE = "org.krema.e2e.SlackSni"
SLACK_SNI_PATH = "/StatusNotifierItem"
SLACK_SNI_CONTROL_INTERFACE = "org.krema.e2e.SlackSni"

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



def _slack_icon_pixmap(state: str) -> dbus.Array:
    if state == "highlight":
        pixel = bytes((255, 224, 30, 90))
    elif state == "unread":
        pixel = bytes((255, 54, 197, 240))
    else:
        pixel = bytes((255, 155, 156, 158))
    data = dbus.ByteArray(pixel * (22 * 22))
    return dbus.Array(
        [dbus.Struct((dbus.Int32(22), dbus.Int32(22), data), signature=None)],
        signature="(iiay)",
    )


def _slack_tooltip(title: str, body: str = "") -> dbus.Struct:
    return dbus.Struct(
        (
            dbus.String(""),
            dbus.Array([], signature="(iiay)"),
            dbus.String(title),
            dbus.String(body),
        ),
        signature=None,
    )


def slack_sni(args: argparse.Namespace) -> None:
    bus = dbus.SessionBus()
    proxy = bus.get_object(SLACK_SNI_SERVICE, SLACK_SNI_PATH)
    iface = dbus.Interface(proxy, SLACK_SNI_CONTROL_INTERFACE)
    iface.SetSlackState(
        dbus.String(args.state),
        dbus.String(args.tooltip_title),
        dbus.String(args.tooltip_body),
    )


class StatusNotifierWatcherServer(dbus.service.Object):
    """Minimal StatusNotifierWatcher for private E2E sessions."""

    def __init__(self, bus: dbus.Bus, initial_items: list[str]) -> None:
        self._items: list[str] = list(initial_items)
        self._bus_name = dbus.service.BusName(
            SNI_WATCHER_SERVICE,
            bus=bus,
            allow_replacement=True,
            replace_existing=True,
            do_not_queue=True,
        )
        super().__init__(self._bus_name, SNI_WATCHER_PATH)

    @dbus.service.method(
        SNI_WATCHER_INTERFACE,
        in_signature="s",
        sender_keyword="sender",
    )
    def RegisterStatusNotifierItem(self, service: str, sender: str | None = None) -> None:
        item = f"{sender}{service}" if service.startswith("/") and sender else (
            service if "/" in service else f"{service}/StatusNotifierItem"
        )
        if item not in self._items:
            self._items.append(item)
        self.StatusNotifierItemRegistered(dbus.String(item))

    @dbus.service.signal(SNI_WATCHER_INTERFACE, signature="s")
    def StatusNotifierItemRegistered(self, item: str) -> None:
        pass

    @dbus.service.signal(SNI_WATCHER_INTERFACE, signature="s")
    def StatusNotifierItemUnregistered(self, item: str) -> None:
        pass

    @dbus.service.method(
        DBUS_PROPERTIES_INTERFACE,
        in_signature="ss",
        out_signature="v",
    )
    def Get(self, interface_name: str, property_name: str):
        return self.GetAll(interface_name)[property_name]

    @dbus.service.method(
        DBUS_PROPERTIES_INTERFACE,
        in_signature="s",
        out_signature="a{sv}",
    )
    def GetAll(self, _interface_name: str):
        return dbus.Dictionary(
            {
                "RegisteredStatusNotifierItems": dbus.Array(self._items, signature="s"),
                "IsStatusNotifierHostRegistered": dbus.Boolean(True),
                "ProtocolVersion": dbus.Int32(0),
            },
            signature="sv",
        )


class SlackSniServer(dbus.service.Object):
    """Test-owned Slack StatusNotifierItem exposing real 22x22 IconPixmap and ToolTip."""

    def __init__(self, bus: dbus.Bus) -> None:
        self._bus_name = dbus.service.BusName(
            SLACK_SNI_SERVICE,
            bus=bus,
            allow_replacement=True,
            replace_existing=True,
            do_not_queue=True,
        )
        super().__init__(self._bus_name, SLACK_SNI_PATH)
        self._props: dict[str, object] = {
            "Category": dbus.String("ApplicationStatus"),
            "Id": dbus.String("Slack_status_icon_1"),
            "Title": dbus.String("Slack"),
            "Status": dbus.String("Active"),
            "IconName": dbus.String(""),
            "OverlayIconName": dbus.String(""),
            "AttentionIconName": dbus.String(""),
            "IconThemePath": dbus.String(""),
            "IconPixmap": _slack_icon_pixmap("rest"),
            "OverlayIconPixmap": dbus.Array([], signature="(iiay)"),
            "AttentionIconPixmap": dbus.Array([], signature="(iiay)"),
            "ToolTip": _slack_tooltip("No unread messages", ""),
        }

    @dbus.service.method(
        SLACK_SNI_CONTROL_INTERFACE,
        in_signature="sss",
    )
    def SetSlackState(self, state: str, tooltip_title: str, tooltip_body: str) -> None:
        self._props["IconPixmap"] = _slack_icon_pixmap(str(state))
        self._props["ToolTip"] = _slack_tooltip(str(tooltip_title), str(tooltip_body))
        self.PropertiesChanged(
            dbus.String(SNI_ITEM_INTERFACE),
            dbus.Dictionary(
                {
                    "IconPixmap": self._props["IconPixmap"],
                    "ToolTip": self._props["ToolTip"],
                },
                signature="sv",
            ),
            dbus.Array([], signature="s"),
        )
        self.NewIcon()
        self.NewToolTip()

    @dbus.service.signal(SNI_ITEM_INTERFACE)
    def NewIcon(self) -> None:
        pass

    @dbus.service.signal(SNI_ITEM_INTERFACE)
    def NewToolTip(self) -> None:
        pass

    @dbus.service.signal(DBUS_PROPERTIES_INTERFACE, signature="sa{sv}as")
    def PropertiesChanged(self, interface_name, changed_properties, invalidated_properties) -> None:
        pass

    @dbus.service.method(
        DBUS_PROPERTIES_INTERFACE,
        in_signature="ss",
        out_signature="v",
    )
    def Get(self, _interface_name: str, property_name: str):
        return self._props[str(property_name)]

    @dbus.service.method(
        DBUS_PROPERTIES_INTERFACE,
        in_signature="s",
        out_signature="a{sv}",
    )
    def GetAll(self, _interface_name: str):
        return dbus.Dictionary(self._props, signature="sv")

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
    sni_item = None
    sni_watcher = None
    if args.with_slack_sni:
        sni_item = SlackSniServer(bus)
        item_addr = f"{SLACK_SNI_SERVICE}{SLACK_SNI_PATH}"
        if bus.name_has_owner(SNI_WATCHER_SERVICE):
            proxy = bus.get_object(SNI_WATCHER_SERVICE, SNI_WATCHER_PATH)
            dbus.Interface(proxy, SNI_WATCHER_INTERFACE).RegisterStatusNotifierItem(SLACK_SNI_SERVICE)
        else:
            sni_watcher = StatusNotifierWatcherServer(bus, [item_addr])
            sni_watcher.StatusNotifierItemRegistered(dbus.String(item_addr))
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
        del sni_watcher
        del sni_item

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

    sni = commands.add_parser("slack-sni", help="update the test-owned Slack StatusNotifierItem")
    sni.add_argument("--state", choices=("rest", "unread", "highlight"), required=True)
    sni.add_argument("--tooltip-title", required=True)
    sni.add_argument("--tooltip-body", default="")
    sni.set_defaults(handler=slack_sni)

    server = commands.add_parser("serve", help="own fd.o notifications for a private E2E session")
    server.add_argument("--ready-file", required=True)
    server.add_argument("--watcher-ready-file", required=True)
    server.add_argument("--allow-replacement", action="store_true")
    server.add_argument("--replace-existing", action="store_true")
    server.add_argument("--with-slack-sni", action="store_true")
    server.set_defaults(handler=serve)
    return root


def main() -> None:
    args = parser().parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()
