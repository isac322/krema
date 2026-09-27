# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""KGlobalAccel (org.kde.kglobalaccel, hosted by the test KWin).

Krema registers component ``krema`` with actions ``toggle-dock``,
``focus-dock``, ``activate-entry-1..9`` and ``new-instance-entry-1..9``
(src/app/application.cpp).

Note: in a stock KWin, Meta+F5 is also KWin's "Move Mouse to Focus"
(component ``kwin``, action ``MoveMouseToFocus``) and KWin wins, so a real
Meta+F5 key press never reaches krema unless that binding is cleared with
:func:`set_shortcut_keys` (see README, investigation 2).
"""

from __future__ import annotations

from . import dbus
from .waits import wait_until

_SERVICE = "org.kde.kglobalaccel"
_COMPONENT_IFACE = "org.kde.kglobalaccel.Component"


def shortcut_names(component: str = "krema") -> list[str]:
    """Action names registered by ``component``."""
    return list(dbus.call(_SERVICE, f"/component/{component}", _COMPONENT_IFACE, "shortcutNames"))


def shortcut_keys(action: str, component: str = "krema") -> list[int]:
    """Active key codes (Qt ``QKeyCombination::toCombined()`` ints) bound to an
    action, e.g. ``[Qt.META | Qt.Key_F5] == [0x10000000 | 0x01000034]``."""
    for info in dbus.call(_SERVICE, f"/component/{component}", _COMPONENT_IFACE, "allShortcutInfos"):
        if info[0] == action:
            return list(info[6])
    raise KeyError(f"{component}/{action} is not registered")


def set_shortcut_keys(action: str, keys: list[int], component: str = "krema") -> None:
    """Rebind (``[]`` = unbind) a global shortcut of any component, the way
    System Settings does."""
    for info in dbus.call(_SERVICE, f"/component/{component}", _COMPONENT_IFACE, "allShortcutInfos"):
        if info[0] == action:
            action_id = [info[2], info[0], info[3], info[1]]
            dbus.call(_SERVICE, "/kglobalaccel", "org.kde.KGlobalAccel", "setForeignShortcut", "asai", action_id, keys)
            return
    raise KeyError(f"{component}/{action} is not registered")


def invoke_shortcut(name: str, component: str = "krema", timeout: float = 10.0) -> None:
    """Trigger a KGlobalAccel action (``focus-dock``, ``toggle-dock``,
    ``activate-entry-1``...) via ``invokeShortcut`` exactly as if its key was
    pressed. Waits until the component has registered the action."""
    wait_until(
        lambda: name in shortcut_names(component),
        timeout=timeout,
        message=f"kglobalaccel {component}/{name} to be registered",
    )
    dbus.call(_SERVICE, f"/component/{component}", _COMPONENT_IFACE, "invokeShortcut", "s", name)
