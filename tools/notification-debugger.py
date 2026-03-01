#!/usr/bin/env python3
"""
Notification Debugger GUI for Krema's NotificationTracker.

Sends and receives notifications (RegisterWatcher) and SNI NeedsAttention
events in a single PyQt6 GUI app. Allows targeting specific apps in the dock
to debug notification → attention animation chain.

Dependencies:
    pip: PyQt6, dbus-python, PyGObject
    system: libnotify (notify-send), gdbus

Usage:
    # Terminal 1: Run krema with notification-only logging
    QT_LOGGING_RULES="*=false;krema.notifications=true;qml=true" krema

    # Terminal 2: Run the debugger
    python3 tools/notification-debugger.py
"""

from __future__ import annotations

import subprocess
import sys
from collections import defaultdict
from datetime import datetime

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop

import gi
gi.require_version("Notify", "0.7")
from gi.repository import GLib, Notify as LibNotify
from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

# ---------------------------------------------------------------------------
# Target app presets (desktop-entry → display name)
# ---------------------------------------------------------------------------

TARGET_APPS = [
    ("org.kde.dolphin", "Dolphin"),
    ("org.kde.konsole", "Konsole"),
    ("org.kde.kate", "Kate"),
    ("systemsettings", "System Settings"),
    ("org.kde.discover", "Discover"),
    ("firefox", "Firefox"),
    ("thunderbird", "Thunderbird"),
    ("org.telegram.desktop", "Telegram"),
    ("discord", "Discord"),
    ("slack", "Slack"),
    ("org.kde.okular", "Okular"),
    ("code", "VS Code"),
]

# ---------------------------------------------------------------------------
# D-Bus watcher (RegisterWatcher protocol)
# ---------------------------------------------------------------------------

# notification_id -> desktop_entry
_notif_id_to_entry: dict[int, str] = {}
# desktop_entry -> set of active notification IDs
_unread: dict[str, set[int]] = defaultdict(set)

# Callback set by the GUI to append log lines
_log_callback = None
_status_callback = None


def _log(tag: str, msg: str) -> None:
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = f"[{ts}] [{tag}] {msg}"
    if _log_callback:
        _log_callback(line, tag)


def _update_status() -> None:
    if _status_callback:
        _status_callback()


class NotificationWatcher(dbus.service.Object):
    """RegisterWatcher endpoint at /NotificationWatcher."""

    def __init__(self, bus: dbus.Bus) -> None:
        bus_name = dbus.service.BusName(
            "org.krema.NotificationDebugger", bus=bus
        )
        super().__init__(bus_name, "/NotificationWatcher")

    @dbus.service.method(
        dbus_interface="org.kde.NotificationWatcher",
        in_signature="ususssasa{sv}i",
    )
    def Notify(
        self,
        nid: int,
        app_name: str,
        replaces_id: int,
        app_icon: str,
        summary: str,
        body: str,
        actions: list,
        hints: dict,
        expire_timeout: int,
    ) -> None:
        desktop_entry = str(hints.get("desktop-entry", app_name) or app_name).lower()

        if replaces_id > 0 and replaces_id in _notif_id_to_entry:
            old = _notif_id_to_entry.pop(replaces_id)
            _unread[old].discard(replaces_id)
            if not _unread[old]:
                del _unread[old]

        _notif_id_to_entry[nid] = desktop_entry
        _unread[desktop_entry].add(nid)

        count = len(_unread[desktop_entry])
        _log(
            "Notify",
            f"id={nid} app={desktop_entry} summary={summary!r} "
            f"replaces={replaces_id} unread={count}",
        )
        _update_status()

    @dbus.service.method(
        dbus_interface="org.kde.NotificationWatcher",
        in_signature="u",
    )
    def CloseNotification(self, nid: int) -> None:
        if nid not in _notif_id_to_entry:
            return
        entry = _notif_id_to_entry.pop(nid)
        _unread[entry].discard(nid)
        if not _unread[entry]:
            del _unread[entry]
        _log("CloseNotification", f"id={nid} app={entry}")
        _update_status()


