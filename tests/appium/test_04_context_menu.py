# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""E2E automation of tests/e2e/scenarios/04-context-menu.md (CTX-001..CTX-006, CTX-008, CTX-009).

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

The empty-area dock menu has Settings..., About Krema, and Quit Krema.
Only its final entry exits the dock (CTX-010).

An app whose .desktop file declares actions lists them first, in Actions=
order, so every position above moves down by their count (CTX-008: the
launched window's title tells which action ran).

Reordering, inserting or removing an entry in the app (or the disabled app-name
header becoming focusable) shifts at least one position and makes the
corresponding test observe the wrong effect.
"""

from __future__ import annotations

import os
import re
import signal
import time
from xml.etree import ElementTree

from PIL import Image, ImageChops

from krema_e2e import config, dbus, env, kwin
from krema_e2e import input as inp
from krema_e2e.krema import DOCK_CONTEXT_MENU_ENTRIES, PAGE_ROLE, SETTINGS_PAGES_XPATH, SETTINGS_XPATH, DescriptionChanges, Krema, Rect, context_menu_entries, has_state, painted_rect
from krema_e2e.waits import KWIN_POLL_INTERVAL, wait_stable, wait_until
from krema_e2e.shortcuts import invoke_shortcut
from krema_e2e.windows import TestWindows
from test_02_mouse import ZOOM_KREMARC, _require_capture

import pytest

APP = env.TEST_APP_ID
APP_NAME = env.TEST_APP_NAME
LAUNCHER = config.launcher(APP)

#: Menu of an unpinned / pinned running single window and of a pinned launcher.
UNPINNED_WINDOW = context_menu_entries(pinned=False, is_window=True)
PINNED_WINDOW = context_menu_entries(pinned=True, is_window=True)
PINNED_LAUNCHER = context_menu_entries(pinned=True, is_window=False)


def _padding_cases():
    for edge in (config.EDGE_TOP, config.EDGE_BOTTOM, config.EDGE_LEFT, config.EDGE_RIGHT):
        for style in (0, 1):
            for spacing in (0, 4, 64):
                yield pytest.param(edge, style, spacing, marks=pytest.mark.kremarc({
                    "PinnedLaunchers": [config.launcher(APP), config.launcher(env.TEST_APP2_ID)],
                    "VisibilityMode": config.ALWAYS_VISIBLE, "Edge": edge,
                    "ZoomStyle": style, "IconSpacing": spacing, "MaxZoomFactor": 1.6,
                    "PreviewEnabled": False,
                }), id=f"edge-{edge}-zoom-{style}-spacing-{spacing}")


@pytest.mark.parametrize("edge,style,spacing", list(_padding_cases()))
def test_ctx012_padding_after_zoom_opens_dock_settings(krema: Krema, edge: int, style: int, spacing: int) -> None:
    def two_items():
        items = krema.items()
        return items if len(items) == 2 else None

    items = wait_until(two_items, message="two pinned launchers")
    rest = wait_stable(lambda: krema.screen_rect(items[0]), duration=0.2)
    inp.move_path(inp.line(kwin.cursor_pos(), rest.center, 12), 20)
    wait_until(lambda: krema.screen_rect(items[0]) != rest, message="first launcher to magnify")
    zoomed = wait_stable(lambda: krema.screen_rect(items[0]), duration=0.2)
    assert zoomed != rest, "hover must be fully established before testing the padding route"
    before = pinned_launchers(krema)
    krema.open_dock_context_menu()
    krema.choose_context_menu_entry("Settings...", DOCK_CONTEXT_MENU_ENTRIES)
    wait_until(lambda: settings_window(krema), timeout=15, message="Settings from padding after zoom")
    assert pinned_launchers(krema) == before, "padding opened the app menu and changed pinning"


@pytest.mark.parametrize("with_app", [False, True], ids=["empty", "populated"])
def test_ctx011_keyboard_opens_dock_settings(krema: Krema, apps: TestWindows, with_app: bool) -> None:
    if with_app:
        apps.open("Alpha")
        krema.wait_for_item("Alpha")
    invoke_shortcut("focus-dock")
    krema.wait_keyboard_focus()
    before = {w.internal_id for w in krema.windows()}
    inp.key("Shift", "F10")
    wait_until(
        lambda: any(w.internal_id not in before and not w.normal_window and not w.special_window and not w.skip_taskbar for w in krema.windows()),
        message="keyboard-opened dock menu",
    )
    krema.choose_context_menu_entry("Settings...", DOCK_CONTEXT_MENU_ENTRIES)
    wait_until(lambda: settings_window(krema), timeout=15, message="Settings from keyboard dock menu")


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
    assert bottom <= area.height * 0.60, f"highlight {changed} is not the first entry row (menu {area})"

    # Escape dismisses it.
    inp.key("Escape")
    wait_until(lambda: all(w.internal_id != menu.internal_id for w in kwin.windows()), timeout=5, message="menu to close")
    assert krema.item("Alpha") is not None


def test_ctx010_about_krema_is_the_second_dock_entry(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    krema.open_dock_context_menu()
    krema.choose_context_menu_entry("About Krema", DOCK_CONTEXT_MENU_ENTRIES)

    win = wait_until(lambda: settings_window(krema), timeout=15, message="Settings window from About Krema")
    assert win.pid == krema.pid
    page = krema.wait_for(SETTINGS_XPATH + f"//{PAGE_ROLE}[@name='About Krema']", timeout=15)
    assert has_state(page, "showing")
    assert krema.find(SETTINGS_XPATH + f"//{PAGE_ROLE}[@name='Icons']") is None
    checked = [e.get_attribute("name") for e in krema.find_all(SETTINGS_PAGES_XPATH) if has_state(e, "checked")]
    assert checked == ["About Krema"], f"sidebar selection {checked}"


@pytest.mark.parametrize("with_app", [False, True], ids=["empty-dock", "running-app"])
def test_ctx010_quit_krema_is_in_the_dock_menu(krema: Krema, apps: TestWindows, with_app: bool) -> None:
    if with_app:
        apps.open("Alpha")
    process = krema.process
    assert process is not None
    krema.open_dock_context_menu()
    krema.choose_context_menu_entry("Quit Krema", DOCK_CONTEXT_MENU_ENTRIES)

    wait_until(lambda: process.poll() is not None, timeout=15, message="krema to quit")
    assert process.returncode == 0, f"krema exited with {process.returncode}; see {krema.log_path}"
    wait_until(lambda: not dbus.has_name("org.kde.krema"), timeout=10, message="org.kde.krema to be released")
    wait_until(lambda: not any(w.pid == process.pid for w in kwin.windows()), timeout=10, message="krema surfaces to go")
    # The fixture window is untouched.
    assert [w.title for w in app_windows()] == (["Alpha"] if with_app else [])
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

    krema.open_dock_context_menu()
    krema.choose_context_menu_entry("Settings...", DOCK_CONTEXT_MENU_ENTRIES)
    win = wait_until(lambda: settings_window(krema), timeout=15, message="Settings from the pointer dock menu")

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


# ------------------------------------------------------------------ CTX-008
#: An app whose .desktop file declares actions, installed in krema's private
#: XDG_DATA_HOME before krema starts. The Actions= order differs from the
#: group order, and a hidden (NoDisplay) action plus a separator sit between
#: the visible ones: showing the hidden action or following the group order
#: moves "Open Inbox" to another keyboard position, which launches a window
#: with another title. The fixture app of the other CTX tests declares no
#: actions, so they cover the unchanged menu.
ACTIONS_ID = "org.krema.ctxactions"
ACTIONS_NAME = "Krema Actions Test"
ACTIONS = ["New Draft", "Open Inbox"]


def _install_actions_app(krema: Krema) -> None:
    apps_dir = krema.home / "data" / "applications"
    apps_dir.mkdir(parents=True, exist_ok=True)
    exe = f"{env.TEST_WINDOW_BINARY} --app-id {ACTIONS_ID}"
    (apps_dir / f"{ACTIONS_ID}.desktop").write_text(
        "[Desktop Entry]\n"
        "Type=Application\n"
        f"Name={ACTIONS_NAME}\n"
        f"Exec={exe}\n"
        "Icon=utilities-terminal\n"
        f"StartupWMClass={ACTIONS_ID}\n"
        "Actions=new-draft;hidden;_SEPARATOR_;open-inbox;\n"
        "\n"
        "[Desktop Action open-inbox]\n"
        "Name=Open Inbox\n"
        f"Exec={exe} --title Inbox\n"
        "\n"
        "[Desktop Action hidden]\n"
        "Name=Hidden Action\n"
        f"Exec={exe} --title Hidden\n"
        "NoDisplay=true\n"
        "\n"
        "[Desktop Action new-draft]\n"
        "Name=New Draft\n"
        "Icon=document-new\n"
        f"Exec={exe} --title Draft\n",
        encoding="utf-8",
    )


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher(ACTIONS_ID)]})
def test_ctx008_pinned_launcher_launches_the_chosen_desktop_action(krema: Krema, apps: TestWindows) -> None:
    _install_actions_app(krema)
    krema.start()
    krema.wait_for_item(ACTIONS_NAME)
    assert app_windows(ACTIONS_ID) == []

    spawned: list[int] = []
    try:
        krema.open_context_menu(ACTIONS_NAME)
        krema.choose_context_menu_entry("Open Inbox", context_menu_entries(pinned=True, is_window=False, desktop_actions=ACTIONS))
        win = wait_until(lambda: next(iter(app_windows(ACTIONS_ID)), None), timeout=15, message="window of the chosen desktop action")
        spawned.append(win.pid)
        assert win.title == "Inbox", f"launched {win.title!r} instead of the Open Inbox action"
        assert [w.title for w in wait_stable(lambda: app_windows(ACTIONS_ID), duration=1.0)] == ["Inbox"]

        # The existing entries follow the actions: Unpin still unpins.
        krema.wait_for_item("Inbox")
        krema.open_context_menu("Inbox")
        krema.choose_context_menu_entry("Unpin from Dock", context_menu_entries(pinned=True, is_window=True, desktop_actions=ACTIONS))
        wait_until(lambda: pinned_launchers(krema) == [], timeout=5, message="launcher removed from kremarc")
        assert wait_stable(krema.item_names, duration=1.0) == ["Inbox"]
    finally:
        for pid in spawned or [w.pid for w in app_windows(ACTIONS_ID)]:
            kill_pid(pid)


@pytest.mark.no_krema_autostart
def test_ctx008_grouped_windows_launch_the_chosen_desktop_action(krema: Krema, apps: TestWindows) -> None:
    _install_actions_app(krema)
    krema.start()
    apps.open("One", app_id=ACTIONS_ID)
    apps.open("Two", app_id=ACTIONS_ID)
    krema.wait_for_item(ACTIONS_NAME)
    wait_until(lambda: "2 windows" in description(krema, ACTIONS_NAME), timeout=5, message="grouped item")
    known = {w.pid for w in app_windows(ACTIONS_ID)}

    spawned: list[int] = []
    try:
        krema.open_context_menu(ACTIONS_NAME)
        krema.choose_context_menu_entry("New Draft", context_menu_entries(pinned=False, is_window=True, desktop_actions=ACTIONS))
        new = wait_until(
            lambda: [w for w in app_windows(ACTIONS_ID) if w.pid not in known] or None, timeout=15, message="window of the chosen desktop action"
        )
        spawned = [w.pid for w in new]
        assert [w.title for w in new] == ["Draft"], f"launched {[w.title for w in new]} instead of the New Draft action"
        wait_until(lambda: "3 windows" in description(krema, ACTIONS_NAME), timeout=5, message="action window joins the group")
    finally:
        for pid in spawned or [w.pid for w in app_windows(ACTIONS_ID) if w.pid not in known]:
            kill_pid(pid)


# ------------------------------------------------------------------ CTX-009
#: A window of a pinned app takes the launcher's slot and its title as the
#: item name, and as a task it opens a window preview on hover. The target is
#: the harness-installed second test app (2nd of the ZOOM_KREMARC launchers):
#: its .desktop file resolves on every distro, unlike some stock launchers
#: (org.kde.kfind is missing on fedora-rawhide), so the window never becomes
#: an extra unpinned item.
HOLD_TARGET = "Gamma"
HOLD_TARGET_APP = env.TEST_APP2_ID
HOLD_TARGET_LAUNCHER = env.TEST_APP2_NAME
HOLD_HOVER_DELAY_MS = 300
HOLD_KREMARC = {**ZOOM_KREMARC, "PreviewEnabled": True, "PreviewHoverDelay": HOLD_HOVER_DELAY_MS}
#: How long the hold is sampled while the menu is open.
HOLD_SAMPLE_SECONDS = 1.5


def hold_painted(krema: Krema, rest: dict[str, Rect], name: str = HOLD_TARGET) -> Rect:
    """Where item ``name`` is drawn (surface-local), given the rest rects."""
    return painted_rect(Rect.of(krema.wait_for_item(name)), rest[name])


def hold_painted_sizes(krema: Krema, rest: dict[str, Rect]) -> dict[str, tuple[int, int]]:
    """Painted sizes by current item name; ``rest`` must follow group renames.

    Sizes rather than rects are compared because task changes can re-centre
    the row horizontally. A missing expected item still fails the comparison.
    """
    sizes = {}
    for e in krema.items():
        name = e.get_attribute("name")
        if name in rest:
            r = painted_rect(Rect.of(e), rest[name])
            sizes[name] = (r.width, r.height)
    return sizes


def hover_zoomed_with_preview(krema: Krema, apps: TestWindows) -> tuple[dict[str, Rect], int, Rect, Rect]:
    """Open a window of a pinned app and hover its item until the window
    preview is open. Returns the rest rects by item name, the base item
    width, the target's zoomed painted rect (surface-local) and the dock
    surface's screen rect."""
    _require_capture()
    krema.wait_for_item(HOLD_TARGET_LAUNCHER, timeout=15)
    before = wait_stable(krema.item_names, duration=0.6)
    expected = [HOLD_TARGET if n == HOLD_TARGET_LAUNCHER else n for n in before]
    apps.open(HOLD_TARGET, app_id=HOLD_TARGET_APP, width=320, height=220)
    wait_until(
        lambda: krema.item_names() == expected,
        timeout=15,
        message=lambda: f"{HOLD_TARGET!r} window to take the {HOLD_TARGET_LAUNCHER!r} launcher slot: {expected} (have {krema.item_names()})",
    )

    krema.move_away(close_preview=False)
    rest = wait_stable(lambda: {e.get_attribute("name"): Rect.of(e) for e in krema.items()})
    assert sorted(rest) == sorted(expected), f"dock items changed while settling: {sorted(rest)} vs {sorted(expected)}"
    widths = {r.width for r in rest.values()}
    assert len(widths) == 1, f"all items at base size before hover: {rest}"
    (base,) = widths
    dock = wait_until(lambda: krema.surface_rect("dock"), message="dock surface geometry")

    krema.hover_item(HOLD_TARGET)
    wait_until(krema.preview_visible, timeout=8, message=f"preview of {HOLD_TARGET!r} (a window item) to open on hover")
    zoomed = wait_stable(lambda: hold_painted(krema, rest))
    assert zoomed.width >= base * 1.5, f"hovered item zoomed to {zoomed.width}px, base {base}px"
    return rest, base, zoomed, dock


def right_click_in_place(krema: Krema) -> kwin.Window:
    """Right-click at the current pointer position (no motion) and wait for
    the QMenu popup window."""
    before = {w.internal_id for w in krema.windows()}
    inp.click(button="right")
    return wait_until(
        lambda: next((w for w in krema.windows() if w.internal_id not in before and not w.normal_window), None),
        timeout=5,
        interval=KWIN_POLL_INTERVAL,
        message=f"context menu of {HOLD_TARGET!r}",
    )


def wait_menu_closed(menu: kwin.Window) -> None:
    wait_until(
        lambda: all(w.internal_id != menu.internal_id for w in kwin.windows()),
        timeout=5,
        interval=KWIN_POLL_INTERVAL,
        message="context menu to close",
    )


@pytest.mark.kremarc(HOLD_KREMARC)
def test_ctx009_zoom_and_hover_hold_while_menu_is_open(krema: Krema, apps: TestWindows) -> None:
    try:
        rest, _, zoomed, _ = hover_zoomed_with_preview(krema, apps)

        menu = right_click_in_place(krema)

        # The preview closes as soon as the menu opens, so it cannot cover it.
        wait_until(lambda: not krema.preview_visible(), timeout=1.0, message="preview to close when the context menu opens")

        # While the menu is open the hovered item keeps its zoom and no
        # preview comes back, although the pointer never moved.
        samples: list[tuple[int, bool]] = []
        deadline = time.monotonic() + HOLD_SAMPLE_SECONDS
        while time.monotonic() < deadline:
            samples.append((hold_painted(krema, rest).width, krema.preview_visible()))
            time.sleep(0.05)
        assert len(samples) >= 3, f"too few samples while the menu was open: {samples}"
        widths = [w for w, _ in samples]
        assert all(abs(w - zoomed.width) <= 2 for w in widths), f"zoom not held while the menu was open: {zoomed.width}px -> {widths}"
        assert not any(visible for _, visible in samples), f"preview reappeared while the menu was open: {samples}"
        assert any(w.internal_id == menu.internal_id for w in kwin.windows()), "context menu closed during the sampling window"

        # Escape closes the menu with the pointer still on the item: zoom
        # stays and the preview comes back after the normal hover delay.
        inp.key("Escape")
        wait_menu_closed(menu)
        held = wait_stable(lambda: hold_painted(krema, rest))
        assert abs(held.width - zoomed.width) <= 2, f"zoom released after the menu closed over the item: {zoomed.width}px -> {held.width}px"
        wait_until(
            krema.preview_visible,
            timeout=HOLD_HOVER_DELAY_MS / 1000 + 3,
            message="preview to reopen after the menu closed with the pointer on the item",
        )
        assert abs(hold_painted(krema, rest).width - zoomed.width) <= 2, "zoom released once the preview reopened"
    finally:
        # Never leave the pointer resting on the dock, even after a failure.
        krema.move_away(close_preview=False)


@pytest.mark.kremarc(HOLD_KREMARC)
def test_ctx009_zoom_releases_when_menu_closes_away_from_the_dock(krema: Krema, apps: TestWindows) -> None:
    known = {w.pid for w in app_windows(HOLD_TARGET_APP)}
    try:
        rest, base, zoomed, dock = hover_zoomed_with_preview(krema, apps)
        before = app_windows(HOLD_TARGET_APP)
        assert len(before) == 1, f"expected one target window before New Instance: {before}"
        known.update(w.pid for w in before)

        menu = right_click_in_place(krema)
        wait_until(lambda: not krema.preview_visible(), timeout=1.0, message="preview to close when the context menu opens")

        # Locate the "New Instance" row: highlight it with the keyboard and diff
        # the menu against its unhighlighted frame (QMenu is not in AT-SPI).
        area = Rect(*menu.client_geometry)
        shot = crop(settled_capture(krema, "hold-menu-open", area), area)
        for _ in range(PINNED_WINDOW.index("New Instance") + 1):
            inp.key("Down")
        row = wait_stable(
            lambda: ImageChops.difference(shot, crop(capture(krema, "hold-menu-new-instance", area), area)).getbbox(),
            duration=0.5,
        )
        assert row is not None, "keyboard highlight of 'New Instance' not visible in the menu"
        target = (area.x + (row[0] + row[2]) // 2, area.y + (row[1] + row[3]) // 2)
        zoomed_top = dock.y + zoomed.y
        assert target[1] < zoomed_top, f"'New Instance' row at {target} is not above the magnified icon (top {zoomed_top})"

        # Click the entry with the pointer: the menu closes with the pointer on
        # the menu's former place, off the dock.
        inp.move_path(inp.line(kwin.cursor_pos(), target, 20), 15)
        inp.click()
        wait_menu_closed(menu)
        wait_until(lambda: len(app_windows(HOLD_TARGET_APP)) == 2, timeout=15, message="New Instance launched a second target-app window")
        krema.wait_for_item(HOLD_TARGET_LAUNCHER)
        wait_until(lambda: "2 windows" in description(krema, HOLD_TARGET_LAUNCHER), message="new instance joined the target group")
        wait_until(lambda: "Starting" not in description(krema, HOLD_TARGET_LAUNCHER), timeout=10, message="new-instance launch feedback to end")

        # A single window is named by its title, but two windows group under
        # the desktop launcher name. Keep the same slot's resting geometry.
        rest = {HOLD_TARGET_LAUNCHER if name == HOLD_TARGET else name: r for name, r in rest.items()}
        rest_sizes = {name: (r.width, r.height) for name, r in rest.items()}
        wait_until(
            lambda: hold_painted_sizes(krema, rest) == rest_sizes,
            timeout=5,
            message=lambda: f"icons to return to rest size (base {base}px, now {hold_painted_sizes(krema, rest)})",
        )
        assert not krema.preview_visible(), "preview reopened although the pointer left the dock"
    finally:
        # Never leave the pointer resting on the dock, even after a failure.
        krema.move_away(close_preview=False)
        # New Instance is launched by Krema, not tracked by the apps fixture.
        for pid in {w.pid for w in app_windows(HOLD_TARGET_APP)} - known:
            kill_pid(pid)


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({**ZOOM_KREMARC, "PreviewEnabled": False})
def test_ctx009_zoom_holds_on_the_first_menu_after_starting_without_a_mouse(
    krema: Krema, apps: TestWindows, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Krema starts while the seat has no pointer (the mouse is plugged in
    later). The very first menu closed with the pointer resting on the item
    must keep the zoom like every later one: the compositor focus Krema reads
    on close must already be tracked when the pointer capability appears."""
    target = env.TEST_APP2_NAME
    # Logs wl_seat.capabilities, the oracle for "started without a pointer".
    monkeypatch.setenv("WAYLAND_DEBUG", "client")
    inp.drop_pointer_capability()
    krema.start()
    log = open(krema.log_path, errors="replace").read()
    capabilities = [int(c) for c in re.findall(r"wl_seat#\d+\.capabilities\((\d+)\)", log)]
    assert capabilities and not any(c & 1 for c in capabilities), f"krema started with a pointer: capabilities {capabilities}"
    centre = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)
    # Kept for the rest of the session: it replaces the session's keep-alive.
    inp.hold_pointer_capability(centre)
    try:
        krema.wait_for_item(target, timeout=15)
        krema.move_away(close_preview=False)
        rest = wait_stable(lambda: {e.get_attribute("name"): Rect.of(e) for e in krema.items()})
        for attempt in ("first", "second"):
            krema.hover_item(target)
            zoomed = wait_stable(lambda: painted_rect(Rect.of(krema.wait_for_item(target)), rest[target]).width)
            assert zoomed >= rest[target].width * 1.4, f"{attempt} hover zoomed to {zoomed}px, base {rest[target].width}px"
            menu = right_click_in_place(krema)
            inp.key("Escape")
            wait_menu_closed(menu)
            widths = []
            deadline = time.monotonic() + 1.5
            while time.monotonic() < deadline:
                widths.append(painted_rect(Rect.of(krema.wait_for_item(target)), rest[target]).width)
                time.sleep(0.05)
            assert len(widths) >= 3, f"too few samples after the {attempt} menu closed: {widths}"
            assert all(abs(w - zoomed) <= 2 for w in widths), f"{attempt} menu closed over the item released the zoom: {zoomed}px -> {widths}"
            krema.move_away(close_preview=False)
            wait_stable(lambda: painted_rect(Rect.of(krema.wait_for_item(target)), rest[target]).width)
    finally:
        krema.move_away(close_preview=False)
