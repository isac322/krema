# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""End-to-end smoke test of the harness: every link of the chain (krema
start, AT-SPI lookup, real input, KWin oracle, kglobalaccel, screenshots) is
exercised with a real assertion."""

from __future__ import annotations

import pytest

from krema_e2e import env, kwin
from krema_e2e import input as inp
from krema_e2e.krema import Krema, Rect, painted_rect
from krema_e2e.shortcuts import invoke_shortcut
from krema_e2e.waits import wait_stable, wait_until
from krema_e2e.windows import TestWindows

# A dock item's accessible name is the window title while its task has one
# window and the app's .desktop Name once several windows are grouped.
APP1 = env.TEST_APP_NAME


def test_toolbar_is_exposed_and_dock_is_bottom_anchored(krema: Krema) -> None:
    toolbar = krema.toolbar()
    assert toolbar.get_attribute("name") == "Krema Dock"
    dock = krema.surface_rect("dock")
    assert dock is not None
    assert dock.y + dock.height == env.SCREEN_HEIGHT
    assert (dock.x, dock.width) == (0, env.SCREEN_WIDTH)


def test_open_windows_appear_as_one_grouped_dock_item(krema: Krema, apps: TestWindows) -> None:
    assert krema.item(APP1) is None
    apps.open("Alpha")
    apps.open("Beta")
    krema.wait_for_item(APP1)
    assert krema.item_names().count(APP1) == 1
    titles = {w.title for w in kwin.app_windows() if w.app_id == env.TEST_APP_ID}
    assert titles == {"Alpha", "Beta"}


def test_clicking_dock_item_activates_its_window(krema: Krema, apps: TestWindows) -> None:
    first = apps.open("First", app_id=env.TEST_APP_ID)
    second = apps.open("Second", app_id=env.TEST_APP2_ID)
    krema.wait_for_item("First")
    krema.wait_for_item("Second")
    wait_until(second.is_active, message="newest window to be active")

    krema.click_item("First")

    active = wait_until(
        lambda: (w := kwin.active_window()) is not None and w.pid == first.pid and w,
        message=lambda: f"{first.title!r} to become active (active: {kwin.active_window()})",
    )
    assert active.title == "First"


def test_hovering_an_item_zooms_it_beyond_its_neighbour(krema: Krema, apps: TestWindows) -> None:
    apps.open("One", app_id=env.TEST_APP_ID)
    apps.open("Two", app_id=env.TEST_APP2_ID)
    krema.wait_for_item("One")
    krema.wait_for_item("Two")
    krema.move_away()
    rest = wait_stable(lambda: (Rect.of(krema.item("One")), Rect.of(krema.item("Two"))), duration=0.3)
    assert rest[0].width == rest[1].width

    krema.hover_item("One")

    hovered, neighbour = wait_stable(lambda: (Rect.of(krema.item("One")), Rect.of(krema.item("Two"))), duration=0.3)
    hovered, neighbour = painted_rect(hovered, rest[0]), painted_rect(neighbour, rest[1])
    assert hovered.width > rest[0].width
    assert hovered.width > neighbour.width

    krema.move_away()
    wait_until(lambda: painted_rect(Rect.of(krema.item("One")), rest[0]).width == rest[0].width, message="zoom to reset after leaving")


def test_focus_dock_shortcut_focuses_a_dock_button(krema: Krema, apps: TestWindows) -> None:
    apps.open("Focus target")
    krema.wait_for_item("Focus target")
    assert krema.focused_item() is None

    invoke_shortcut("focus-dock")

    focused = wait_until(krema.focused_item, message="a dock button with the focused state")
    assert focused == "Focus target"
    krema.wait_keyboard_focus()
    inp.key("Escape")
    wait_until(lambda: krema.focused_item() is None, message="Escape to end keyboard navigation")


def test_screenshot_captures_the_rendered_dock(krema: Krema, apps: TestWindows) -> None:
    if not kwin.can_capture():
        pytest.skip(f"KWin compositing is {kwin.compositing_type()}: no DRM render node, ScreenShot2 needs OpenGL")
    apps.open("Shot")
    krema.wait_for_item("Shot")
    krema.move_away()
    image = krema.screenshot("dock")

    assert image.size == (env.SCREEN_WIDTH, env.SCREEN_HEIGHT)
    item = krema.screen_rect(krema.item("Shot"))
    crop = image.crop((item.x, item.y, item.x + item.width, item.y + item.height))
    colors = crop.getcolors(maxcolors=1 << 16)
    assert colors is None or len(colors) > 16, f"dock item region looks blank: {colors}"