def _on_notification_closed(nid: int, reason: int) -> None:
    if nid not in _notif_id_to_entry:
        return
    entry = _notif_id_to_entry.pop(nid)
    _unread[entry].discard(nid)
    if not _unread[entry]:
        del _unread[entry]
    reasons = {1: "expired", 2: "dismissed", 3: "closed-by-call", 4: "undefined"}
    _log(
        "NotificationClosed",
        f"id={nid} app={entry} reason={reasons.get(reason, f'unknown({reason})')}",
    )
    _update_status()


# ---------------------------------------------------------------------------
# SNI (StatusNotifierItem) — self-register for NeedsAttention toggling
# ---------------------------------------------------------------------------


class StatusNotifierItem(dbus.service.Object):
    """Minimal SNI service for NeedsAttention testing."""

    IFACE = "org.kde.StatusNotifierItem"

    def __init__(self, bus: dbus.Bus, sni_id: str = "krema-notification-debugger") -> None:
        self._bus = bus
        self._status = "Active"
        self._sni_id = sni_id
        self._service_name = "org.krema.NotificationDebugger"
        bus_name = dbus.service.BusName(self._service_name, bus=bus)
        super().__init__(bus_name, "/StatusNotifierItem")

    @property
    def sni_id(self) -> str:
        return self._sni_id

    @sni_id.setter
    def sni_id(self, value: str) -> None:
        self._sni_id = value

    def register_with_watcher(self) -> None:
        try:
            proxy = self._bus.get_object(
                "org.kde.StatusNotifierWatcher",
                "/StatusNotifierWatcher",
            )
            iface = dbus.Interface(proxy, "org.kde.StatusNotifierWatcher")
            iface.RegisterStatusNotifierItem(self._service_name)
            _log("SNI", f"RegisterStatusNotifierItem succeeded (Id={self._sni_id})")
        except dbus.DBusException as e:
            _log("SNI-ERROR", f"RegisterStatusNotifierItem failed: {e}")

    def toggle_attention(self) -> bool:
        if self._status == "Active":
            self._status = "NeedsAttention"
        else:
            self._status = "Active"
        self.NewStatus(self._status)
        _log("SNI", f"Toggled status -> {self._status} (Id={self._sni_id})")
        return self._status == "NeedsAttention"

    @property
    def status(self) -> str:
        return self._status

    # -- D-Bus signals --

    @dbus.service.signal(dbus_interface="org.kde.StatusNotifierItem", signature="s")
    def NewStatus(self, status: str) -> None:
        pass

    # -- D-Bus properties via org.freedesktop.DBus.Properties --

    @dbus.service.method(
        dbus_interface="org.freedesktop.DBus.Properties",
        in_signature="ss",
        out_signature="v",
    )
    def Get(self, interface: str, prop: str):
        if interface != self.IFACE:
            raise dbus.DBusException(f"Unknown interface {interface}")
        props = {
            "Id": self._sni_id,
            "Status": self._status,
            "IconName": "dialog-information",
            "AttentionIconName": "dialog-warning",
            "Category": "ApplicationStatus",
            "Title": f"Krema Debugger ({self._sni_id})",
            "ToolTip": dbus.Struct(
                ("", dbus.Array([], signature="(iiay)"), "Krema Debugger", ""),
                signature=None,
            ),
            "ItemIsMenu": dbus.Boolean(False),
            "Menu": dbus.ObjectPath("/NO_DBUSMENU"),
        }
        if prop not in props:
            raise dbus.DBusException(f"Unknown property {prop}")
        return props[prop]

    @dbus.service.method(
        dbus_interface="org.freedesktop.DBus.Properties",
        in_signature="s",
        out_signature="a{sv}",
    )
    def GetAll(self, interface: str):
        if interface != self.IFACE:
            return {}
        return {
            "Id": self._sni_id,
            "Status": self._status,
            "IconName": "dialog-information",
            "AttentionIconName": "dialog-warning",
            "Category": "ApplicationStatus",
            "Title": f"Krema Debugger ({self._sni_id})",
            "ItemIsMenu": dbus.Boolean(False),
            "Menu": dbus.ObjectPath("/NO_DBUSMENU"),
        }


