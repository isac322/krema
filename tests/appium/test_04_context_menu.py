# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""E2E automation of tests/e2e/scenarios/04-context-menu.md (CTX-001..CTX-006).

The dock item context menu is a native QMenu. Qt keeps ``Qt::Popup`` windows
out of the AT-SPI tree (README, investigation 3), so:

* the menu itself is observed through KWin (a new, non-normal krema window at
  the click point) and a screenshot of that window;
* entries are activated with the real keyboard (Down x position, Return) in
  the order given by ``krema_e2e.krema.context_menu_entries``, which mirrors
  ``src/models/dockcontextmenu.cpp``.

Entry labels cannot be read, so the menu *order* is verified by effect: every
enabled position is activated by some test and each one has a distinct,
asserted outcome::

    0  Pin to Dock / Unpin from Dock   CTX-002 / CTX-003 (kremarc PinnedLaunchers)
    1  New Instance                    CTX-004 (KWin window count +1)
    2  Close                           CTX-005 (KWin windows of the app gone)
    3  Settings...                     CTX-006 (Settings window, Icons page)
    4  About Krema                     CTX-001 (Settings window, About Krema page)
    5  Quit                            CTX-001 (krema exits with status 0)

Reordering, inserting or removing an entry in the app (or the disabled app-name
header becoming focusable) shifts at least one position and makes the
corresponding test observe the wrong effect.
"""

from __future__ import annotations

import os
import signal
from xml.etree import ElementTree

from PIL import Image, ImageChops

from krema_e2e import config, dbus, env, kwin
from krema_e2e import input as inp
from krema_e2e.krema import PAGE_ROLE, SETTINGS_PAGES_XPATH, SETTINGS_XPATH, DescriptionChanges, Krema, Rect, context_menu_entries, has_state
from krema_e2e.waits import wait_stable, wait_until
from krema_e2e.windows import TestWindows

import pytest

APP = env.TEST_APP_ID
APP_NAME = env.TEST_APP_NAME
LAUNCHER = config.launcher(APP)

#: Menu of an unpinned / pinned running single window and of a pinned launcher.
UNPINNED_WINDOW = context_menu_entries(pinned=False, is_window=True)
PINNED_WINDOW = context_menu_entries(pinned=True, is_window=True)
PINNED_LAUNCHER = context_menu_entries(pinned=True, is_window=False)


# ------------------------------------------------------------------- oracles
def app_windows(app_id: str = APP) -> list[kwin.Window]:
    return [w for w in kwin.app_windows() if w.app_id == app_id]


def pinned_launchers(krema: Krema) -> list[str]:
    return config.as_list(krema.read_config().get("General", {}).get("PinnedLaunchers", ""))


def item_descriptions(krema: Krema) -> dict[str, str]:
    """Accessible description ("Pinned, Active, 2 windows, Starting"...) of
    every dock item by name, read from the AT-SPI tree."""
    root = ElementTree.fromstring(krema.page_source())
    bar = next(e for e in root.iter("tool_bar") if e.get("name") == "Krema Dock")
    return {b.get("name", ""): b.get("description", "") for b in bar.findall("button")}


def description(krema: Krema, name: str) -> str:
    return item_descriptions(krema).get(name, "")


def settings_window(krema: Krema) -> kwin.Window | None:
    return next((w for w in krema.windows() if w.normal_window and not w.skip_taskbar and w.title.startswith("Settings")), None)


def work_area() -> Rect:
    """KWin's placement area of the active output: the screen minus exclusive zones."""
    a = kwin.evaluate(
        "const a = workspace.clientArea(KWin.PlacementArea, workspace.activeScreen, workspace.currentDesktop);"
        "report([a.x, a.y, a.width, a.height]);"
    )
    return Rect(*(round(v) for v in a))


def capture(krema: Krema, name: str, area: Rect | None = None) -> Image.Image:
    """Screen image; with ``area`` only that part is captured (see kwin.screenshot)."""
    assert kwin.can_capture(), f"KWin compositing is {kwin.compositing_type()}: pixel oracle needs a DRM render node"
    return krema.screenshot(name, area)


def settled_capture(krema: Krema, name: str, area: Rect) -> Image.Image:
    """``capture`` once ``area`` stops changing. A new item's AT-SPI rect
    and the panel width reach their final values while its icon is still
    sliding into the slot, so a single frame can show the slot empty."""
    return wait_stable(lambda: capture(krema, name, area), duration=0.6, interval=0.2)


