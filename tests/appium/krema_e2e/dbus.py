# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Minimal session-bus helpers (Gio, already a dependency of the webdriver)."""

from __future__ import annotations

from typing import Any

from gi.repository import Gio, GLib

_bus: Gio.DBusConnection | None = None


def session_bus() -> Gio.DBusConnection:
    """Shared connection to the private session bus of the E2E run."""
    global _bus
    if _bus is None or _bus.is_closed():
        _bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    return _bus


def call(
    service: str,
    path: str,
    interface: str,
    method: str,
    signature: str | None = None,
    *args: Any,
    timeout_ms: int = 10000,
) -> Any:
    """Call a D-Bus method and return its unpacked result.

    ``signature`` is the GVariant tuple signature of ``args`` without the
    parentheses (e.g. ``"s"`` or ``"ss"``); pass None for no arguments.
    Returns None for no reply values, the single value for one, else a tuple.
    """
    params = GLib.Variant(f"({signature})", args) if signature else None
    reply = session_bus().call_sync(
        service, path, interface, method, params, None, Gio.DBusCallFlags.NONE, timeout_ms, None
    )
    if reply is None:
        return None
    values = reply.unpack()
    if len(values) == 0:
        return None
    if len(values) == 1:
        return values[0]
    return values


def get_property(service: str, path: str, interface: str, name: str) -> Any:
    """Read one D-Bus property."""
    return call(service, path, "org.freedesktop.DBus.Properties", "Get", "ss", interface, name)


def list_names() -> list[str]:
    """Names currently owned on the session bus."""
    return list(call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames"))


def has_name(name: str) -> bool:
    """Whether ``name`` is owned on the session bus."""
    return bool(call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", "s", name))


def name_pid(name: str) -> int:
    """PID of the process owning ``name``."""
    return int(
        call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID", "s", name)
    )