# ---------------------------------------------------------------------------
# SNI Watcher — monitor external SNI events
# ---------------------------------------------------------------------------

_sni_attention_apps: set[str] = set()


def _on_sni_registered(service: str) -> None:
    _log("SNI-Registered", f"service={service}")
    _update_status()


def _on_sni_unregistered(service: str) -> None:
    _sni_attention_apps.discard(service)
    _log("SNI-Unregistered", f"service={service}")
    _update_status()


def _on_sni_new_status(status: str, *, sender: str = None, **kwargs) -> None:
    src = sender or "unknown"
    if status == "NeedsAttention":
        _sni_attention_apps.add(src)
    else:
        _sni_attention_apps.discard(src)
    _log("SNI-NewStatus", f"sender={src} status={status}")
    _update_status()


# ---------------------------------------------------------------------------
# Notification senders (parametrized by desktop_entry)
# ---------------------------------------------------------------------------

_send_counter = 0


def _next_id() -> int:
    global _send_counter
    _send_counter += 1
    return _send_counter


def send_notify_send(desktop_entry: str) -> None:
    n = _next_id()
    try:
        subprocess.Popen(
            [
                "notify-send",
                "-a", desktop_entry,
                "-h", f"string:desktop-entry:{desktop_entry}",
                f"notify-send #{n}",
                f"Via CLI notify-send (target={desktop_entry})",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        _log("SEND", f"notify-send #{n} (target={desktop_entry})")
    except FileNotFoundError:
        _log("SEND-ERROR", "notify-send not found")


def send_gdbus(desktop_entry: str) -> None:
    n = _next_id()
    try:
        subprocess.Popen(
            [
                "gdbus",
                "call",
                "--session",
                "--dest=org.freedesktop.Notifications",
                "--object-path=/org/freedesktop/Notifications",
                "--method=org.freedesktop.Notifications.Notify",
                desktop_entry,
                "0",
                "dialog-information",
                f"gdbus #{n}",
                f"Via gdbus (target={desktop_entry})",
                "[]",
                f"{{'desktop-entry': <'{desktop_entry}'>}}",
                "5000",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        _log("SEND", f"gdbus #{n} (target={desktop_entry})")
    except FileNotFoundError:
        _log("SEND-ERROR", "gdbus not found")


def send_libnotify(desktop_entry: str) -> None:
    n = _next_id()
    try:
        if not LibNotify.is_initted():
            LibNotify.init(desktop_entry)
        notif = LibNotify.Notification.new(
            f"libnotify #{n}",
            f"Via python gi.repository.Notify (target={desktop_entry})"
        )
        notif.set_app_name(desktop_entry)
        # Set desktop-entry hint so notification server forwards it to watchers
        notif.set_hint("desktop-entry", GLib.Variant.new_string(desktop_entry))
        notif.show()
        _log("SEND", f"libnotify #{n} (target={desktop_entry})")
    except Exception as e:
        _log("SEND-ERROR", f"libnotify: {e}")


def send_dbus_python(bus: dbus.Bus, desktop_entry: str) -> None:
    n = _next_id()
    try:
        proxy = bus.get_object(
            "org.freedesktop.Notifications",
            "/org/freedesktop/Notifications",
        )
        iface = dbus.Interface(proxy, "org.freedesktop.Notifications")
        iface.Notify(
            desktop_entry,
            dbus.UInt32(0),
            "dialog-information",
            f"dbus-python #{n}",
            f"Via raw dbus-python Notify call (target={desktop_entry})",
            dbus.Array([], signature="s"),
            dbus.Dictionary({"desktop-entry": desktop_entry}, signature="sv"),
            dbus.Int32(5000),
        )
        _log("SEND", f"dbus-python #{n} (target={desktop_entry})")
    except dbus.DBusException as e:
        _log("SEND-ERROR", f"dbus-python: {e}")


def send_portal(bus: dbus.Bus, desktop_entry: str) -> None:
    n = _next_id()
    nid = f"krema-debug-{n}"
    try:
        proxy = bus.get_object(
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
        )
        iface = dbus.Interface(proxy, "org.freedesktop.portal.Notification")
        iface.AddNotification(
            nid,
            dbus.Dictionary(
                {
                    "title": f"Portal #{n}",
                    "body": f"Via xdg-desktop-portal (target={desktop_entry})",
                    "priority": "normal",
                },
                signature="sv",
            ),
        )
        _log("SEND", f"portal #{n} (id={nid}, target={desktop_entry}) — note: portal ignores desktop-entry hint")
    except dbus.DBusException as e:
        _log("SEND-ERROR", f"portal: {e}")


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------

TAG_COLORS = {
    "Notify": "#2196F3",
    "CloseNotification": "#FF9800",
    "NotificationClosed": "#f44336",
    "SNI-Registered": "#9C27B0",
    "SNI-Unregistered": "#795548",
    "SNI-NewStatus": "#E91E63",
    "SNI": "#9C27B0",
    "SNI-ERROR": "#f44336",
    "SEND": "#4CAF50",
    "SEND-ERROR": "#f44336",
    "INIT": "#607D8B",
    "INIT-ERROR": "#f44336",
}


class DebuggerWindow(QWidget):
    def __init__(self, bus: dbus.Bus | None, sni: StatusNotifierItem | None) -> None:
        super().__init__()
        self._bus = bus
        self._sni = sni
        self.setWindowTitle("Krema Notification Debugger")
        self.resize(960, 650)
        self._build_ui()

    def _get_target(self) -> str:
        """Return the currently selected desktop-entry target."""
        custom = self._custom_entry.text().strip()
        if custom:
            return custom
        idx = self._target_combo.currentIndex()
        if idx >= 0 and idx < len(TARGET_APPS):
            return TARGET_APPS[idx][0]
        return "krema-debugger"

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)

        # -- top: send panel (left) + log panel (right) --
        top = QHBoxLayout()

        # Send panel
        send_group = QGroupBox("Send Notifications")
        send_layout = QVBoxLayout(send_group)

        # Target app selector
        target_label = QLabel("Target App (desktop-entry)")
        target_label.setStyleSheet("font-weight: bold; color: #1565C0;")
        send_layout.addWidget(target_label)

        self._target_combo = QComboBox()
        for entry, name in TARGET_APPS:
            self._target_combo.addItem(f"{name}  ({entry})")
        send_layout.addWidget(self._target_combo)

        custom_layout = QHBoxLayout()
        custom_layout.addWidget(QLabel("Custom:"))
        self._custom_entry = QLineEdit()
        self._custom_entry.setPlaceholderText("Override desktop-entry (e.g. org.kde.dolphin)")
        custom_layout.addWidget(self._custom_entry)
        send_layout.addLayout(custom_layout)

        send_layout.addSpacing(6)

        # Notification buttons
        notif_label = QLabel("Notification (RegisterWatcher)")
        notif_label.setStyleSheet("font-weight: bold;")
        send_layout.addWidget(notif_label)

        buttons = [
            ("1. notify-send", lambda: send_notify_send(self._get_target())),
            ("2. gdbus direct", lambda: send_gdbus(self._get_target())),
            ("3. Python libnotify", lambda: send_libnotify(self._get_target())),
            ("4. Python dbus-python", lambda: self._send_with_bus(lambda b: send_dbus_python(b, self._get_target()))),
            ("5. Portal (no desktop-entry)", lambda: self._send_with_bus(lambda b: send_portal(b, self._get_target()))),
        ]
        for label, callback in buttons:
            btn = QPushButton(label)
            btn.clicked.connect(callback)
            send_layout.addWidget(btn)

        send_layout.addSpacing(10)

        # SNI section
        sni_label = QLabel("SNI (StatusNotifierItem)")
        sni_label.setStyleSheet("font-weight: bold;")
        send_layout.addWidget(sni_label)

        sni_id_layout = QHBoxLayout()
        sni_id_layout.addWidget(QLabel("SNI Id:"))
        self._sni_id_entry = QLineEdit("krema-notification-debugger")
        self._sni_id_entry.setToolTip("SNI Id property. Set before first toggle.\nKrema queries this once at SNI registration.")
        sni_id_layout.addWidget(self._sni_id_entry)
        send_layout.addLayout(sni_id_layout)

        self._sni_btn = QPushButton("6. SNI: Toggle NeedsAttention")
        self._sni_btn.setCheckable(True)
        self._sni_btn.clicked.connect(self._on_sni_toggle)
        send_layout.addWidget(self._sni_btn)

        sni_note = QLabel("Note: SNI Id is queried once by Krema.\nChange Id, then re-register to update.")
        sni_note.setStyleSheet("color: #888; font-size: 10px;")
        sni_note.setWordWrap(True)
        send_layout.addWidget(sni_note)

        self._sni_reregister_btn = QPushButton("Re-register SNI (with new Id)")
        self._sni_reregister_btn.clicked.connect(self._on_sni_reregister)
        send_layout.addWidget(self._sni_reregister_btn)

        send_layout.addStretch()

        # Instructions
        help_label = QLabel(
            "<b>Run Krema with:</b><br>"
            "<code>QT_LOGGING_RULES=\"*=false;krema.notifications=true;qml=true\" krema</code>"
        )
        help_label.setWordWrap(True)
        help_label.setStyleSheet("background: #f5f5f5; padding: 6px; border-radius: 4px; font-size: 10px;")
        send_layout.addWidget(help_label)

        top.addWidget(send_group, stretch=1)

        # Log panel
        log_group = QGroupBox("Received Events (this debugger's watcher)")
        log_layout = QVBoxLayout(log_group)
        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("monospace", 9))
        log_layout.addWidget(self._log)

        clear_btn = QPushButton("Clear Log")
        clear_btn.clicked.connect(self._log.clear)
        log_layout.addWidget(clear_btn)
        top.addWidget(log_group, stretch=2)

        root.addLayout(top)

        # -- bottom: status bar --
        status_group = QGroupBox("Status Summary")
        status_layout = QVBoxLayout(status_group)
        self._status_label = QLabel("No notifications yet")
        self._status_label.setWordWrap(True)
        status_layout.addWidget(self._status_label)
        root.addWidget(status_group)

    def append_log(self, line: str, tag: str) -> None:
        color = TAG_COLORS.get(tag, "#888888")
        html = line.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        # Highlight the tag portion
        html = html.replace(f"[{tag}]", f'<span style="color:{color};font-weight:bold">[{tag}]</span>')
        self._log.append(html)
        # Auto-scroll
        sb = self._log.verticalScrollBar()
        sb.setValue(sb.maximum())

    def update_status(self) -> None:
        parts = []
        # Unread counts
        if _unread:
            items = []
            for entry, ids in sorted(_unread.items()):
                if ids:
                    items.append(f"{entry}: {len(ids)}")
            total = sum(len(ids) for ids in _unread.values())
            parts.append(f"Unread: {', '.join(items)} (total: {total})")
        else:
            parts.append("Unread: 0")

        # SNI attention
        if _sni_attention_apps:
            parts.append(f"SNI NeedsAttention: {', '.join(sorted(_sni_attention_apps))}")

        self._status_label.setText("  |  ".join(parts))

    def _send_with_bus(self, fn) -> None:
        if self._bus is None:
            _log("SEND-ERROR", "D-Bus not connected")
            return
        fn(self._bus)

    def _on_sni_toggle(self) -> None:
        if self._sni is None:
            _log("SNI-ERROR", "SNI not initialized (D-Bus not connected)")
            return
        # Update SNI Id from text field before toggle
        new_id = self._sni_id_entry.text().strip()
        if new_id and new_id != self._sni.sni_id:
            self._sni.sni_id = new_id
            _log("SNI", f"Updated SNI Id to: {new_id}")
        is_attention = self._sni.toggle_attention()
        self._sni_btn.setChecked(is_attention)
        self._sni_btn.setText(
            f"6. SNI: {'NeedsAttention' if is_attention else 'Active'} (click to toggle)"
        )

    def _on_sni_reregister(self) -> None:
        if self._sni is None:
            _log("SNI-ERROR", "SNI not initialized (D-Bus not connected)")
            return
        new_id = self._sni_id_entry.text().strip()
        if new_id:
            self._sni.sni_id = new_id
        self._sni.register_with_watcher()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def _setup_dbus(window: DebuggerWindow) -> None:
    """Set up all D-Bus watchers and signal receivers.

    Called after the GUI is shown so that slow/failing D-Bus calls
    don't block the window from appearing.
    """
    try:
        bus = dbus.SessionBus()
    except dbus.DBusException as e:
        _log("INIT-ERROR", f"Cannot connect to session bus: {e}")
        return

    window._bus = bus

    # RegisterWatcher setup
    watcher = NotificationWatcher(bus)  # noqa: F841
    # prevent GC
    window._watcher = watcher

    bus.add_signal_receiver(
        _on_notification_closed,
        signal_name="NotificationClosed",
        dbus_interface="org.freedesktop.Notifications",
        bus_name="org.freedesktop.Notifications",
        path="/org/freedesktop/Notifications",
    )

    try:
        proxy = bus.get_object(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications"
        )
        iface = dbus.Interface(proxy, "org.kde.NotificationManager")
        iface.RegisterWatcher()
        _log("INIT", "RegisterWatcher() succeeded — this debugger is now receiving notifications too")
    except dbus.DBusException as e:
        _log("INIT-ERROR", f"RegisterWatcher() failed: {e}")

    # SNI setup — use SNI Id from the text field
    sni_id = window._sni_id_entry.text().strip() or "krema-notification-debugger"
    sni = StatusNotifierItem(bus, sni_id=sni_id)
    window._sni = sni
    sni.register_with_watcher()

    # SNI monitoring (external items)
    bus.add_signal_receiver(
        _on_sni_registered,
        signal_name="StatusNotifierItemRegistered",
        dbus_interface="org.kde.StatusNotifierWatcher",
        bus_name="org.kde.StatusNotifierWatcher",
        path="/StatusNotifierWatcher",
    )
    bus.add_signal_receiver(
        _on_sni_unregistered,
        signal_name="StatusNotifierItemUnregistered",
        dbus_interface="org.kde.StatusNotifierWatcher",
        bus_name="org.kde.StatusNotifierWatcher",
        path="/StatusNotifierWatcher",
    )
    # NewStatus from any SNI (no sender filter)
    bus.add_signal_receiver(
        _on_sni_new_status,
        signal_name="NewStatus",
        dbus_interface="org.kde.StatusNotifierItem",
        sender_keyword="sender",
    )


def main() -> None:
    # GLib main loop for dbus-python (must be set before QApplication)
    DBusGMainLoop(set_as_default=True)

    app = QApplication(sys.argv)

    # Show GUI immediately — D-Bus setup happens after
    window = DebuggerWindow(bus=None, sni=None)

    global _log_callback, _status_callback
    _log_callback = window.append_log
    _status_callback = window.update_status

    window.show()

    # Pump GLib events inside Qt event loop
    glib_ctx = GLib.MainContext.default()
    pump_timer = QTimer()
    pump_timer.setInterval(50)
    pump_timer.timeout.connect(lambda: glib_ctx.iteration(False))
    pump_timer.start()
    # prevent GC
    window._pump_timer = pump_timer

    # Deferred D-Bus setup (runs after event loop starts)
    QTimer.singleShot(100, lambda: _setup_dbus(window))

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