def crop(image: Image.Image, r: Rect | tuple[int, int, int, int]) -> Image.Image:
    x, y, w, h = r
    return image.crop((x, y, x + w, y + h))


def indicator_contrast(image: Image.Image, item: Rect) -> int:
    """Brightest deviation from the median in the indicator strip of a
    bottom-edge dock item: the rows below the icon (item height = icon size +
    indicator space, width = icon size), centre +-12 px, where the running
    dots are drawn."""
    icon = item.width
    cx = item.x + item.width // 2
    strip = image.crop((cx - 12, item.y + icon, cx + 12, item.y + item.height))
    lum = sorted(sum(p) for p in strip.getdata())
    median = lum[len(lum) // 2]
    return max(abs(v - median) for v in lum)


def kill_pid(pid: int) -> None:
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    wait_until(lambda: not any(w.pid == pid for w in kwin.windows()), timeout=10, message=f"pid {pid} windows to close")


# ------------------------------------------------------------------ CTX-001
def test_ctx001_right_click_opens_native_menu_at_the_item(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    krema.wait_for_item("Alpha")
    click = krema.item_center("Alpha")

    menu = krema.open_context_menu("Alpha")

    # A krema-owned popup (QMenu: not a normal window, not in the taskbar list)
    # anchored at the right-click point, fully on screen. KWin keeps xdg popups
    # inside the work area (Workspace::clientArea(PlacementArea)), which
    # excludes the dock's own exclusive zone, so for a click on the reserved
    # panel strip the menu slides to just above the panel (as Plasma panel
    # menus do): the anchor is the click point clamped into the work area.
    assert menu.pid == krema.pid
    assert not menu.normal_window
    area = Rect(*menu.client_geometry)
    grown = Rect(area.x - 2, area.y - 2, area.width + 4, area.height + 4)
    work = work_area()
    anchor = (click[0], min(click[1], work.y + work.height - 1))
    assert grown.contains(*anchor), f"menu {area} not at click point {click} (work area {work})"
    assert area.x >= 0 and area.y >= 0
    assert area.x + area.width <= env.SCREEN_WIDTH and area.y + area.height <= env.SCREEN_HEIGHT

    # Rendered with content (text/separators), not an empty surface.
    shot = crop(capture(krema, "menu-open", area), area)
    colors = shot.getcolors(maxcolors=1 << 16)
    assert colors is None or len(colors) > 16, f"menu region looks blank: {colors}"

    # The menu owns the keyboard: the first Down highlights the first enabled
    # entry ("Pin to Dock"), a row below the disabled app-name header, in the
    # upper part of the menu.
    inp.key("Down")
    changed = wait_until(
        lambda: ImageChops.difference(shot, crop(capture(krema, "menu-down", area), area)).getbbox(),
        timeout=5,
        message="keyboard highlight in the menu",
    )
    top, bottom = changed[1], changed[3]
    assert top >= area.height * 0.08, f"highlight {changed} is on the header row (menu {area})"
    assert bottom <= area.height * 0.34, f"highlight {changed} is not the first entry row (menu {area})"

    # Escape dismisses it.
    inp.key("Escape")
    wait_until(lambda: all(w.internal_id != menu.internal_id for w in kwin.windows()), timeout=5, message="menu to close")
    assert krema.item("Alpha") is not None


def test_ctx001_about_krema_is_the_fifth_entry(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    krema.open_context_menu("Alpha")
    krema.choose_context_menu_entry("About Krema", UNPINNED_WINDOW)

    win = wait_until(lambda: settings_window(krema), timeout=15, message="Settings window from About Krema")
    assert win.pid == krema.pid
    page = krema.wait_for(SETTINGS_XPATH + f"//{PAGE_ROLE}[@name='About Krema']", timeout=15)
    assert has_state(page, "showing")
    assert krema.find(SETTINGS_XPATH + f"//{PAGE_ROLE}[@name='Icons']") is None
    checked = [e.get_attribute("name") for e in krema.find_all(SETTINGS_PAGES_XPATH) if has_state(e, "checked")]
    assert checked == ["About Krema"], f"sidebar selection {checked}"


def test_ctx001_quit_is_the_last_entry(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    process = krema.process
    assert process is not None
    krema.open_context_menu("Alpha")
    krema.choose_context_menu_entry("Quit", UNPINNED_WINDOW)

    wait_until(lambda: process.poll() is not None, timeout=15, message="krema to quit")
    assert process.returncode == 0, f"krema exited with {process.returncode}; see {krema.log_path}"
    wait_until(lambda: not dbus.has_name("org.kde.krema"), timeout=10, message="org.kde.krema to be released")
    wait_until(lambda: not any(w.pid == process.pid for w in kwin.windows()), timeout=10, message="krema surfaces to go")
    # The fixture window is untouched.
    assert [w.title for w in app_windows()] == ["Alpha"]
    krema.stop()  # expected exit: detach the fixture from the finished process


# ------------------------------------------------------------------ CTX-002
def test_ctx002_pin_keeps_the_app_in_the_dock_after_it_closes(krema: Krema, apps: TestWindows) -> None:
    alpha = apps.open("Alpha")
    krema.wait_for_item("Alpha")
    assert LAUNCHER not in pinned_launchers(krema)
    krema.move_away()
    shot = settled_capture(krema, "running", krema.surface_rect("dock"))
    running_strip = indicator_contrast(shot, wait_stable(lambda: krema.screen_rect(krema.item("Alpha"))))

    krema.open_context_menu("Alpha")
    krema.choose_context_menu_entry("Pin to Dock", UNPINNED_WINDOW)
    wait_until(lambda: pinned_launchers(krema) == [LAUNCHER], timeout=5, message="launcher saved to kremarc")

    apps.close(alpha)
    assert app_windows() == []
    krema.wait_for_item(APP_NAME)
    assert wait_stable(krema.item_names, duration=1.0) == [APP_NAME]
    assert "Pinned" in description(krema, APP_NAME)

    # No running indicator: the dot drawn under the icon while running is gone.
    krema.move_away()
    rect = wait_stable(lambda: krema.screen_rect(krema.item(APP_NAME)))
    closed_strip = indicator_contrast(settled_capture(krema, "closed", rect), rect)
    assert running_strip >= 120, f"oracle cannot see the running dot (contrast {running_strip})"
    assert closed_strip <= 40, f"indicator strip still shows a dot (contrast {closed_strip})"

    # Persisted: a fresh krema shows the pinned launcher again.
    krema.restart()
    krema.wait_for_item(APP_NAME)
    assert pinned_launchers(krema) == [LAUNCHER]
    assert "Pinned" in description(krema, APP_NAME)


# ------------------------------------------------------------------ CTX-003
@pytest.mark.kremarc({"PinnedLaunchers": [LAUNCHER]})
def test_ctx003_unpin_removes_a_closed_app(krema: Krema, apps: TestWindows) -> None:
    krema.wait_for_item(APP_NAME)
    assert app_windows() == []

    krema.open_context_menu(APP_NAME)
    krema.choose_context_menu_entry("Unpin from Dock", PINNED_LAUNCHER)

    krema.wait_for_no_item(APP_NAME, timeout=5)
    wait_until(lambda: pinned_launchers(krema) == [], timeout=5, message="launcher removed from kremarc")
    assert krema.item_names() == []


@pytest.mark.kremarc({"PinnedLaunchers": [LAUNCHER]})
def test_ctx003_unpinned_running_app_stays_until_it_closes(krema: Krema, apps: TestWindows) -> None:
    alpha = apps.open("Alpha")
    krema.wait_for_item("Alpha")
    assert "Pinned" in description(krema, "Alpha")

    krema.open_context_menu("Alpha")
    krema.choose_context_menu_entry("Unpin from Dock", PINNED_WINDOW)
    wait_until(lambda: pinned_launchers(krema) == [], timeout=5, message="launcher removed from kremarc")
    assert wait_stable(krema.item_names, duration=1.0) == ["Alpha"]

    apps.close(alpha)
    krema.wait_for_no_item("Alpha", timeout=5)
    assert wait_stable(krema.item_names, duration=1.0) == []


# ------------------------------------------------------------------ CTX-004
def test_ctx004_new_instance_launches_another_window(krema: Krema, apps: TestWindows) -> None:
    alpha = apps.open("Alpha")
    krema.wait_for_item("Alpha")
    before = app_windows()
    assert [w.pid for w in before] == [alpha.pid]

    # Launch feedback (bounce): the item reports "Starting" while launching.
    # That lasts only until the new window maps, for the fixture often less
    # than one page_source() poll, so the description changes are recorded
    # as AT-SPI events from before the launch instead of polled.
    changes = DescriptionChanges()
    spawned: list[int] = []
    try:
        krema.open_context_menu("Alpha")
        krema.choose_context_menu_entry("New Instance", UNPINNED_WINDOW)
        after = wait_until(lambda: len(app_windows()) == 2 and app_windows(), timeout=15, message="a second window")
        spawned = [w.pid for w in after if w.pid != alpha.pid]
        assert len(spawned) == 1
        new = next(w for w in after if w.pid == spawned[0])
        assert new.app_id == APP and new.title == APP_NAME
        wait_until(
            lambda: any("Starting" in d for d in changes.texts()),
            timeout=5,
            message=lambda: f"dock item to report the launch (bounce) (descriptions seen: {changes.texts()})",
        )
        # The two windows are grouped under the app's .desktop name.
        krema.wait_for_item(APP_NAME)
        wait_until(lambda: "2 windows" in description(krema, APP_NAME), timeout=5, message="grouped item")
        wait_until(lambda: "Starting" not in description(krema, APP_NAME), timeout=10, message="launch feedback to end")
    finally:
        changes.close()
        for pid in spawned or [w.pid for w in app_windows() if w.pid != alpha.pid]:
            kill_pid(pid)


# ------------------------------------------------------------------ CTX-005
def test_ctx005_close_closes_every_window_of_an_unpinned_app(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    apps.open("Beta")
    krema.wait_for_item(APP_NAME)
    assert sorted(w.title for w in app_windows()) == ["Alpha", "Beta"]

    krema.open_context_menu(APP_NAME)
    krema.choose_context_menu_entry("Close", UNPINNED_WINDOW)

    wait_until(lambda: app_windows() == [], timeout=10, message="all app windows to close")
    krema.wait_for_no_item(APP_NAME, timeout=5)
    assert krema.item_names() == []


@pytest.mark.kremarc({"PinnedLaunchers": [LAUNCHER]})
def test_ctx005_close_keeps_a_pinned_app_without_indicator(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    apps.open("Beta")
    krema.wait_for_item(APP_NAME)
    wait_until(lambda: "2 windows" in description(krema, APP_NAME), timeout=5, message="grouped item")

    krema.open_context_menu(APP_NAME)
    krema.choose_context_menu_entry("Close", PINNED_WINDOW)

    wait_until(lambda: app_windows() == [], timeout=10, message="all app windows to close")
    assert wait_stable(krema.item_names, duration=1.0) == [APP_NAME]
    wait_until(lambda: "windows" not in description(krema, APP_NAME), timeout=5, message="window count to clear")
    krema.move_away()
    rect = wait_stable(lambda: krema.screen_rect(krema.item(APP_NAME)))
    strip = indicator_contrast(settled_capture(krema, "closed", rect), rect)
    assert strip <= 40, f"indicator strip still shows a dot (contrast {strip})"
    assert pinned_launchers(krema) == [LAUNCHER]


# ------------------------------------------------------------------ CTX-006
def test_ctx006_settings_entry_opens_the_settings_window(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    krema.wait_for_item("Alpha")
    before = [w for w in krema.windows() if w.normal_window and not w.skip_taskbar]
    assert before == []

    win = krema.open_settings("Alpha", UNPINNED_WINDOW)

    after = [w for w in krema.windows() if w.normal_window and not w.skip_taskbar]
    assert [w.internal_id for w in after] == [win.internal_id]
    assert win.pid == krema.pid and win.title == "Settings — Krema"

    # AT-SPI: the settings frame at the KWin window's position.
    frame = krema.wait_for(SETTINGS_XPATH, timeout=10)
    assert has_state(frame, "showing")
    assert krema.surface_rect("settings") == Rect(*win.client_geometry)
    pages = [e.get_attribute("name") for e in krema.find_all(SETTINGS_PAGES_XPATH)]
    for page in ("Icons", "Panel Style", "Behavior", "Monitors & Desktops", "Window Preview", "About Krema", "About KDE"):
        assert page in pages, f"{page!r} missing from settings pages {pages}"
    # The Icons page is shown by default and selected in the sidebar.
    krema.wait_for(SETTINGS_XPATH + f"//{PAGE_ROLE}[@name='Icons']")
    checked = [e.get_attribute("name") for e in krema.find_all(SETTINGS_PAGES_XPATH) if has_state(e, "checked")]
    assert checked == ["Icons"], f"sidebar selection {checked}"
    assert krema.find(SETTINGS_XPATH + "//slider[@name='Icon size']") is not None
    slider = krema.find(SETTINGS_XPATH + "//slider[@name='Zoom factor']")
    assert slider is not None and has_state(slider, "focusable")

    shot = crop(capture(krema, "settings"), Rect(*win.client_geometry))
    colors = shot.getcolors(maxcolors=1 << 16)
    assert colors is None or len(colors) > 16, "settings window looks blank"
