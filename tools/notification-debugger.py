#!/usr/bin/env python3
"""
Notification Debugger GUI for Krema.

Full-featured debugger for fd.o Notifications, Portal notifications,
StatusNotifierItem (SNI), Unity LauncherEntry, KDE Jobs, Inbox simulation,
and Do Not Disturb (DND).

Dependencies:
    pip: PyQt6, dbus-python, PyGObject

Usage:
    python3 tools/notification-debugger.py
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QScrollArea,
    QSlider,
    QSpinBox,
    QTabWidget,
    QTableWidget,
    QTableWidgetItem,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

# ---------------------------------------------------------------------------
# Constants
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

TAG_COLORS = {
    "Notify": "#2196F3",
    "CloseNotification": "#FF9800",
    "NotificationClosed": "#f44336",
    "ActionInvoked": "#4CAF50",
    "NotificationReplied": "#00BCD4",
    "SNI-Registered": "#9C27B0",
    "SNI-Unregistered": "#795548",
    "SNI-NewStatus": "#E91E63",
    "SNI-NewIcon": "#AB47BC",
    "SNI-NewOverlayIcon": "#7E57C2",
    "SNI-NewAttentionIcon": "#EC407A",
    "SNI-NewToolTip": "#5C6BC0",
    "SNI": "#9C27B0",
    "SNI-ERROR": "#f44336",
    "SEND": "#4CAF50",
    "SEND-ERROR": "#f44336",
    "PORTAL": "#FF7043",
    "PORTAL-ERROR": "#f44336",
    "PORTAL-ACTION": "#FF8A65",
    "UNITY": "#00BCD4",
    "UNITY-ERROR": "#f44336",
    "DND": "#FF5722",
    "DND-ERROR": "#f44336",
    "JOB": "#8D6E63",
    "JOB-ERROR": "#f44336",
    "JOB-SIGNAL": "#A1887F",
    "INBOX": "#3F51B5",
    "INBOX-ERROR": "#f44336",
    "BURST": "#8BC34A",
    "CAPS": "#607D8B",
    "INIT": "#607D8B",
    "INIT-ERROR": "#f44336",
}

# ---------------------------------------------------------------------------
# Logging / state helpers
# ---------------------------------------------------------------------------

_log_callback = None
_status_callback = None
_notif_table_callback = None  # (nid, app, summary, status) -> None

_notif_id_to_entry: dict[int, str] = {}
_unread: dict[str, set[int]] = defaultdict(set)
_sni_attention_apps: set[str] = set()
_inbox_ref = None
_send_counter = 0
_recorder = None
_tracked_notif_ids: set[int] = set()
_tracked_jobs: dict[str, object] = {}
_tracked_dnd_cookies: set[int] = set()
_tracked_portal_ids: set[str] = set()
_tracked_unity_entries: set[str] = set()
_event_table_callback = None

_CANONICAL_MECHANISMS = {
    "freedesktop-notifications",
    "portal-notification",
    "unity-launcher-entry",
    "status-notifier-item",
    "kde-job",
    "inbox-simulation",
    "dnd",
}


def _canonical_event(mechanism: str, payload) -> tuple[str, dict]:
    raw = str(mechanism)
    if raw in _CANONICAL_MECHANISMS:
        return raw, dict(payload or {})
    if raw.startswith("portal.") or raw.startswith("reset.portal"):
        canonical = "portal-notification"
    elif raw.startswith("unity.") or raw.startswith("reset.unity"):
        canonical = "unity-launcher-entry"
    elif raw.startswith("sni.") or raw.startswith("reset.sni"):
        canonical = "status-notifier-item"
    elif raw.startswith("jobs.") or raw.startswith("reset.jobs"):
        canonical = "kde-job"
    elif (
        raw.startswith("freedesktop.Inhibit")
        or raw.startswith("freedesktop.UnInhibit")
        or raw == "freedesktop.Notifications.Inhibited"
        or raw.startswith("reset.dnd")
    ):
        canonical = "dnd"
    elif raw.startswith("reset.inbox"):
        canonical = "inbox-simulation"
    else:
        canonical = "freedesktop-notifications"
    event_payload = dict(payload or {})
    event_payload.setdefault("operation", raw)
    return canonical, event_payload


def _json_safe(value, key: str = ""):
    if re.search(r"(pass|secret|token|credential|cookie|auth)", key, re.I):
        return "<redacted>"
    if isinstance(value, dict):
        return {str(k): _json_safe(v, str(k)) for k, v in value.items()}
    if isinstance(value, (list, tuple, set)):
        return [_json_safe(v) for v in value]
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value if not isinstance(value, str) else value[:2048]
    try:
        return _json_safe(value.__int__())
    except (AttributeError, TypeError, ValueError):
        return str(value)[:2048]


class EventRecorder:
    def __init__(self, path: str, scenario: str) -> None:
        self.path = path
        self.scenario = scenario
        self.counts = {"sent": 0, "observed": 0, "reset": 0}
        self.error = None
        try:
            self._stream = open(path, "a", encoding="utf-8", buffering=1)
        except OSError as exc:
            self._stream = None
            self.error = str(exc)

    def record(
        self, direction: str, mechanism: str, *, correlation=None,
        event_id=None, reason="", payload=None, scenario=None,
    ) -> bool:
        if direction not in self.counts or self._stream is None:
            return False
        canonical, event_payload = _canonical_event(mechanism, payload)
        event = {
            "ts": datetime.now().astimezone().isoformat(timespec="milliseconds"),
            "dir": direction,
            "scenario": scenario or self.scenario,
            "mechanism": canonical,
            "corr": correlation,
            "serverId": event_id,
            "reason": reason,
            "payload": _json_safe(event_payload),
        }
        try:
            self._stream.write(json.dumps(event, separators=(",", ":")) + "\n")
            self._stream.flush()
        except OSError as exc:
            self.error = str(exc)
            try:
                self._stream.close()
            except OSError:
                pass
            self._stream = None
            return False
        self.counts[direction] += 1
        return True


def _record_event(
    direction: str, mechanism: str, *, correlation=None,
    event_id=None, reason="", payload=None,
) -> None:
    canonical, event_payload = _canonical_event(mechanism, payload)
    if _recorder:
        _recorder.record(
            direction, canonical, correlation=correlation, event_id=event_id,
            reason=reason, payload=event_payload,
        )
    if _event_table_callback:
        _event_table_callback(
            direction, canonical, correlation, event_id, reason, event_payload
        )
    _update_status()


def _track_notification(nid: int) -> None:
    _tracked_notif_ids.add(int(nid))


def _track_portal(notification_id: str) -> None:
    _tracked_portal_ids.add(str(notification_id))


def _track_unity(desktop_entry: str) -> None:
    _tracked_unity_entries.add(str(desktop_entry))



def _log(tag: str, msg: str) -> None:
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = f"[{ts}] [{tag}] {msg}"
    if _log_callback:
        _log_callback(line, tag)


def _update_status() -> None:
    if _status_callback:
        _status_callback()


def _next_id() -> int:
    global _send_counter
    _send_counter += 1
    return _send_counter


# ---------------------------------------------------------------------------
# D-Bus service classes
# ---------------------------------------------------------------------------


class NotificationWatcherService(dbus.service.Object):
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
    def Notify(self, nid, app_name, replaces_id, app_icon, summary, body,
               actions, hints, expire_timeout):
        desktop_entry = str(hints.get("desktop-entry", app_name) or app_name).lower()

        if replaces_id > 0 and replaces_id in _notif_id_to_entry:
            old = _notif_id_to_entry.pop(replaces_id)
            _unread[old].discard(replaces_id)
            if not _unread[old]:
                del _unread[old]

        _notif_id_to_entry[nid] = desktop_entry
        _unread[desktop_entry].add(nid)

        count = len(_unread[desktop_entry])
        _log("Notify",
             f"id={nid} app={desktop_entry} summary={summary!r} "
             f"replaces={replaces_id} unread={count}")
        _record_event(
            "observed", "freedesktop.Notify", correlation=f"notification:{nid}",
            event_id=int(nid), payload={
                "app": desktop_entry, "summary": str(summary), "body": str(body),
                "replaces_id": int(replaces_id),
            },
        )
        if _notif_table_callback:
            _notif_table_callback(int(nid), desktop_entry, str(summary), "active")
        _update_status()

    @dbus.service.method(
        dbus_interface="org.kde.NotificationWatcher",
        in_signature="u",
    )
    def CloseNotification(self, nid):
        if nid not in _notif_id_to_entry:
            return
        entry = _notif_id_to_entry.pop(nid)
        _unread[entry].discard(nid)
        if not _unread[entry]:
            del _unread[entry]
        _log("CloseNotification", f"id={nid} app={entry}")
        _record_event(
            "observed", "krema.NotificationWatcher.CloseNotification",
            correlation=f"notification:{nid}", event_id=int(nid),
            payload={"app": entry},
        )
        if _notif_table_callback:
            _notif_table_callback(int(nid), entry, "", "closed")
        _update_status()


class StatusNotifierItemService(dbus.service.Object):
    """Full SNI service for testing."""

    IFACE = "org.kde.StatusNotifierItem"

    def __init__(self, bus: dbus.Bus) -> None:
        self._bus = bus
        self._props = {
            "Id": "krema-notification-debugger",
            "Status": "Active",
            "Category": "ApplicationStatus",
            "Title": "Krema Debugger",
            "IconName": "dialog-information",
            "AttentionIconName": "dialog-warning",
            "OverlayIconName": "",
            "ToolTip": dbus.Struct(
                ("", dbus.Array([], signature="(iiay)"), "Krema Debugger", ""),
                signature=None,
            ),
            "ItemIsMenu": dbus.Boolean(False),
            "Menu": dbus.ObjectPath("/NO_DBUSMENU"),
        }
        self._service_name = "org.krema.NotificationDebugger.StatusNotifierItem"
        self._bus_name = dbus.service.BusName(self._service_name, bus=bus)
        super().__init__(self._bus_name, "/StatusNotifierItem")

    def register_with_watcher(self) -> None:
        try:
            if self._bus_name is None:
                self._bus_name = dbus.service.BusName(
                    self._service_name, bus=self._bus
                )
            proxy = self._bus.get_object(
                "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher"
            )
            iface = dbus.Interface(proxy, "org.kde.StatusNotifierWatcher")
            iface.RegisterStatusNotifierItem(self._service_name)
            _record_event(
                "sent", "sni.RegisterStatusNotifierItem",
                correlation=f"sni:{self._service_name}",
                event_id=str(self._props["Id"]),
                payload={"service": self._service_name, "id": self._props["Id"]},
            )
            _log("SNI", f"RegisterStatusNotifierItem succeeded (Id={self._props['Id']})")
        except dbus.DBusException as e:
            _record_event(
                "sent", "sni.RegisterStatusNotifierItem",
                correlation=f"sni:{self._service_name}", reason=str(e),
            )
            _log("SNI-ERROR", f"RegisterStatusNotifierItem failed: {e}")

    def unregister_with_watcher(self) -> bool:
        try:
            self._bus.release_name(self._service_name)
            self._bus_name = None
            _record_event(
                "sent", "sni.UnregisterStatusNotifierItem",
                correlation=f"sni:{self._service_name}",
                event_id=str(self._props["Id"]),
                reason="released bus name",
            )
            _log("SNI", "StatusNotifierItem unregistered by releasing bus name")
            return True
        except dbus.DBusException as e:
            _record_event(
                "sent", "sni.UnregisterStatusNotifierItem",
                correlation=f"sni:{self._service_name}", reason=str(e),
            )
            _log("SNI-ERROR", f"UnregisterStatusNotifierItem failed: {e}")
            return False


    def set_property(self, name: str, value) -> None:
        self._props[name] = value
        self.PropertiesChanged(self.IFACE, {name: value}, [])
        _record_event(
            "sent", "sni.PropertiesChanged", correlation=f"sni:{name}",
            payload={"property": name, "value": value},
        )

    def set_status(self, status: str) -> None:
        self._props["Status"] = status
        self.NewStatus(status)
        _record_event(
            "sent", "sni.NewStatus", correlation="sni:status",
            payload={"status": status},
        )
        _log("SNI", f"NewStatus -> {status}")

    def set_icon(self, icon_name: str) -> None:
        self._props["IconName"] = icon_name
        self.NewIcon()
        _record_event(
            "sent", "sni.NewIcon", correlation="sni:icon",
            payload={"icon": icon_name},
        )
        _log("SNI", f"NewIcon -> {icon_name}")

    def set_overlay_icon(self, icon_name: str) -> None:
        self._props["OverlayIconName"] = icon_name
        self.NewOverlayIcon()
        _record_event(
            "sent", "sni.NewOverlayIcon", correlation="sni:overlay",
            payload={"icon": icon_name},
        )
        _log("SNI", f"NewOverlayIcon -> {icon_name}")

    def set_attention_icon(self, icon_name: str) -> None:
        self._props["AttentionIconName"] = icon_name
        self.NewAttentionIcon()
        _record_event(
            "sent", "sni.NewAttentionIcon", correlation="sni:attention",
            payload={"icon": icon_name},
        )
        _log("SNI", f"NewAttentionIcon -> {icon_name}")

    def set_tooltip(self, title: str, body: str = "") -> None:
        self._props["ToolTip"] = dbus.Struct(
            ("", dbus.Array([], signature="(iiay)"), title, body),
            signature=None,
        )
        self.NewToolTip()
        _record_event(
            "sent", "sni.NewToolTip", correlation="sni:tooltip",
            payload={"title": title, "body": body},
        )
        _log("SNI", f"NewToolTip -> title={title!r} body={body!r}")


    # -- D-Bus signals --

    @dbus.service.signal(dbus_interface="org.kde.StatusNotifierItem", signature="s")
    def NewStatus(self, status):
        pass

    @dbus.service.signal(dbus_interface="org.kde.StatusNotifierItem")
    def NewIcon(self):
        pass

    @dbus.service.signal(dbus_interface="org.kde.StatusNotifierItem")
    def NewOverlayIcon(self):
        pass

    @dbus.service.signal(dbus_interface="org.kde.StatusNotifierItem")
    def NewAttentionIcon(self):
        pass

    @dbus.service.signal(dbus_interface="org.kde.StatusNotifierItem")
    def NewToolTip(self):
        pass

    @dbus.service.signal(
        dbus_interface="org.freedesktop.DBus.Properties",
        signature="sa{sv}as",
    )
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    # -- D-Bus properties --

    @dbus.service.method(
        dbus_interface="org.freedesktop.DBus.Properties",
        in_signature="ss", out_signature="v",
    )
    def Get(self, interface, prop):
        if interface != self.IFACE:
            raise dbus.DBusException(f"Unknown interface {interface}")
        if prop not in self._props:
            raise dbus.DBusException(f"Unknown property {prop}")
        return self._props[prop]

    @dbus.service.method(
        dbus_interface="org.freedesktop.DBus.Properties",
        in_signature="s", out_signature="a{sv}",
    )
    def GetAll(self, interface):
        if interface != self.IFACE:
            return {}
        return dict(self._props)


# ---------------------------------------------------------------------------
# D-Bus signal handlers
# ---------------------------------------------------------------------------


def _on_notification_closed(nid: int, reason: int) -> None:
    reasons = {1: "expired", 2: "dismissed", 3: "closed-by-call", 4: "undefined"}
    reason_str = reasons.get(reason, f"unknown({reason})")

    if nid in _notif_id_to_entry:
        entry = _notif_id_to_entry.pop(nid)
        _unread[entry].discard(nid)
        if not _unread[entry]:
            del _unread[entry]
        _log("NotificationClosed", f"id={nid} app={entry} reason={reason_str}")
    else:
        _log("NotificationClosed", f"id={nid} reason={reason_str}")

    if _notif_table_callback:
        _notif_table_callback(int(nid), "", "", f"closed ({reason_str})")
    _record_event(
        "observed", "freedesktop.NotificationClosed",
        correlation=f"notification:{nid}", event_id=int(nid),
        reason=reason_str,
    )
    if _inbox_ref:
        _inbox_ref.on_external_close(int(nid))
    _tracked_notif_ids.discard(int(nid))
    _update_status()


def _on_action_invoked(nid: int, action_key: str) -> None:
    _log("ActionInvoked", f"id={nid} action={action_key!r}")
    _record_event(
        "observed", "freedesktop.ActionInvoked",
        correlation=f"notification:{nid}", event_id=int(nid),
        payload={"action": action_key},
    )


def _on_notification_replied(nid: int, text: str) -> None:
    _log("NotificationReplied", f"id={nid} text={text!r}")
    _record_event(
        "observed", "freedesktop.NotificationReplied",
        correlation=f"notification:{nid}", event_id=int(nid),
        payload={"text": text},
    )

def _on_sni_registered(service: str) -> None:
    _log("SNI-Registered", f"service={service}")
    _record_event(
        "observed", "sni.StatusNotifierItemRegistered",
        correlation=f"sni:{service}", event_id=service,
    )
    _update_status()


def _on_sni_unregistered(service: str) -> None:
    _sni_attention_apps.discard(service)
    _log("SNI-Unregistered", f"service={service}")
    _record_event(
        "observed", "sni.StatusNotifierItemUnregistered",
        correlation=f"sni:{service}", event_id=service,
    )
    _update_status()


def _on_sni_new_status(status: str, *, sender: str = None, **kwargs) -> None:
    src = sender or "unknown"
    if status == "NeedsAttention":
        _sni_attention_apps.add(src)
    else:
        _sni_attention_apps.discard(src)
    _log("SNI-NewStatus", f"sender={src} status={status}")
    _record_event(
        "observed", "sni.NewStatus", correlation=f"sni:{src}",
        payload={"sender": src, "status": status},
    )
    _update_status()


def _on_sni_signal(name: str, *, sender: str = None, **kwargs) -> None:
    src = sender or "unknown"
    _log(f"SNI-{name}", f"sender={src}")
    _record_event(
        "observed", f"sni.{name}", correlation=f"sni:{src}",
        payload={"sender": src},
    )


# ---------------------------------------------------------------------------
# D-Bus client functions
# ---------------------------------------------------------------------------


def send_notify_dbus(bus, *, app_name, replaces_id=0,
                     icon="dialog-information", summary="",
                     body="", actions=None, hints=None, timeout=5000):
    """Send fd.o Notify call and return server-assigned ID."""
    try:
        proxy = bus.get_object(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications"
        )
        iface = dbus.Interface(proxy, "org.freedesktop.Notifications")
        act = dbus.Array(actions or [], signature="s")
        h = dbus.Dictionary(hints or {}, signature="sv")
        nid = iface.Notify(app_name, dbus.UInt32(replaces_id), icon,
                           summary, body, act, h, dbus.Int32(timeout))
        nid = int(nid)
        _track_notification(nid)
        _record_event(
            "sent", "freedesktop.Notify", correlation=f"notification:{nid}",
            event_id=nid, payload={
                "app": app_name, "summary": summary, "body": body,
                "replaces_id": replaces_id, "timeout": timeout,
            },
        )
        _log("SEND", f"Notify -> id={nid} app={app_name} summary={summary!r}")
        return nid
    except dbus.DBusException as e:
        _log("SEND-ERROR", f"Notify: {e}")
        return None


def send_portal(bus, *, notification_id, title="", body="",
                priority="normal", markup_body=False, icon="",
                sound=False, display_hint="", category="",
                default_action="", buttons=None):
    """Send Portal AddNotification."""
    try:
        proxy = bus.get_object(
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
        )
        iface = dbus.Interface(proxy, "org.freedesktop.portal.Notification")
        data = {"title": title, "priority": priority}
        data["markup-body" if markup_body else "body"] = body
        if icon:
            data["icon"] = dbus.Struct(
                (
                    "themed",
                    dbus.Variant(
                        dbus.Array([icon], signature="s"),
                        signature="as",
                    ),
                ),
                signature="(sv)",
            )
        data["sound"] = dbus.String("default" if sound else "silent")
        if display_hint:
            data["display-hint"] = dbus.Array([display_hint], signature="s")
        if category:
            data["category"] = category
        if default_action:
            data["default-action"] = default_action
        if buttons:
            btn_arr = []
            for b in buttons:
                bd = dbus.Dictionary(
                    {"label": b.get("label", ""), "action": b.get("action", "")},
                    signature="sv",
                )
                if b.get("purpose"):
                    bd["purpose"] = b["purpose"]
                btn_arr.append(bd)
            data["buttons"] = dbus.Array(btn_arr, signature="a{sv}")

        iface.AddNotification(
            notification_id, dbus.Dictionary(data, signature="sv")
        )
        _track_portal(notification_id)
        _record_event(
            "sent", "portal.AddNotification",
            correlation=f"portal:{notification_id}", event_id=notification_id,
            payload={
                "title": title,
                "markup-body" if markup_body else "body": body,
                "priority": priority,
            },
        )
        _log("PORTAL", f"AddNotification id={notification_id!r} title={title!r}")
    except dbus.DBusException as e:
        _log("PORTAL-ERROR", f"AddNotification: {e}")


def remove_portal(bus, notification_id):
    try:
        proxy = bus.get_object(
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
        )
        iface = dbus.Interface(proxy, "org.freedesktop.portal.Notification")
        iface.RemoveNotification(notification_id)
        _tracked_portal_ids.discard(str(notification_id))
        _record_event(
            "sent", "portal.RemoveNotification",
            correlation=f"portal:{notification_id}", event_id=notification_id,
        )
        _log("PORTAL", f"RemoveNotification id={notification_id!r}")
        return True
    except dbus.DBusException as e:
        _log("PORTAL-ERROR", f"RemoveNotification: {e}")
        return False

def send_unity_update(bus, desktop_entry, props):
    """Send com.canonical.Unity.LauncherEntry.Update signal."""
    try:
        uri = f"application://{desktop_entry}.desktop"
        msg = dbus.lowlevel.SignalMessage(
            "/kremaDebugger", "com.canonical.Unity.LauncherEntry", "Update"
        )
        msg.append(uri, signature="s")
        msg.append(dbus.Dictionary(props, signature="sv"), signature="a{sv}")
        bus.send_message(msg)
        _track_unity(desktop_entry)
        _record_event(
            "sent", "unity.LauncherEntry.Update",
            correlation=f"unity:{desktop_entry}", event_id=desktop_entry,
            payload={"uri": uri, "props": dict(props)},
        )
        _log("UNITY", f"Update({uri}, {dict(props)})")
        return True
    except Exception as e:
        _log("UNITY-ERROR", f"{e}")
        return False

def close_notification(bus, nid):
    try:
        proxy = bus.get_object(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications"
        )
        iface = dbus.Interface(proxy, "org.freedesktop.Notifications")
        iface.CloseNotification(dbus.UInt32(nid))
        _tracked_notif_ids.discard(int(nid))
        _record_event(
            "sent", "freedesktop.CloseNotification",
            correlation=f"notification:{nid}", event_id=int(nid),
        )
        _log("SEND", f"CloseNotification(id={nid})")
        return True
    except dbus.DBusException as e:
        _log("SEND-ERROR", f"CloseNotification: {e}")
        return False

def get_capabilities(bus):
    try:
        proxy = bus.get_object(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications"
        )
        iface = dbus.Interface(proxy, "org.freedesktop.Notifications")
        caps = list(iface.GetCapabilities())
        _log("CAPS", f"Capabilities: {caps}")
        return caps
    except dbus.DBusException as e:
        _log("SEND-ERROR", f"GetCapabilities: {e}")
        return []


def get_server_information(bus):
    try:
        proxy = bus.get_object(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications"
        )
        iface = dbus.Interface(proxy, "org.freedesktop.Notifications")
        info = iface.GetServerInformation()
        _log("CAPS", f"Server: name={info[0]} vendor={info[1]} "
             f"version={info[2]} spec={info[3]}")
        return tuple(info)
    except dbus.DBusException as e:
        _log("SEND-ERROR", f"GetServerInformation: {e}")
        return ()


def create_job(bus, desktop_entry, capabilities=0):
    """Create a job via kuiserver. Returns (path, iface) or (None, None)."""
    try:
        proxy = bus.get_object("org.kde.kuiserver", "/JobViewServer")
        iface = dbus.Interface(proxy, "org.kde.JobViewServer")
        path = iface.requestView(desktop_entry, "", dbus.Int32(capabilities))
        _log("JOB", f"requestView -> {path}")
        job_proxy = bus.get_object("org.kde.kuiserver", path)
        job_iface = dbus.Interface(job_proxy, "org.kde.JobViewV2")
        _tracked_jobs[str(path)] = job_iface
        _record_event(
            "sent", "jobs.requestView", correlation=f"job:{path}",
            event_id=str(path), payload={"desktop_entry": desktop_entry},
        )
        return str(path), job_iface
    except dbus.DBusException as e:
        _log("JOB-ERROR", f"requestView: {e}")
        return None, None


def inhibit_dnd(bus, app, reason):
    try:
        proxy = bus.get_object(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications"
        )
        iface = dbus.Interface(proxy, "org.freedesktop.Notifications")
        cookie = iface.Inhibit(app, reason, dbus.Dictionary({}, signature="sv"))
        cookie = int(cookie)
        _tracked_dnd_cookies.add(cookie)
        _record_event(
            "sent", "freedesktop.Inhibit", correlation=f"dnd:{cookie}",
            event_id=cookie, payload={"app": app, "reason": reason},
        )
        _log("DND", f"Inhibit({app!r}, {reason!r}) -> cookie={cookie}")
        return cookie
    except dbus.DBusException as e:
        _log("DND-ERROR", f"Inhibit: {e}")
        return None


def uninhibit_dnd(bus, cookie):
    try:
        proxy = bus.get_object(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications"
        )
        iface = dbus.Interface(proxy, "org.freedesktop.Notifications")
        iface.UnInhibit(dbus.UInt32(cookie))
        _tracked_dnd_cookies.discard(int(cookie))
        _record_event(
            "sent", "freedesktop.UnInhibit", correlation=f"dnd:{cookie}",
            event_id=int(cookie),
        )
        _log("DND", f"UnInhibit(cookie={cookie})")
        return True
    except dbus.DBusException as e:
        _log("DND-ERROR", f"UnInhibit: {e}")
        return False

# ---------------------------------------------------------------------------
# Inbox model
# ---------------------------------------------------------------------------


@dataclass
class InboxMessage:
    text: str
    server_id: int | None = None
    read: bool = False


@dataclass
class InboxChannel:
    name: str
    desktop_entry: str
    messages: list[InboxMessage] = field(default_factory=list)

    @property
    def unread_count(self) -> int:
        return sum(1 for m in self.messages if not m.read)


class InboxModel:
    """Manages channels and message state, syncs Unity badge + SNI."""

    def __init__(self) -> None:
        self.channels: list[InboxChannel] = []
        self._bus = None
        self._sni = None
        self._ui_callback = None

    def set_bus(self, bus) -> None:
        self._bus = bus

    def set_sni(self, sni) -> None:
        self._sni = sni

    @property
    def total_unread(self) -> int:
        return sum(ch.unread_count for ch in self.channels)

    def add_channel(self, name: str, desktop_entry: str) -> InboxChannel:
        ch = InboxChannel(name=name, desktop_entry=desktop_entry)
        self.channels.append(ch)
        return ch

    def remove_channel(self, channel: InboxChannel) -> None:
        if self._bus:
            for m in channel.messages:
                if not m.read and m.server_id is not None:
                    close_notification(self._bus, m.server_id)
        self.channels.remove(channel)
        self._sync_badge()

    def send_message(self, channel: InboxChannel, text: str):
        if not self._bus:
            _log("INBOX-ERROR", "D-Bus not connected")
            return None
        n = _next_id()
        summary = f"[{channel.name}] Message #{n}"
        hints = {"desktop-entry": channel.desktop_entry}
        server_id = send_notify_dbus(
            self._bus, app_name=channel.desktop_entry,
            summary=summary, body=text, hints=hints, timeout=0,
        )
        if server_id is None:
            return None
        msg = InboxMessage(text=text, server_id=server_id)
        channel.messages.append(msg)
        _log("INBOX", f"Sent to #{channel.name}: server_id={server_id}")
        self._sync_badge()
        return msg

    def mark_read(self, channel: InboxChannel, msg: InboxMessage) -> None:
        if msg.read:
            return
        msg.read = True
        if self._bus and msg.server_id is not None:
            close_notification(self._bus, msg.server_id)
        _log("INBOX", f"Read: #{channel.name} server_id={msg.server_id}")
        self._sync_badge()

    def mark_channel_read(self, channel: InboxChannel) -> None:
        for m in channel.messages:
            if not m.read:
                m.read = True
                if self._bus and m.server_id is not None:
                    close_notification(self._bus, m.server_id)
        _log("INBOX", f"Read all in #{channel.name}")
        self._sync_badge()

    def on_external_close(self, server_id: int) -> None:
        for ch in self.channels:
            for m in ch.messages:
                if m.server_id == server_id and not m.read:
                    m.read = True
                    _log("INBOX", f"External dismiss: #{ch.name} server_id={server_id}")
                    self._sync_badge()
                    return

    def _sync_badge(self) -> None:
        total = self.total_unread
        if self._bus:
            entries = {ch.desktop_entry for ch in self.channels}
            for entry in entries:
                send_unity_update(self._bus, entry, {
                    "count": dbus.Int64(total),
                    "count-visible": dbus.Boolean(total > 0),
                })
        if self._sni:
            new_status = "NeedsAttention" if total > 0 else "Active"
            if self._sni._props["Status"] != new_status:
                self._sni.set_status(new_status)
        if self._ui_callback:
            self._ui_callback()


# ---------------------------------------------------------------------------
# GUI: shared widgets
# ---------------------------------------------------------------------------


class TargetBar(QWidget):
    """Shared target app selector bar."""

    def __init__(self) -> None:
        super().__init__()
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        layout.addWidget(QLabel("Target:"))
        self._combo = QComboBox()
        for entry, name in TARGET_APPS:
            self._combo.addItem(f"{name} ({entry})")
        layout.addWidget(self._combo, stretch=1)

        layout.addWidget(QLabel("Custom:"))
        self._custom = QLineEdit()
        self._custom.setPlaceholderText("desktop-entry override")
        layout.addWidget(self._custom, stretch=1)

    def get_target(self) -> str:
        custom = self._custom.text().strip()
        if custom:
            return custom
        idx = self._combo.currentIndex()
        if 0 <= idx < len(TARGET_APPS):
            return TARGET_APPS[idx][0]
        return "krema-debugger"


class LogPanel(QWidget):
    """Shared log viewer."""

    def __init__(self) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("monospace", 9))
        layout.addWidget(self._log)

        clear_btn = QPushButton("Clear Log")
        clear_btn.clicked.connect(self._log.clear)
        layout.addWidget(clear_btn)

    def append(self, line: str, tag: str) -> None:
        color = TAG_COLORS.get(tag, "#888888")
        html = line.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        html = html.replace(
            f"[{tag}]",
            f'<span style="color:{color};font-weight:bold">[{tag}]</span>',
        )
        self._log.append(html)
        sb = self._log.verticalScrollBar()
        sb.setValue(sb.maximum())


# ---------------------------------------------------------------------------
# Tab 0: Notifications
# ---------------------------------------------------------------------------


class NotificationsTab(QWidget):
    """fd.o Notifications with all hints."""

    def __init__(self, target_bar: TargetBar) -> None:
        super().__init__()
        self._target = target_bar
        self._bus = None
        self._build_ui()

    def set_bus(self, bus) -> None:
        self._bus = bus

    def _build_ui(self) -> None:
        root = QHBoxLayout(self)

        # Left: send controls
        scroll_content = QWidget()
        sl = QVBoxLayout(scroll_content)

        sl.addWidget(QLabel("Summary:"))
        self._summary = QLineEdit("Test notification")
        sl.addWidget(self._summary)

        sl.addWidget(QLabel("Body:"))
        self._body = QLineEdit("Notification body text")
        sl.addWidget(self._body)

        sl.addWidget(QLabel("Icon:"))
        self._icon = QLineEdit("dialog-information")
        sl.addWidget(self._icon)

        row = QHBoxLayout()
        row.addWidget(QLabel("Replaces ID:"))
        self._replaces_id = QSpinBox()
        self._replaces_id.setRange(0, 999999)
        row.addWidget(self._replaces_id)
        row.addWidget(QLabel("Timeout:"))
        self._timeout = QSpinBox()
        self._timeout.setRange(-1, 60000)
        self._timeout.setValue(5000)
        self._timeout.setSuffix("ms")
        row.addWidget(self._timeout)
        sl.addLayout(row)

        # -- Hints --
        lbl = QLabel("Hints")
        lbl.setStyleSheet("font-weight:bold; margin-top:6px;")
        sl.addWidget(lbl)

        row = QHBoxLayout()
        row.addWidget(QLabel("Urgency:"))
        self._urgency = QComboBox()
        self._urgency.addItems(["(none)", "low (0)", "normal (1)", "critical (2)"])
        row.addWidget(self._urgency)
        sl.addLayout(row)

        row = QHBoxLayout()
        row.addWidget(QLabel("Category:"))
        self._category = QComboBox()
        self._category.setEditable(True)
        self._category.addItems([
            "", "email", "email.arrived", "im", "im.received",
            "transfer", "transfer.complete", "device", "network",
        ])
        row.addWidget(self._category)
        sl.addLayout(row)

        self._de_override = QCheckBox("Override desktop-entry from target")
        self._de_override.setChecked(True)
        sl.addWidget(self._de_override)

        self._resident = QCheckBox("resident")
        sl.addWidget(self._resident)
        self._transient = QCheckBox("transient")
        sl.addWidget(self._transient)
        self._action_icons = QCheckBox("action-icons")
        sl.addWidget(self._action_icons)

        row = QHBoxLayout()
        row.addWidget(QLabel("image-path:"))
        self._image_path = QLineEdit()
        self._image_path.setPlaceholderText("/path/to/image or icon name")
        row.addWidget(self._image_path)
        sl.addLayout(row)

        row = QHBoxLayout()
        row.addWidget(QLabel("sound-name:"))
        self._sound = QLineEdit()
        self._sound.setPlaceholderText("message-new-instant")
        row.addWidget(self._sound)
        sl.addLayout(row)

        self._inline_reply = QCheckBox("inline-reply")
        sl.addWidget(self._inline_reply)

        row = QHBoxLayout()
        self._use_xy = QCheckBox("x,y:")
        row.addWidget(self._use_xy)
        self._hint_x = QSpinBox()
        self._hint_x.setRange(0, 9999)
        row.addWidget(self._hint_x)
        self._hint_y = QSpinBox()
        self._hint_y.setRange(0, 9999)
        row.addWidget(self._hint_y)
        sl.addLayout(row)

        # -- Actions --
        lbl = QLabel("Actions (key=label per line):")
        lbl.setStyleSheet("font-weight:bold; margin-top:6px;")
        sl.addWidget(lbl)
        self._actions = QTextEdit()
        self._actions.setPlaceholderText("default=Open\naction1=Reply")
        self._actions.setMaximumHeight(60)
        sl.addWidget(self._actions)

        # Send
        send_btn = QPushButton("Send Notification")
        send_btn.setStyleSheet("font-weight:bold; padding:8px;")
        send_btn.clicked.connect(self._on_send)
        sl.addWidget(send_btn)

        # Close by ID
        row = QHBoxLayout()
        row.addWidget(QLabel("Close ID:"))
        self._close_id = QSpinBox()
        self._close_id.setRange(1, 999999)
        row.addWidget(self._close_id)
        close_btn = QPushButton("CloseNotification")
        close_btn.clicked.connect(self._on_close)
        row.addWidget(close_btn)
        sl.addLayout(row)

        # Caps / Info
        row = QHBoxLayout()
        caps_btn = QPushButton("GetCapabilities")
        caps_btn.clicked.connect(self._on_caps)
        row.addWidget(caps_btn)
        info_btn = QPushButton("GetServerInformation")
        info_btn.clicked.connect(self._on_info)
        row.addWidget(info_btn)
        sl.addLayout(row)

        # Burst
        row = QHBoxLayout()
        burst_btn = QPushButton("Burst")
        burst_btn.clicked.connect(self._on_burst)
        row.addWidget(burst_btn)
        self._burst_count = QSpinBox()
        self._burst_count.setRange(2, 50)
        self._burst_count.setValue(5)
        row.addWidget(self._burst_count)
        self._burst_interval = QSpinBox()
        self._burst_interval.setRange(50, 5000)
        self._burst_interval.setValue(500)
        self._burst_interval.setSuffix("ms")
        row.addWidget(self._burst_interval)
        sl.addLayout(row)

        sl.addStretch()

        scroll = QScrollArea()
        scroll.setWidget(scroll_content)
        scroll.setWidgetResizable(True)
        scroll.setMinimumWidth(320)
        root.addWidget(scroll, stretch=1)

        # Right: received events table
        right = QVBoxLayout()
        right.addWidget(QLabel("Received Notifications"))
        self._table = QTableWidget(0, 4)
        self._table.setHorizontalHeaderLabels(["ID", "App", "Summary", "Status"])
        self._table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )
        right.addWidget(self._table)
        root.addLayout(right, stretch=1)

    def add_received(self, nid: int, app: str, summary: str, status: str) -> None:
        for row in range(self._table.rowCount()):
            item = self._table.item(row, 0)
            if item and item.text() == str(nid):
                self._table.setItem(row, 3, QTableWidgetItem(status))
                return
        row = self._table.rowCount()
        self._table.insertRow(row)
        self._table.setItem(row, 0, QTableWidgetItem(str(nid)))
        self._table.setItem(row, 1, QTableWidgetItem(app))
        self._table.setItem(row, 2, QTableWidgetItem(summary))
        self._table.setItem(row, 3, QTableWidgetItem(status))

    def _build_hints(self) -> dict:
        hints = {}
        if self._de_override.isChecked():
            hints["desktop-entry"] = self._target.get_target()
        idx = self._urgency.currentIndex()
        if idx > 0:
            hints["urgency"] = dbus.Byte(idx - 1)
        cat = self._category.currentText().strip()
        if cat:
            hints["category"] = cat
        if self._resident.isChecked():
            hints["resident"] = dbus.Boolean(True)
        if self._transient.isChecked():
            hints["transient"] = dbus.Boolean(True)
        if self._action_icons.isChecked():
            hints["action-icons"] = dbus.Boolean(True)
        img = self._image_path.text().strip()
        if img:
            hints["image-path"] = img
        snd = self._sound.text().strip()
        if snd:
            hints["sound-name"] = snd
        if self._inline_reply.isChecked():
            hints["inline-reply"] = dbus.Boolean(True)
        if self._use_xy.isChecked():
            hints["x"] = dbus.Int32(self._hint_x.value())
            hints["y"] = dbus.Int32(self._hint_y.value())
        return hints

    def _parse_actions(self) -> list[str]:
        text = self._actions.toPlainText().strip()
        if not text:
            return []
        result = []
        for line in text.split("\n"):
            line = line.strip()
            if "=" in line:
                key, label = line.split("=", 1)
                result.extend([key.strip(), label.strip()])
        return result

    def _on_send(self) -> None:
        if not self._bus:
            _log("SEND-ERROR", "D-Bus not connected")
            return
        send_notify_dbus(
            self._bus, app_name=self._target.get_target(),
            replaces_id=self._replaces_id.value(),
            icon=self._icon.text().strip() or "dialog-information",
            summary=self._summary.text(), body=self._body.text(),
            actions=self._parse_actions(), hints=self._build_hints(),
            timeout=self._timeout.value(),
        )

    def _on_close(self) -> None:
        if self._bus:
            close_notification(self._bus, self._close_id.value())

    def _on_caps(self) -> None:
        if self._bus:
            get_capabilities(self._bus)

    def _on_info(self) -> None:
        if self._bus:
            get_server_information(self._bus)

    def _on_burst(self) -> None:
        if not self._bus:
            return
        count = self._burst_count.value()
        interval = self._burst_interval.value()
        target = self._target.get_target()
        _log("BURST", f"Sending {count} to {target} every {interval}ms")
        self._burst_remaining = count
        self._burst_timer = QTimer()
        self._burst_timer.setInterval(interval)
        self._burst_timer.timeout.connect(lambda: self._burst_tick(target))
        self._burst_timer.start()
        self._burst_tick(target)

    def _burst_tick(self, target: str) -> None:
        if self._burst_remaining <= 0:
            self._burst_timer.stop()
            _log("BURST", "Done")
            return
        n = _next_id()
        send_notify_dbus(
            self._bus, app_name=target,
            summary=f"Burst #{n}", body=f"Burst notification #{n}",
            hints={"desktop-entry": target}, timeout=5000,
        )
        self._burst_remaining -= 1


# ---------------------------------------------------------------------------
# Tab 1: Portal
# ---------------------------------------------------------------------------


class PortalTab(QWidget):
    """Portal notifications."""

    def __init__(self, target_bar: TargetBar) -> None:
        super().__init__()
        self._target = target_bar
        self._bus = None
        self._portal_counter = 0
        self._build_ui()

    def set_bus(self, bus) -> None:
        self._bus = bus

    def _build_ui(self) -> None:
        layout = QVBoxLayout(self)

        row = QHBoxLayout()
        row.addWidget(QLabel("Notification ID:"))
        self._notif_id = QLineEdit("krema-debug-1")
        row.addWidget(self._notif_id)
        auto_btn = QPushButton("Auto")
        auto_btn.clicked.connect(self._auto_id)
        row.addWidget(auto_btn)
        layout.addLayout(row)

        layout.addWidget(QLabel("Title:"))
        self._title = QLineEdit("Portal Notification")
        layout.addWidget(self._title)

        layout.addWidget(QLabel("Body:"))
        self._body = QLineEdit("Portal body text")
        layout.addWidget(self._body)

        self._markup = QCheckBox("Body contains markup (sent as markup-body)")
        layout.addWidget(self._markup)

        row = QHBoxLayout()
        row.addWidget(QLabel("Icon:"))
        self._icon = QLineEdit("dialog-information")
        row.addWidget(self._icon)
        layout.addLayout(row)

        row = QHBoxLayout()
        row.addWidget(QLabel("Priority:"))
        self._priority = QComboBox()
        self._priority.addItems(["low", "normal", "high", "urgent"])
        self._priority.setCurrentIndex(1)
        row.addWidget(self._priority)
        layout.addLayout(row)

        row = QHBoxLayout()
        row.addWidget(QLabel("Display hint:"))
        self._display_hint = QComboBox()
        self._display_hint.addItems([
            "(none)", "transient", "tray", "persistent",
            "hide-on-lockscreen", "show-as-banner", "hide-content-on-lockscreen",
        ])
        row.addWidget(self._display_hint)
        layout.addLayout(row)

        row = QHBoxLayout()
        row.addWidget(QLabel("Category:"))
        self._category = QComboBox()
        self._category.setEditable(True)
        self._category.addItems([
            "(none)", "im", "im.received", "im.missed-call",
            "email", "email.arrived", "call", "call.incoming", "sms",
        ])
        row.addWidget(self._category)
        layout.addLayout(row)

        self._sound = QCheckBox("Play sound")
        layout.addWidget(self._sound)

        row = QHBoxLayout()
        row.addWidget(QLabel("Default action:"))
        self._default_action = QLineEdit()
        self._default_action.setPlaceholderText("action-name")
        row.addWidget(self._default_action)
        layout.addLayout(row)

        layout.addWidget(QLabel("Buttons (label=action per line):"))
        self._buttons = QTextEdit()
        self._buttons.setPlaceholderText("Open=open-action\nDismiss=dismiss-action")
        self._buttons.setMaximumHeight(50)
        layout.addWidget(self._buttons)

        btn_row = QHBoxLayout()
        send_btn = QPushButton("AddNotification")
        send_btn.setStyleSheet("font-weight:bold; padding:8px;")
        send_btn.clicked.connect(self._on_send)
        btn_row.addWidget(send_btn)
        remove_btn = QPushButton("RemoveNotification")
        remove_btn.clicked.connect(self._on_remove)
        btn_row.addWidget(remove_btn)
        layout.addLayout(btn_row)

        layout.addStretch()

    def _auto_id(self) -> None:
        self._portal_counter += 1
        self._notif_id.setText(f"krema-debug-{self._portal_counter}")

    def _parse_buttons(self):
        text = self._buttons.toPlainText().strip()
        if not text:
            return None
        result = []
        for line in text.split("\n"):
            line = line.strip()
            if "=" in line:
                label, action = line.split("=", 1)
                result.append({"label": label.strip(), "action": action.strip()})
        return result or None

    def _on_send(self) -> None:
        if not self._bus:
            _log("PORTAL-ERROR", "D-Bus not connected")
            return
        dh = self._display_hint.currentText()
        if dh == "(none)":
            dh = ""
        cat = self._category.currentText()
        if cat == "(none)":
            cat = ""
        send_portal(
            self._bus,
            notification_id=self._notif_id.text().strip(),
            title=self._title.text(), body=self._body.text(),
            priority=self._priority.currentText(),
            markup_body=self._markup.isChecked(),
            icon=self._icon.text().strip(),
            sound=self._sound.isChecked(),
            display_hint=dh, category=cat,
            default_action=self._default_action.text().strip(),
            buttons=self._parse_buttons(),
        )

    def _on_remove(self) -> None:
        if self._bus:
            remove_portal(self._bus, self._notif_id.text().strip())


# ---------------------------------------------------------------------------
# Tab 2: SmartLauncher
# ---------------------------------------------------------------------------


class SmartLauncherTab(QWidget):
    """Unity LauncherEntry."""

    def __init__(self, target_bar: TargetBar) -> None:
        super().__init__()
        self._target = target_bar
        self._bus = None
        self._build_ui()

    def set_bus(self, bus) -> None:
        self._bus = bus

    def _build_ui(self) -> None:
        layout = QVBoxLayout(self)

        # Progress
        pg = QGroupBox("Progress")
        pgl = QVBoxLayout(pg)
        row = QHBoxLayout()
        row.addWidget(QLabel("Value:"))
        self._progress = QSlider(Qt.Orientation.Horizontal)
        self._progress.setRange(0, 100)
        self._progress.setValue(50)
        row.addWidget(self._progress)
        self._prog_label = QLabel("50%")
        self._progress.valueChanged.connect(
            lambda v: self._prog_label.setText(f"{v}%")
        )
        row.addWidget(self._prog_label)
        pgl.addLayout(row)
        self._prog_visible = QCheckBox("Visible")
        self._prog_visible.setChecked(True)
        pgl.addWidget(self._prog_visible)
        prog_btn = QPushButton("Send Progress")
        prog_btn.clicked.connect(self._on_progress)
        pgl.addWidget(prog_btn)
        layout.addWidget(pg)

        # Count
        cg = QGroupBox("Count (Badge)")
        cgl = QVBoxLayout(cg)
        row = QHBoxLayout()
        row.addWidget(QLabel("Count:"))
        self._count = QSpinBox()
        self._count.setRange(0, 999)
        self._count.setValue(5)
        row.addWidget(self._count)
        cgl.addLayout(row)
        self._count_visible = QCheckBox("Visible")
        self._count_visible.setChecked(True)
        cgl.addWidget(self._count_visible)
        count_btn = QPushButton("Send Count")
        count_btn.clicked.connect(self._on_count)
        cgl.addWidget(count_btn)
        layout.addWidget(cg)

        # Urgent
        self._urgent = QCheckBox("Urgent")
        layout.addWidget(self._urgent)
        urgent_btn = QPushButton("Send Urgent")
        urgent_btn.clicked.connect(self._on_urgent)
        layout.addWidget(urgent_btn)

        layout.addStretch()

    def _on_progress(self) -> None:
        if not self._bus:
            return
        send_unity_update(self._bus, self._target.get_target(), {
            "progress": dbus.Double(self._progress.value() / 100.0),
            "progress-visible": dbus.Boolean(self._prog_visible.isChecked()),
        })

    def _on_count(self) -> None:
        if not self._bus:
            return
        send_unity_update(self._bus, self._target.get_target(), {
            "count": dbus.Int64(self._count.value()),
            "count-visible": dbus.Boolean(self._count_visible.isChecked()),
        })

    def _on_urgent(self) -> None:
        if not self._bus:
            return
        send_unity_update(self._bus, self._target.get_target(), {
            "urgent": dbus.Boolean(self._urgent.isChecked()),
        })


# ---------------------------------------------------------------------------
# Tab 3: SNI
# ---------------------------------------------------------------------------


class SNITab(QWidget):
    """StatusNotifierItem full control."""

    def __init__(self) -> None:
        super().__init__()
        self._sni = None
        self._bus = None
        self._build_ui()

    def set_sni(self, sni) -> None:
        self._sni = sni

    def set_bus(self, bus) -> None:
        self._bus = bus

    def _build_ui(self) -> None:
        layout = QHBoxLayout(self)

        # Left: property controls
        left = QVBoxLayout()
        pg = QGroupBox("SNI Properties")
        pgl = QVBoxLayout(pg)

        for label, attr, default in [
            ("Id:", "_id", "krema-notification-debugger"),
            ("Title:", "_title", "Krema Debugger"),
            ("IconName:", "_icon", "dialog-information"),
            ("AttentionIcon:", "_att_icon", "dialog-warning"),
            ("OverlayIcon:", "_overlay_icon", ""),
        ]:
            row = QHBoxLayout()
            row.addWidget(QLabel(label))
            le = QLineEdit(default)
            setattr(self, attr, le)
            row.addWidget(le)
            pgl.addLayout(row)

        row = QHBoxLayout()
        row.addWidget(QLabel("Status:"))
        self._status = QComboBox()
        self._status.addItems(["Active", "Passive", "NeedsAttention"])
        row.addWidget(self._status)
        pgl.addLayout(row)

        row = QHBoxLayout()
        row.addWidget(QLabel("Category:"))
        self._category = QComboBox()
        self._category.addItems([
            "ApplicationStatus", "Communications", "SystemServices", "Hardware",
        ])
        row.addWidget(self._category)
        pgl.addLayout(row)

        # ToolTip
        pgl.addWidget(QLabel("ToolTip:"))
        row = QHBoxLayout()
        row.addWidget(QLabel("Title:"))
        self._tt_title = QLineEdit("Krema Debugger")
        row.addWidget(self._tt_title)
        row.addWidget(QLabel("Body:"))
        self._tt_body = QLineEdit()
        row.addWidget(self._tt_body)
        pgl.addLayout(row)

        left.addWidget(pg)

        # Signal buttons
        sig = QGroupBox("Send Signals")
        sigl = QVBoxLayout(sig)
        for label, handler in [
            ("NewStatus", self._on_status),
            ("NewIcon", self._on_icon),
            ("NewAttentionIcon", self._on_att_icon),
            ("NewOverlayIcon", self._on_overlay),
            ("NewToolTip", self._on_tooltip),
        ]:
            btn = QPushButton(label)
            btn.clicked.connect(handler)
            sigl.addWidget(btn)
        left.addWidget(sig)

        # Register
        row = QHBoxLayout()
        reg_btn = QPushButton("Register")
        reg_btn.clicked.connect(self._on_register)
        row.addWidget(reg_btn)
        rereg_btn = QPushButton("Re-register")
        rereg_btn.clicked.connect(self._on_register)
        row.addWidget(rereg_btn)
        left.addLayout(row)

        left.addStretch()
        layout.addLayout(left, stretch=2)

        # Right: external SNI monitor
        right = QVBoxLayout()
        right.addWidget(QLabel("External SNI Items"))
        self._sni_table = QTableWidget(0, 3)
        self._sni_table.setHorizontalHeaderLabels(
            ["Service", "Status", "Last Event"]
        )
        self._sni_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )
        right.addWidget(self._sni_table)

        refresh_btn = QPushButton("Refresh")
        refresh_btn.clicked.connect(self._refresh_list)
        right.addWidget(refresh_btn)

        layout.addLayout(right, stretch=1)

    def add_external_event(self, service: str, event: str) -> None:
        for row in range(self._sni_table.rowCount()):
            item = self._sni_table.item(row, 0)
            if item and item.text() == service:
                st = "NeedsAttention" if service in _sni_attention_apps else "Active"
                self._sni_table.setItem(row, 1, QTableWidgetItem(st))
                self._sni_table.setItem(row, 2, QTableWidgetItem(event))
                return
        row = self._sni_table.rowCount()
        self._sni_table.insertRow(row)
        self._sni_table.setItem(row, 0, QTableWidgetItem(service))
        self._sni_table.setItem(row, 1, QTableWidgetItem("Active"))
        self._sni_table.setItem(row, 2, QTableWidgetItem(event))

    def _sync_props(self) -> None:
        if not self._sni:
            return
        self._sni.set_property("Id", self._id.text().strip())
        self._sni.set_property("Category", self._category.currentText())
        self._sni.set_property("Title", self._title.text())

    def _on_status(self) -> None:
        if self._sni:
            self._sync_props()
            self._sni.set_status(self._status.currentText())

    def _on_icon(self) -> None:
        if self._sni:
            self._sni.set_icon(self._icon.text().strip())

    def _on_att_icon(self) -> None:
        if self._sni:
            self._sni.set_attention_icon(self._att_icon.text().strip())

    def _on_overlay(self) -> None:
        if self._sni:
            self._sni.set_overlay_icon(self._overlay_icon.text().strip())

    def _on_tooltip(self) -> None:
        if self._sni:
            self._sni.set_tooltip(self._tt_title.text(), self._tt_body.text())

    def _on_register(self) -> None:
        if self._sni:
            self._sync_props()
            self._sni.register_with_watcher()

    def _refresh_list(self) -> None:
        if not self._bus:
            return
        try:
            proxy = self._bus.get_object(
                "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher"
            )
            props = dbus.Interface(proxy, "org.freedesktop.DBus.Properties")
            items = props.Get(
                "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems"
            )
            self._sni_table.setRowCount(0)
            for svc in items:
                row = self._sni_table.rowCount()
                self._sni_table.insertRow(row)
                self._sni_table.setItem(row, 0, QTableWidgetItem(str(svc)))
                st = "NeedsAttention" if str(svc) in _sni_attention_apps else "Active"
                self._sni_table.setItem(row, 1, QTableWidgetItem(st))
                self._sni_table.setItem(row, 2, QTableWidgetItem("-"))
        except dbus.DBusException as e:
            _log("SNI-ERROR", f"Failed to list SNI items: {e}")


# ---------------------------------------------------------------------------
# Tab 4: Jobs
# ---------------------------------------------------------------------------


class JobsTab(QWidget):
    """KDE Jobs via kuiserver."""

    def __init__(self, target_bar: TargetBar) -> None:
        super().__init__()
        self._target = target_bar
        self._bus = None
        self._job_path = None
        self._job_iface = None
        self._build_ui()

    def set_bus(self, bus) -> None:
        self._bus = bus

    def _build_ui(self) -> None:
        layout = QVBoxLayout(self)

        create_btn = QPushButton("Create Job (requestView)")
        create_btn.setStyleSheet("font-weight:bold; padding:8px;")
        create_btn.clicked.connect(self._on_create)
        layout.addWidget(create_btn)

        self._job_status = QLabel("No active job")
        self._job_status.setStyleSheet("color:#888;")
        layout.addWidget(self._job_status)

        cg = QGroupBox("Job Controls")
        cl = QVBoxLayout(cg)

        # Percent
        row = QHBoxLayout()
        row.addWidget(QLabel("Percent:"))
        self._percent = QSlider(Qt.Orientation.Horizontal)
        self._percent.setRange(0, 100)
        row.addWidget(self._percent)
        self._pct_label = QLabel("0%")
        self._percent.valueChanged.connect(
            lambda v: self._pct_label.setText(f"{v}%")
        )
        row.addWidget(self._pct_label)
        pct_btn = QPushButton("Set")
        pct_btn.clicked.connect(self._on_percent)
        row.addWidget(pct_btn)
        cl.addLayout(row)

        # Info message
        row = QHBoxLayout()
        row.addWidget(QLabel("Info:"))
        self._info_msg = QLineEdit("Copying files...")
        row.addWidget(self._info_msg)
        btn = QPushButton("Set")
        btn.clicked.connect(self._on_info)
        row.addWidget(btn)
        cl.addLayout(row)

        # Speed
        row = QHBoxLayout()
        row.addWidget(QLabel("Speed (B/s):"))
        self._speed = QSpinBox()
        self._speed.setRange(0, 999999999)
        self._speed.setValue(1048576)
        row.addWidget(self._speed)
        btn = QPushButton("Set")
        btn.clicked.connect(self._on_speed)
        row.addWidget(btn)
        cl.addLayout(row)

        # Total / Processed
        row = QHBoxLayout()
        row.addWidget(QLabel("Total:"))
        self._total = QSpinBox()
        self._total.setRange(0, 999999999)
        self._total.setValue(104857600)
        row.addWidget(self._total)
        btn = QPushButton("Set")
        btn.clicked.connect(self._on_total)
        row.addWidget(btn)
        cl.addLayout(row)

        row = QHBoxLayout()
        row.addWidget(QLabel("Processed:"))
        self._processed = QSpinBox()
        self._processed.setRange(0, 999999999)
        row.addWidget(self._processed)
        btn = QPushButton("Set")
        btn.clicked.connect(self._on_processed)
        row.addWidget(btn)
        cl.addLayout(row)

        # Description field
        row = QHBoxLayout()
        row.addWidget(QLabel("Field#:"))
        self._desc_num = QSpinBox()
        self._desc_num.setRange(0, 10)
        self._desc_num.setMaximumWidth(50)
        row.addWidget(self._desc_num)
        row.addWidget(QLabel("Name:"))
        self._desc_name = QLineEdit("From")
        self._desc_name.setMaximumWidth(80)
        row.addWidget(self._desc_name)
        row.addWidget(QLabel("Value:"))
        self._desc_value = QLineEdit("/home/user/file.txt")
        row.addWidget(self._desc_value)
        btn = QPushButton("Set")
        btn.clicked.connect(self._on_desc)
        row.addWidget(btn)
        cl.addLayout(row)

        # Dest URL
        row = QHBoxLayout()
        row.addWidget(QLabel("Dest URL:"))
        self._dest_url = QLineEdit()
        self._dest_url.setPlaceholderText("file:///home/user/dest/")
        row.addWidget(self._dest_url)
        btn = QPushButton("Set")
        btn.clicked.connect(self._on_dest)
        row.addWidget(btn)
        cl.addLayout(row)

        # Suspended
        self._suspended = QCheckBox("Suspended")
        self._suspended.clicked.connect(self._on_suspended)
        cl.addWidget(self._suspended)

        # Terminate
        term_btn = QPushButton("Terminate Job")
        term_btn.setStyleSheet("color: red;")
        term_btn.clicked.connect(self._on_terminate)
        cl.addWidget(term_btn)

        layout.addWidget(cg)

        self._signal_log = QLabel("Signals: (none)")
        self._signal_log.setWordWrap(True)
        self._signal_log.setStyleSheet("color:#888;")
        layout.addWidget(self._signal_log)
        layout.addStretch()

    def _on_create(self) -> None:
        if not self._bus:
            _log("JOB-ERROR", "D-Bus not connected")
            return
        path, iface = create_job(self._bus, self._target.get_target())
        if path:
            self._job_path = path
            self._job_iface = iface
            self._job_status.setText(f"Active job: {path}")
            self._job_status.setStyleSheet("color: green;")
            for sig in ("cancelRequested", "suspendRequested", "resumeRequested"):
                self._bus.add_signal_receiver(
                    lambda s=sig: self._on_signal(s),
                    signal_name=sig,
                    dbus_interface="org.kde.JobViewV2",
                    path=path,
                )

    def _on_signal(self, name: str) -> None:
        _log("JOB-SIGNAL", f"{name} on {self._job_path}")
        _record_event(
            "observed", f"jobs.{name}", correlation=f"job:{self._job_path}",
            event_id=self._job_path,
        )
        self._signal_log.setText(f"Last signal: {name}")

    def _call(self, method: str, *args) -> bool:
        if not self._job_iface:
            _log("JOB-ERROR", "No active job")
            return False
        try:
            getattr(self._job_iface, method)(*args)
            _record_event(
                "sent", f"jobs.{method}", correlation=f"job:{self._job_path}",
                event_id=self._job_path,
                payload={"args": [str(a) for a in args]},
            )
            _log("JOB", f"{method}({', '.join(str(a) for a in args)})")
            return True
        except dbus.DBusException as e:
            _log("JOB-ERROR", f"{method}: {e}")
            return False

    def _on_percent(self):
        self._call("setPercent", dbus.UInt32(self._percent.value()))

    def _on_info(self):
        self._call("setInfoMessage", self._info_msg.text())

    def _on_speed(self):
        self._call("setSpeed", dbus.UInt64(self._speed.value()))

    def _on_total(self):
        self._call("setTotalAmount", dbus.UInt64(self._total.value()), "bytes")

    def _on_processed(self):
        self._call("setProcessedAmount", dbus.UInt64(self._processed.value()), "bytes")

    def _on_desc(self):
        self._call("setDescriptionField", dbus.UInt32(self._desc_num.value()),
                    self._desc_name.text(), self._desc_value.text())

    def _on_dest(self):
        self._call("setDestUrl", self._dest_url.text())

    def _on_suspended(self):
        self._call("setSuspended", dbus.Boolean(self._suspended.isChecked()))

    def _on_terminate(self):
        job_path = self._job_path
        if self._call("terminate", "User requested"):
            if job_path:
                _tracked_jobs.pop(str(job_path), None)
            self._job_status.setText("Job terminated")
            self._job_status.setStyleSheet("color: red;")
            self._job_path = None
            self._job_iface = None


# ---------------------------------------------------------------------------
# Tab 5: Inbox
# ---------------------------------------------------------------------------


class InboxTab(QWidget):
    """Slack-style inbox simulation."""

    def __init__(self, target_bar: TargetBar, inbox: InboxModel) -> None:
        super().__init__()
        self._target = target_bar
        self._inbox = inbox
        self._inbox._ui_callback = self._refresh
        self._build_ui()

    def _build_ui(self) -> None:
        layout = QHBoxLayout(self)

        # Left: channel list
        left = QVBoxLayout()
        left.addWidget(QLabel("Channels"))
        self._channel_list = QListWidget()
        self._channel_list.currentRowChanged.connect(self._on_channel_select)
        left.addWidget(self._channel_list)

        row = QHBoxLayout()
        self._new_channel = QLineEdit()
        self._new_channel.setPlaceholderText("Channel name")
        row.addWidget(self._new_channel)
        add_btn = QPushButton("+")
        add_btn.setMaximumWidth(30)
        add_btn.clicked.connect(self._on_add)
        row.addWidget(add_btn)
        del_btn = QPushButton("-")
        del_btn.setMaximumWidth(30)
        del_btn.clicked.connect(self._on_del)
        row.addWidget(del_btn)
        left.addLayout(row)
        layout.addLayout(left, stretch=1)

        # Right: messages
        right = QVBoxLayout()
        self._ch_title = QLabel("Select a channel")
        self._ch_title.setStyleSheet("font-weight:bold; font-size:14px;")
        right.addWidget(self._ch_title)

        self._msg_list = QListWidget()
        right.addWidget(self._msg_list)

        row = QHBoxLayout()
        self._msg_input = QLineEdit()
        self._msg_input.setPlaceholderText("Type a message...")
        self._msg_input.returnPressed.connect(self._on_send)
        row.addWidget(self._msg_input)
        send_btn = QPushButton("Send")
        send_btn.clicked.connect(self._on_send)
        row.addWidget(send_btn)
        right.addLayout(row)

        row = QHBoxLayout()
        read_btn = QPushButton("Mark Selected Read")
        read_btn.clicked.connect(self._on_mark_read)
        row.addWidget(read_btn)
        read_all_btn = QPushButton("Mark All Read")
        read_all_btn.clicked.connect(self._on_mark_all)
        row.addWidget(read_all_btn)
        right.addLayout(row)

        self._badge_label = QLabel("Total unread: 0")
        self._badge_label.setStyleSheet("color:#888;")
        right.addWidget(self._badge_label)

        layout.addLayout(right, stretch=2)

    def _current_channel(self):
        idx = self._channel_list.currentRow()
        if 0 <= idx < len(self._inbox.channels):
            return self._inbox.channels[idx]
        return None

    def _on_add(self) -> None:
        name = self._new_channel.text().strip()
        if not name:
            return
        self._inbox.add_channel(name, self._target.get_target())
        self._new_channel.clear()
        self._refresh()

    def _on_del(self) -> None:
        ch = self._current_channel()
        if ch:
            self._inbox.remove_channel(ch)
            self._refresh()

    def _on_channel_select(self, row: int) -> None:
        self._refresh_messages()

    def _on_send(self) -> None:
        ch = self._current_channel()
        if not ch:
            return
        text = self._msg_input.text().strip()
        if not text:
            return
        self._inbox.send_message(ch, text)
        self._msg_input.clear()
        self._refresh_messages()

    def _on_mark_read(self) -> None:
        ch = self._current_channel()
        if not ch:
            return
        row = self._msg_list.currentRow()
        if 0 <= row < len(ch.messages):
            self._inbox.mark_read(ch, ch.messages[row])
            self._refresh_messages()

    def _on_mark_all(self) -> None:
        ch = self._current_channel()
        if ch:
            self._inbox.mark_channel_read(ch)
            self._refresh_messages()

    def _refresh(self) -> None:
        current = self._channel_list.currentRow()
        self._channel_list.clear()
        for ch in self._inbox.channels:
            u = ch.unread_count
            label = f"#{ch.name} ({ch.desktop_entry})"
            if u > 0:
                label += f"  [{u} unread]"
            item = QListWidgetItem(label)
            if u > 0:
                item.setForeground(QColor("#1565C0"))
            self._channel_list.addItem(item)
        if 0 <= current < self._channel_list.count():
            self._channel_list.setCurrentRow(current)
        self._refresh_messages()
        self._badge_label.setText(f"Total unread: {self._inbox.total_unread}")

    def _refresh_messages(self) -> None:
        ch = self._current_channel()
        self._msg_list.clear()
        if not ch:
            self._ch_title.setText("Select a channel")
            return
        self._ch_title.setText(f"#{ch.name} ({ch.desktop_entry})")
        for m in ch.messages:
            status = "read" if m.read else "UNREAD"
            sid = m.server_id if m.server_id else "?"
            label = f"[{status}] (id={sid}) {m.text}"
            item = QListWidgetItem(label)
            if not m.read:
                font = item.font()
                font.setBold(True)
                item.setFont(font)
            self._msg_list.addItem(item)


# ---------------------------------------------------------------------------
# Tab 6: DND
# ---------------------------------------------------------------------------


class DNDTab(QWidget):
    """Do Not Disturb (Inhibit/UnInhibit)."""

    def __init__(self) -> None:
        super().__init__()
        self._bus = None
        self._cookies: list[tuple[int, str, str]] = []
        self._build_ui()

    def set_bus(self, bus) -> None:
        self._bus = bus

    def _build_ui(self) -> None:
        layout = QVBoxLayout(self)

        ig = QGroupBox("Inhibit")
        igl = QVBoxLayout(ig)
        row = QHBoxLayout()
        row.addWidget(QLabel("App:"))
        self._app = QLineEdit("krema-debugger")
        row.addWidget(self._app)
        igl.addLayout(row)
        row = QHBoxLayout()
        row.addWidget(QLabel("Reason:"))
        self._reason = QLineEdit("Testing DND")
        row.addWidget(self._reason)
        igl.addLayout(row)
        inhibit_btn = QPushButton("Inhibit")
        inhibit_btn.setStyleSheet("font-weight:bold; padding:6px;")
        inhibit_btn.clicked.connect(self._on_inhibit)
        igl.addWidget(inhibit_btn)
        layout.addWidget(ig)

        layout.addWidget(QLabel("Active Inhibitions:"))
        self._cookie_list = QListWidget()
        layout.addWidget(self._cookie_list)

        row = QHBoxLayout()
        uninhibit_btn = QPushButton("UnInhibit Selected")
        uninhibit_btn.clicked.connect(self._on_uninhibit)
        row.addWidget(uninhibit_btn)
        uninhibit_all_btn = QPushButton("UnInhibit All")
        uninhibit_all_btn.clicked.connect(self._on_uninhibit_all)
        row.addWidget(uninhibit_all_btn)
        layout.addLayout(row)

        row = QHBoxLayout()
        read_btn = QPushButton("Read Inhibited")
        read_btn.clicked.connect(self._on_read)
        row.addWidget(read_btn)
        self._inhibited_label = QLabel("?")
        row.addWidget(self._inhibited_label)
        layout.addLayout(row)

        layout.addStretch()

    def _on_inhibit(self) -> None:
        if not self._bus:
            _log("DND-ERROR", "D-Bus not connected")
            return
        app = self._app.text().strip()
        reason = self._reason.text().strip()
        cookie = inhibit_dnd(self._bus, app, reason)
        if cookie is not None:
            self._cookies.append((cookie, app, reason))
            self._refresh()

    def _on_uninhibit(self) -> None:
        if not self._bus:
            return
        row = self._cookie_list.currentRow()
        if 0 <= row < len(self._cookies):
            cookie, _, _ = self._cookies.pop(row)
            uninhibit_dnd(self._bus, cookie)
            self._refresh()

    def _on_uninhibit_all(self) -> None:
        if not self._bus:
            return
        for cookie, _, _ in self._cookies:
            uninhibit_dnd(self._bus, cookie)
        self._cookies.clear()
        self._refresh()

    def _on_read(self) -> None:
        if not self._bus:
            return
        try:
            proxy = self._bus.get_object(
                "org.freedesktop.Notifications", "/org/freedesktop/Notifications"
            )
            props = dbus.Interface(proxy, "org.freedesktop.DBus.Properties")
            inhibited = props.Get("org.freedesktop.Notifications", "Inhibited")
            self._inhibited_label.setText(f"Inhibited = {bool(inhibited)}")
            _log("DND", f"Inhibited = {bool(inhibited)}")
            _record_event(
                "observed", "freedesktop.Notifications.Inhibited",
                correlation="dnd:inhibited",
                payload={"inhibited": bool(inhibited)},
            )
        except dbus.DBusException as e:
            _log("DND-ERROR", f"Read Inhibited: {e}")
            self._inhibited_label.setText("Error")

    def _refresh(self) -> None:
        self._cookie_list.clear()
        for cookie, app, reason in self._cookies:
            self._cookie_list.addItem(
                f"cookie={cookie}  app={app}  reason={reason}"
            )


# ---------------------------------------------------------------------------
# Main window
# ---------------------------------------------------------------------------


class DebuggerWindow(QWidget):
    def __init__(self) -> None:
        super().__init__()
        self.setObjectName("notificationDebuggerWindow")
        self.setWindowTitle("Krema Notification Debugger")
        self.resize(1024, 700)

        self._bus = None
        self._sni = None
        self._watcher = None
        self._inbox_model = InboxModel()
        self._reset_state = "ready"
        self._build_ui()

    def reset_all(self) -> None:
        self._reset_state = "resetting"
        self.update_status()
        reset_failed = False

        if not self._bus:
            _log("RESET", "Reset All skipped: D-Bus not connected")
            for job_path, job_iface in list(_tracked_jobs.items()):
                try:
                    job_iface.terminate("Reset All")
                    _tracked_jobs.pop(job_path, None)
                    _record_event(
                        "reset", "jobs.terminate",
                        correlation=f"job:{job_path}",
                        event_id=job_path, reason="completed",
                    )
                except dbus.DBusException as exc:
                    reset_failed = True
                    _log("JOB-ERROR", f"Reset terminate: {exc}")
                    _record_event(
                        "reset", "jobs.terminate",
                        correlation=f"job:{job_path}",
                        event_id=job_path, reason=str(exc),
                    )
            _tracked_dnd_cookies.clear()
            _tracked_portal_ids.clear()
            _tracked_unity_entries.clear()
            self._jobs_tab._job_path = None
            self._jobs_tab._job_iface = None
            self._jobs_tab._job_status.setText("No active job")
            self._jobs_tab._job_status.setStyleSheet("color:#888;")
            self._dnd_tab._cookies.clear()
            self._dnd_tab._refresh()
            self._inbox_model.channels.clear()
            self._inbox_tab._refresh()
            _record_event("reset", "reset-all", reason="D-Bus not connected")
            self._reset_state = "reset-incomplete"
            self.update_status()
            return

        _log("RESET", "Reset All: notifications")
        notification_ids = list(_tracked_notif_ids)
        for nid in notification_ids:
            success = close_notification(self._bus, nid)
            reset_failed |= not success
            if success:
                entry = _notif_id_to_entry.pop(int(nid), None)
                if entry is not None:
                    ids = _unread.get(entry)
                    if ids is not None:
                        ids.discard(int(nid))
                        if not ids:
                            del _unread[entry]
            _record_event(
                "reset", "reset.notifications",
                correlation=f"notification:{nid}", event_id=nid,
                reason="completed" if success else "unavailable",
            )
        if not notification_ids:
            _record_event("reset", "reset.notifications", reason="none tracked")

        _log("RESET", "Reset All: portal notifications")
        portal_ids = list(_tracked_portal_ids)
        for notification_id in portal_ids:
            success = remove_portal(self._bus, notification_id)
            reset_failed |= not success
            _record_event(
                "reset", "reset.portal",
                correlation=f"portal:{notification_id}",
                event_id=notification_id,
                reason="completed" if success else "unavailable",
            )
        if not portal_ids:
            _record_event("reset", "reset.portal", reason="none tracked")

        _log("RESET", "Reset All: Unity zero state")
        unity_entries = list(_tracked_unity_entries)
        for desktop_entry in unity_entries:
            success = send_unity_update(self._bus, desktop_entry, {
                "count": dbus.Int64(0), "count-visible": dbus.Boolean(False),
                "progress": dbus.Double(0.0), "progress-visible": dbus.Boolean(False),
                "urgent": dbus.Boolean(False),
            })
            reset_failed |= not success
            if success:
                _tracked_unity_entries.discard(desktop_entry)
            _record_event(
                "reset", "reset.unity",
                correlation=f"unity:{desktop_entry}", event_id=desktop_entry,
                reason="completed" if success else "unavailable",
            )
        if not unity_entries:
            _record_event("reset", "reset.unity", reason="none tracked")

        _log("RESET", "Reset All: SNI Active then unregister")
        if self._sni:
            self._sni.set_status("Active")
            self._sni.set_overlay_icon("")
            self._sni.set_attention_icon("dialog-warning")
            sni_success = self._sni.unregister_with_watcher()
            reset_failed |= not sni_success
            if sni_success:
                _sni_attention_apps.clear()
            _record_event(
                "reset", "reset.sni",
                correlation=f"sni:{self._sni._service_name}",
                event_id=str(self._sni._props["Id"]),
                reason="completed" if sni_success else "unavailable",
            )
        else:
            reset_failed = True
            _record_event("reset", "reset.sni", reason="unavailable")

        _log("RESET", "Reset All: terminate jobs")
        tracked_jobs = list(_tracked_jobs.items())
        for job_path, job_iface in tracked_jobs:
            try:
                job_iface.terminate("Reset All")
                _tracked_jobs.pop(job_path, None)
                _record_event(
                    "reset", "jobs.terminate",
                    correlation=f"job:{job_path}",
                    event_id=job_path, reason="completed",
                )
            except dbus.DBusException as exc:
                reset_failed = True
                _log("JOB-ERROR", f"Reset terminate: {exc}")
                _record_event(
                    "reset", "jobs.terminate",
                    correlation=f"job:{job_path}",
                    event_id=job_path, reason=str(exc),
                )
        if not tracked_jobs:
            _record_event("reset", "jobs.terminate", reason="none active")
        self._jobs_tab._job_path = None
        self._jobs_tab._job_iface = None
        self._jobs_tab._job_status.setText("No active job")
        self._jobs_tab._job_status.setStyleSheet("color:#888;")

        _log("RESET", "Reset All: release DND cookies")
        dnd_cookies = list(_tracked_dnd_cookies)
        for cookie in dnd_cookies:
            success = uninhibit_dnd(self._bus, cookie)
            reset_failed |= not success
            _record_event(
                "reset", "reset.dnd",
                correlation=f"dnd:{cookie}", event_id=cookie,
                reason="completed" if success else "unavailable",
            )
        if not dnd_cookies:
            _record_event("reset", "reset.dnd", reason="none tracked")
        self._dnd_tab._cookies = [
            item for item in self._dnd_tab._cookies if item[0] in _tracked_dnd_cookies
        ]
        self._dnd_tab._refresh()

        _log("RESET", "Reset All: clear inbox simulation")
        self._inbox_model.channels.clear()
        self._inbox_tab._refresh()
        _record_event(
            "reset", "reset.inbox", correlation="inbox:reset", reason="completed"
        )
        _record_event(
            "reset", "reset-all",
            reason="incomplete" if reset_failed else "completed",
        )
        self._reset_state = (
            "reset-incomplete"
            if reset_failed or (_recorder and _recorder.error)
            else "reset-complete"
        )
        self.update_status()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)

        self._target_bar = TargetBar()
        self._target_bar.setObjectName("targetBar")
        root.addWidget(self._target_bar)

        main = QHBoxLayout()

        self._tabs = QTabWidget()
        self._notif_tab = NotificationsTab(self._target_bar)
        self._notif_tab.setObjectName("notificationsTab")
        self._tabs.addTab(self._notif_tab, "Notifications")
        self._portal_tab = PortalTab(self._target_bar)
        self._portal_tab.setObjectName("portalTab")
        self._tabs.addTab(self._portal_tab, "Portal")
        self._smart_tab = SmartLauncherTab(self._target_bar)
        self._smart_tab.setObjectName("smartLauncherTab")
        self._tabs.addTab(self._smart_tab, "SmartLauncher")
        self._sni_tab = SNITab()
        self._sni_tab.setObjectName("sniTab")
        self._tabs.addTab(self._sni_tab, "SNI")
        self._jobs_tab = JobsTab(self._target_bar)
        self._jobs_tab.setObjectName("jobsTab")
        self._tabs.addTab(self._jobs_tab, "Jobs")
        self._inbox_tab = InboxTab(self._target_bar, self._inbox_model)
        self._inbox_tab.setObjectName("inboxTab")
        self._tabs.addTab(self._inbox_tab, "Inbox")
        self._dnd_tab = DNDTab()
        self._dnd_tab.setObjectName("dndTab")
        self._tabs.addTab(self._dnd_tab, "DND")

        main.addWidget(self._tabs, stretch=2)

        event_views = QVBoxLayout()
        event_views.addWidget(QLabel("Sent Events"))
        self._sent_table = QTableWidget(0, 4)
        self._sent_table.setObjectName("sentTable")
        self._sent_table.setHorizontalHeaderLabels(
            ["Operation", "Correlation", "ID", "Reason"]
        )
        self._sent_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )
        event_views.addWidget(self._sent_table)

        event_views.addWidget(QLabel("Observed Events"))
        self._observed_table = QTableWidget(0, 4)
        self._observed_table.setObjectName("observedTable")
        self._observed_table.setHorizontalHeaderLabels(
            ["Operation", "Correlation", "ID", "Reason"]
        )
        self._observed_table.horizontalHeader().setSectionResizeMode(
            QHeaderView.ResizeMode.Stretch
        )
        event_views.addWidget(self._observed_table)

        self._log_panel = LogPanel()
        event_views.addWidget(QLabel("Debug Log"))
        event_views.addWidget(self._log_panel)
        main.addLayout(event_views, stretch=1)

        status_panel = QGroupBox("QA Status")
        status_panel.setObjectName("scenarioState")
        status_layout = QVBoxLayout(status_panel)
        scenario = _recorder.scenario if _recorder else "manual"
        status_panel.setTitle(f"QA Status — scenario: {scenario}")
        self._status = QLabel(f"Scenario: {scenario} | State: ready")
        self._status.setWordWrap(True)
        self._status.setStyleSheet(
            "padding:4px; background:#f5f5f5; border-radius:4px;"
        )
        status_layout.addWidget(self._status)
        self._event_counts = QLabel("Sent: 0 | Observed: 0 | Reset: 0")
        status_layout.addWidget(self._event_counts)
        self._limitation = QLabel(
            "Limitations: Portal forwarding depends on the desktop portal; "
            "Jobs are not consumed by Krema; Inbox is a simulation, not app read state."
        )
        self._limitation.setObjectName("limitationLabel")
        self._limitation.setWordWrap(True)
        status_layout.addWidget(self._limitation)
        reset_btn = QPushButton("Reset All")
        reset_btn.setObjectName("resetAllButton")
        reset_btn.clicked.connect(self.reset_all)
        status_layout.addWidget(reset_btn)
        root.addWidget(status_panel)

    def append_log(self, line: str, tag: str) -> None:
        self._log_panel.append(line, tag)
        # Update SNI table for relevant events
        if tag in ("SNI-Registered", "SNI-Unregistered", "SNI-NewStatus"):
            m = re.search(r"(?:service|sender)=(\S+)", line)
            if m:
                self._sni_tab.add_external_event(m.group(1), tag)

    def append_event(
        self, direction: str, mechanism: str, correlation, event_id, reason, payload
    ) -> None:
        if direction not in ("sent", "observed"):
            return
        table = self._sent_table if direction == "sent" else self._observed_table
        row = table.rowCount()
        table.insertRow(row)
        operation = payload.get("operation", mechanism) if isinstance(payload, dict) else mechanism
        values = (str(operation), str(correlation or ""), str(event_id or ""), str(reason or ""))
        for column, value in enumerate(values):
            table.setItem(row, column, QTableWidgetItem(value))

    def update_status(self) -> None:
        parts = []
        if _unread:
            items = [f"{e}: {len(ids)}" for e, ids in sorted(_unread.items()) if ids]
            total = sum(len(ids) for ids in _unread.values())
            parts.append(f"Unread: {', '.join(items)} (total: {total})")
        else:
            parts.append("Unread: 0")
        if _sni_attention_apps:
            parts.append(
                f"SNI Attention: {', '.join(sorted(_sni_attention_apps))}"
            )
        inbox_unread = self._inbox_model.total_unread
        if inbox_unread > 0:
            parts.append(f"Inbox: {inbox_unread} unread")
        state = self._reset_state
        if _recorder and _recorder.error:
            state = "recorder-error"
            self._limitation.setText(
                f"JSONL recorder error: {_recorder.error}. "
                "This run cannot provide machine-readable evidence."
            )
        scenario = _recorder.scenario if _recorder else "manual"
        self._status.setText(f"Scenario: {scenario} | State: {state} | " + "  |  ".join(parts))
        if _recorder:
            counts = _recorder.counts
            jsonl = (
                f"ERROR: {_recorder.error}"
                if _recorder.error else _recorder.path
            )
            self._event_counts.setText(
                f"Sent: {counts['sent']} | Observed: {counts['observed']} | "
                f"Reset: {counts['reset']} | JSONL: {jsonl}"
            )

    def set_bus(self, bus) -> None:
        self._bus = bus
        self._notif_tab.set_bus(bus)
        self._portal_tab.set_bus(bus)
        self._smart_tab.set_bus(bus)
        self._sni_tab.set_bus(bus)
        self._jobs_tab.set_bus(bus)
        self._dnd_tab.set_bus(bus)
        self._inbox_model.set_bus(bus)

    def set_sni(self, sni) -> None:
        self._sni = sni
        self._sni_tab.set_sni(sni)
        self._inbox_model.set_sni(sni)


# ---------------------------------------------------------------------------
# D-Bus setup & main
# ---------------------------------------------------------------------------


def _setup_dbus(window: DebuggerWindow) -> None:
    try:
        bus = dbus.SessionBus()
    except dbus.DBusException as e:
        _log("INIT-ERROR", f"Cannot connect to session bus: {e}")
        return

    window.set_bus(bus)

    # NotificationWatcher
    watcher = NotificationWatcherService(bus)
    window._watcher = watcher

    # Notification signals
    bus.add_signal_receiver(
        _on_notification_closed,
        signal_name="NotificationClosed",
        dbus_interface="org.freedesktop.Notifications",
        bus_name="org.freedesktop.Notifications",
        path="/org/freedesktop/Notifications",
    )
    bus.add_signal_receiver(
        _on_action_invoked,
        signal_name="ActionInvoked",
        dbus_interface="org.freedesktop.Notifications",
        bus_name="org.freedesktop.Notifications",
        path="/org/freedesktop/Notifications",
    )
    bus.add_signal_receiver(
        _on_notification_replied,
        signal_name="NotificationReplied",
        dbus_interface="org.freedesktop.Notifications",
        bus_name="org.freedesktop.Notifications",
        path="/org/freedesktop/Notifications",
    )

    # Register as watcher
    try:
        proxy = bus.get_object(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications"
        )
        iface = dbus.Interface(proxy, "org.kde.NotificationManager")
        iface.RegisterWatcher()
        _record_event(
            "sent", "kde.NotificationManager.RegisterWatcher",
            correlation="watcher:register",
        )
        _log("INIT", "RegisterWatcher() succeeded")
    except dbus.DBusException as e:
        _log("INIT-ERROR", f"RegisterWatcher: {e}")

    # SNI
    sni = StatusNotifierItemService(bus)
    window.set_sni(sni)
    sni.register_with_watcher()

    # SNI monitoring
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
    bus.add_signal_receiver(
        _on_sni_new_status,
        signal_name="NewStatus",
        dbus_interface="org.kde.StatusNotifierItem",
        sender_keyword="sender",
    )
    for signal_name in ("NewIcon", "NewOverlayIcon", "NewAttentionIcon", "NewToolTip"):
        bus.add_signal_receiver(
            lambda *args, _name=signal_name, **kwargs: _on_sni_signal(
                _name, **kwargs
            ),
            signal_name=signal_name,
            dbus_interface="org.kde.StatusNotifierItem",
            sender_keyword="sender",
        )

    # Portal ActionInvoked
    try:
        def on_portal_action(nid, action, param):
            _log(
                "PORTAL-ACTION",
                f"ActionInvoked id={nid} action={action}",
            )
            _record_event(
                "observed", "portal.ActionInvoked",
                correlation=f"portal:{nid}", event_id=str(nid),
                payload={"action": str(action), "parameter": param},
            )

        bus.add_signal_receiver(
            on_portal_action,
            signal_name="ActionInvoked",
            dbus_interface="org.freedesktop.portal.Notification",
        )
    except Exception:
        pass

    # Inbox external dismiss
    global _inbox_ref, _notif_table_callback
    _inbox_ref = window._inbox_model
    _notif_table_callback = window._notif_tab.add_received


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--jsonl", default=None, metavar="PATH",
        help="append machine-readable QA events to PATH",
    )
    parser.add_argument(
        "--scenario", default="manual", metavar="ID",
        help="scenario identifier written to JSONL records",
    )
    args = parser.parse_args()
    global _recorder
    if args.jsonl:
        path = args.jsonl
    else:
        temp_file = tempfile.NamedTemporaryFile(
            prefix="krema-notification-debugger-", suffix=".jsonl",
            delete=False,
        )
        path = temp_file.name
        temp_file.close()
    _recorder = EventRecorder(path, args.scenario)
    if _recorder.error:
        print(f"Unable to open JSONL recorder {path}: {_recorder.error}", file=sys.stderr)
        return 2
    DBusGMainLoop(set_as_default=True)

    app = QApplication(sys.argv)
    window = DebuggerWindow()

    global _log_callback, _status_callback, _event_table_callback
    _log_callback = window.append_log
    _status_callback = window.update_status
    _event_table_callback = window.append_event

    window.show()

    # GLib pump
    glib_ctx = GLib.MainContext.default()
    pump_timer = QTimer()
    pump_timer.setInterval(50)
    pump_timer.timeout.connect(lambda: glib_ctx.iteration(False))
    pump_timer.start()
    window._pump_timer = pump_timer

    QTimer.singleShot(100, lambda: _setup_dbus(window))

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
