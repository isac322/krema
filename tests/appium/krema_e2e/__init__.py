# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Krema E2E helpers. tests/appium/README.md documents the API contract.

Modules: env, waits, dbus, config, input, kwin, shortcuts, windows, krema,
pytest_plugin. The most used names are re-exported here.
"""

from . import config, dbus, env, input, kwin, shortcuts
from .krema import Krema, Rect, context_menu_entries, has_state
from .shortcuts import invoke_shortcut
from .waits import WaitTimeout, wait_stable, wait_until
from .windows import TestWindow, TestWindows

__all__ = [
    "Krema",
    "Rect",
    "TestWindow",
    "TestWindows",
    "WaitTimeout",
    "config",
    "context_menu_entries",
    "dbus",
    "env",
    "has_state",
    "input",
    "invoke_shortcut",
    "kwin",
    "shortcuts",
    "wait_stable",
    "wait_until",
]
