# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""E2E automation of tests/e2e/scenarios/06-settings.md (SET-001..SET-014).

The real Kirigami/FormCard settings window is driven with real pointer and
keyboard input (KWin fake-input) and located through AT-SPI. Every test
checks both directions: the UI change is persisted to kremarc and it takes
effect on the running dock (AT-SPI item geometry/visibility, KWin surface
geometry, rendered pixels).

AT-SPI facts this relies on (probed in this harness):

* The settings frame is ``frame[@name='Settings']``; its sidebar is a
  ``dialog`` whose ``list_item`` children are the pages.
* FormComboBoxDelegate rows are ``list_item[@name=<label>]`` whose first
  ``label`` child shows the current choice. Clicking a row opens an in-scene
  ``dialog`` (Dialog mode: named after the row; Popup mode: unnamed) whose
  options are ``list_item`` or ``menu_item`` elements, selected using
  the real pointer.
* Rows below the fold have no ``showing`` state and a 0x0 rect until the page
  is wheel-scrolled.
* The QColorDialog is a separate toplevel ``frame[@name='Choose tint color']``.

SET-008 and SET-012 need two outputs and run in their own session:
``KREMA_E2E_OUTPUT_COUNT=2 tests/appium/run-e2e.sh -m outputs``.
SET-013 also has a two-output shared-dock case; SET-012's subset test uses
``KREMA_E2E_OUTPUT_COUNT=3``.
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Callable

import pyatspi
import pytest

from krema_e2e import config as kcfg
from krema_e2e import env, kwin
from krema_e2e import input as inp
from krema_e2e import preview as pv
from krema_e2e.krema import PREVIEW_XPATH, SETTINGS_STACK_XPATH, TOOLBAR_XPATH, Krema, Rect, context_menu_entries, has_state, painted_rect
from krema_e2e.shortcuts import invoke_shortcut
from krema_e2e.waits import WaitTimeout, wait_stable, wait_until
from krema_e2e.windows import TestWindow, TestWindows

SETTINGS = "//frame[@name='Settings']"
SIDEBAR = SETTINGS + "/dialog//list_item"
SETTINGS_TITLE = "Settings — Krema"
COLOR_DIALOG = "//frame[@name='Choose tint color']"
PAGES = ["Appearance", "Behavior", "Window Preview", "About Krema", "About KDE"]
W, H = env.SCREEN_WIDTH, env.SCREEN_HEIGHT
#: Dock item context menu of an unpinned single-window task.
ENTRIES = context_menu_entries(pinned=False, is_window=True)


# --------------------------------------------------------------------- helpers
def settings_windows(krema: Krema) -> list[kwin.Window]:
    return [w for w in krema.windows() if w.normal_window and not w.skip_taskbar and w.title.startswith("Settings")]


#: Thickness of krema's edge strips (kEdgeTriggerThickness): the Follow Active
#: Mouse trigger maps one on each inactive output. They are not docks.
EDGE_TRIGGER_PX = 4


def dock_surfaces(krema: Krema) -> list[kwin.Window]:
    """Mapped dock surfaces of this krema (layer-shell, full output width,
    anchored to the bottom edge, taller than an edge strip), sorted left to right."""
    out = [
        w
        for w in krema.windows()
        if w.skip_taskbar
        and not w.desktops
        and w.client_width == W
        and w.client_y + w.client_height == H
        and w.client_height > EDGE_TRIGGER_PX
    ]
    return sorted(out, key=lambda w: w.client_x)

def _item_has_description(krema: Krema, name: str, part: str) -> bool:
    item = krema.item_accessible(name)
    if item is None:
        return False
    item.clear_cache()
    return part in (item.description or "")


def preview_surfaces(krema: Krema) -> list[kwin.Window]:
    """Pre-shown bottom-edge preview layer surfaces above the dock band."""
    return sorted(
        [
            w
            for w in krema.windows()
            if w.skip_taskbar
            and not w.desktops
            and w.client_width == W
            and w.client_y + w.client_height < H - EDGE_TRIGGER_PX
            and w.client_height > EDGE_TRIGGER_PX
        ],
        key=lambda w: w.client_x,
    )


def click_el(krema: Krema, el, surface: str = "settings", button: str = "left") -> None:
    inp.click(*krema.screen_rect(el, surface).center, button=button)


def holds(predicate: Callable[[], bool], duration: float, message: str) -> None:
    """Assert ``predicate`` stays true for ``duration`` seconds (polled)."""
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        assert predicate(), message
        time.sleep(0.05)


def page_wheel_point(krema: Krema) -> tuple[int, int]:
    """Screen centre of the current settings page's vertical scroll bar.

    Wheel events there always scroll the page: Kirigami's WheelHandler
    filters the ScrollView's scroll bars. Over the page body a wheel at rest
    goes to the control under the pointer first, and org.kde.desktop
    ComboBox/SpinBox set ``wheelEnabled: true``, so they would eat it (and
    change their value).
    """

    def bar():
        for b in krema.find_all(SETTINGS_STACK_XPATH + "//scroll_bar"):
            r = Rect.of(b)
            if has_state(b, "showing") and r.width and r.height > r.width:
                return b
        return None

    return krema.screen_rect(wait_until(bar, message="settings page vertical scroll bar"), "settings").center


def scroll_into_view(krema: Krema, xpath: str):
    """Wheel-scroll the settings page until ``xpath`` is fully visible and at rest."""
    page = krema.wait_for(SETTINGS_STACK_XPATH)
    view = Rect.of(page)

    def in_view(el, r: Rect) -> bool:
        return has_state(el, "showing") and bool(r.width) and r.y >= view.y and r.y + r.height <= view.y + view.height

    wheel_at = None
    for _ in range(60):
        el = krema.wait_for(xpath)
        # Decide on the settled position only: each wheel step animates and
        # a new step during the animation adds to its end value, so acting
        # on a mid-animation rect overshoots and can oscillate around the
        # target forever.
        r = wait_stable(lambda: Rect.of(el), duration=0.3)
        if in_view(el, r):
            return el
        below = r.width == 0 or r.y + r.height > view.y + view.height
        # Four notches while far away; one notch once the element is within
        # half a page, so a step can not jump over the visible window.
        gap = (r.y + r.height - view.y - view.height) if below else (view.y - r.y)
        notches = 1 if r.width and gap < view.height // 2 else 4
        wheel_at = wheel_at or page_wheel_point(krema)
        inp.scroll(*wheel_at, dy=15 * notches if below else -15 * notches)
    raise AssertionError(f"could not scroll {xpath} into view (last rect {r})")


def open_page(krema: Krema, name: str) -> None:
    item = krema.wait_for(f"{SIDEBAR}[@name='{name}']")
    if not has_state(item, "checked"):
        click_el(krema, item)
    wait_until(lambda: has_state(krema.find(f"{SIDEBAR}[@name='{name}']"), "checked"), message=f"page {name} selected")


def current_choice(krema: Krema, row: str) -> str:
    return krema.wait_for(f"{SETTINGS}//list_item[@name='{row}']/label").get_attribute("name")


def choose(krema: Krema, row: str, option: str) -> None:
    """Pick ``option`` in the FormComboBoxDelegate ``row`` with real clicks."""
    click_el(krema, scroll_into_view(krema, f"{SETTINGS}//list_item[@name='{row}']"))
    opt = krema.wait_for(f"{SETTINGS}/dialog//*[self::list_item or self::menu_item][@name='{option}']")
    wait_until(lambda: has_state(opt, "showing") and Rect.of(opt).width > 0, message=f"option {option} shown")
    click_el(krema, opt)
    wait_until(lambda: current_choice(krema, row) == option, message=f"{row} to show {option}")


def close_settings(krema: Krema) -> None:
    """Close the settings window with a real Alt+F4 (KWin's Window Close)."""
    (win,) = settings_windows(krema)
    kwin.activate(win.internal_id)
    wait_until(lambda: (kwin.active_window() or win).internal_id == win.internal_id and kwin.active_window() is not None)
    inp.key("alt", "f4")
    wait_until(lambda: not settings_windows(krema), message="settings window to close")


def reveal_dock(krema: Krema, name: str) -> None:
    """Show an auto-hidden dock with a real bottom-edge hover (x far from items)."""
    inp.move(60, H - 1)
    wait_until(lambda: has_state(krema.item(name), "showing"), timeout=5, message="dock to show on edge hover")
    # Wait for the slide-in to finish so item coordinates are final.
    wait_until(
        lambda: (r := Rect.of(krema.item(name))).y >= 0 and r.y + r.height <= krema.surface_rect("dock").height,
        timeout=5,
        message="dock item inside its surface",
    )
    wait_stable(lambda: Rect.of(krema.item(name)))


def pointer_to_center() -> None:
    inp.move(W // 2, H // 3)


def item_width(krema: Krema, name: str) -> int:
    return Rect.of(krema.item(name)).width


def zoomed_width(krema: Krema, name: str, rest: Rect) -> int:
    """Drawn width of item ``name`` whose unzoomed rect is ``rest``."""
    return painted_rect(Rect.of(krema.item(name)), rest).width


def hover_ready(k: Krema, name: str = "Alpha") -> None:
    """Glide onto dock item ``name`` and wait until the panel registers the
    hover (the item zooms beyond its icon size). The dock resolves clicks
    through ``root.hoveredIndex`` (main.qml onClicked), so a click that
    arrives before the hover was processed is ignored; a real user always
    hovers first."""
    icon = int(k.read_config().get("General", {}).get("IconSize", 48))
    # wait_for_item: while krema builds a Settings window (Settings... then
    # straight to another menu, SET-009) an AT-SPI query of the dock can
    # briefly return no items at all.
    item = k.screen_rect(k.wait_for_item(name))
    px, py = kwin.cursor_pos()
    if Rect(item.x - item.width, item.y - item.height, 3 * item.width, 3 * item.height).contains(px, py):
        # The pointer is still on the item (a context menu chosen with the
        # keyboard just closed), so the item is zoomed and, after
        # PreviewHoverDelay, its preview is open; while the preview lives
        # (also when the pointer rests on it, just above the dock) the item
        # stays zoomed. With Qt < 6.9 a zoomed item's rect keeps the unzoomed
        # size (painted_rect), so it cannot serve as ``rest``: move away until
        # the preview is gone first.
        k.move_away()
    rest = wait_stable(lambda: Rect.of(k.wait_for_item(name)))
    k.hover_item(name)
    wait_until(lambda: zoomed_width(k, name, rest) > icon, timeout=5, message=f"dock item {name} hovered (zoomed)")


def open_menu(k: Krema, name: str = "Alpha") -> kwin.Window:
    hover_ready(k, name)
    return k.open_context_menu(name)


def open_settings(k: Krema, name: str = "Alpha") -> kwin.Window:
    """Open Settings from ``name``'s context menu, then leave the dock like a
    user heading for the dialog.

    After the keyboard-chosen menu entry the pointer still rests on ``name``,
    whose hover preview then opens (window item); it must be gone before the
    dialog is used, or it takes the wheel and clicks aimed at the dialog."""
    hover_ready(k, name)
    win = k.open_settings(name, ENTRIES)
    k.move_away()
    return win


def config_value(krema: Krema, key: str) -> str | None:
    return krema.read_config().get("General", {}).get(key)


def atspi_actions(krema: Krema, role: str, name: str) -> list[str]:
    """AT-SPI action names of krema's first ``role`` named ``name`` (the
    WebDriver only exposes Press/Toggle/SetFocus through click())."""
    desktop = pyatspi.Registry.getDesktop(0)
    app = next(a for a in desktop if a is not None and a.get_process_id() == krema.pid)
    node = pyatspi.findDescendant(app, lambda n: n.getRoleName() == role and n.name == name)
    assert node is not None, f"{role} {name!r} not in krema's AT-SPI tree"
    action = node.queryAction()
    return [action.getName(i) for i in range(action.nActions)]


def panel_band(krema: Krema, tag: str) -> list[tuple[int, int, int]]:
    """Screenshot pixels of the dock panel's flat background left of the
    leftmost item (between the panel's rounded left edge, found on the item's
    centre row, and the icon). Only the black desktop is behind it. The left
    side is used because an open Settings window adds krema's own item on the
    right."""
    img = krema.screenshot(tag, krema.surface_rect("dock"))
    ir = min((krema.screen_rect(e) for e in krema.items()), key=lambda r: r.x)
    cy = ir.y + ir.height // 2
    x = ir.x - 1
    while x > 0 and sum(img.getpixel((x, cy))) > 30:
        x -= 1
    left, right = x + 4, ir.x - 2
    assert right - left >= 3, f"no panel padding left of {ir}: panel starts at x={x}"
    return list(img.crop((left, ir.y + 14, right, ir.y + ir.height - 14)).getdata())


def mean(px: list[tuple[int, int, int]]) -> tuple[float, float, float]:
    return tuple(sum(p[i] for p in px) / len(px) for i in range(3))  # type: ignore[return-value]


def requires_capture() -> None:
    if not kwin.can_capture():
        pytest.fail("screenshot oracle needs KWin OpenGL (/dev/dri); run in the Lima VM with vgem")


# ------------------------------------------------------------------- SET-001
@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": kcfg.AUTO_HIDE})
def test_set001_settings_opens_once_with_formcard_controls_and_keeps_dock_shown(krema: Krema, apps: TestWindows) -> None:
    alpha = apps.open("Alpha")
    reveal_dock(krema, "Alpha")
    assert settings_windows(krema) == []
    app_windows_before = [w for w in kwin.app_windows() if w.pid == krema.pid]

    win = open_settings(krema)

    # A separate toplevel window: exactly one more krema app window.
    app_windows_after = [w for w in kwin.app_windows() if w.pid == krema.pid]
    assert len(app_windows_after) == len(app_windows_before) + 1
    assert win.title == SETTINGS_TITLE and win.normal_window
    # Sidebar pages in order, Appearance shown by default.
    wait_until(lambda: [e.get_attribute("name") for e in krema.find_all(SIDEBAR)] == PAGES, message="sidebar pages")
    assert has_state(krema.find(f"{SIDEBAR}[@name='Appearance']"), "checked")
    assert krema.find(f"{SETTINGS}//heading[@name='Icons']") is not None
    # FormCard widgets exposed with labels.
    spin = krema.find(f"{SETTINGS}//list_item[label[@name='Icon size']]//spin_button")
    assert spin is not None and float(spin.get_attribute("value")) == 48.0
    # Zoom animation card: Preset tab selected with the default Natural preset.
    assert zoom_tab_selected(krema.wait_for(PRESET_TAB)) and not zoom_tab_selected(krema.wait_for(CUSTOM_TAB))
    assert has_state(krema.wait_for(preset_radio("Natural")), "checked")
    assert krema.find(f"{SETTINGS}//slider[@name='Zoom factor']") is not None
    assert krema.find(f"{SETTINGS}//list_item[@name='Attention animation']/combo_box") is not None
    assert krema.find(f"{SETTINGS}//check_box[@name='Icon size normalization']") is not None
    for role, name in (("slider", "Zoom factor"), ("spin button", "")):
        actions = atspi_actions(krema, role, name)
        assert {"Increase", "Decrease"} <= set(actions), f"{role} {name!r} actions: {actions}"
    krema.screenshot("settings-dialog")

    # The dock stays shown while the dialog is open (auto-hide would hide it).
    pointer_to_center()
    holds(lambda: has_state(krema.item("Alpha"), "showing"), 1.5, "auto-hide dock hid while Settings is open")

    # Settings... again raises the same window instead of opening a second one.
    kwin.activate(alpha.internal_id)
    wait_until(lambda: kwin.active_window() is not None and kwin.active_window().internal_id == alpha.internal_id)
    open_menu(krema)
    krema.choose_context_menu_entry("Settings...", ENTRIES)
    wait_until(
        lambda: kwin.active_window() is not None and kwin.active_window().internal_id == win.internal_id,
        message="existing Settings window to be raised and activated",
    )
    assert [w.internal_id for w in settings_windows(krema)] == [win.internal_id]
    assert has_state(krema.item("Alpha"), "showing")


# ------------------------------------------------------------------- SET-002
@pytest.mark.kremarc({"PinnedLaunchers": [], "MaxZoomFactor": 1.6})
def test_set002_icon_size_spinbox_resizes_dock_live_and_keeps_zoom_proportion(krema: Krema, apps: TestWindows) -> None:
    max_zoom = 1.6
    apps.open("Alpha")
    # apps.open waits for KWin only; the dock item follows through krema's task model.
    krema.wait_for_item("Alpha")
    pid = krema.pid

    def peak_width(rest: Rect) -> int:
        """Peak item width with the pointer on the item's centre (``rest``:
        the item's unzoomed rect).

        zoomCentre applies MaxZoomFactor only when the pointer is exactly on
        the centre; an offset scales the factor down. The item's centre moves
        as it zooms (and by more relative to its size at 48 px than at 64 px,
        which is what made the naive ratio noisy), so the pointer is
        re-centred until the rect stops moving."""

        def centre() -> tuple[int, int]:
            return krema.to_screen(painted_rect(Rect.of(krema.item("Alpha")), rest)).center

        krema.hover_item("Alpha")  # enter with a glide, not a teleport
        for _ in range(8):
            inp.move(*centre())
            if wait_stable(centre, duration=0.3) == inp.pointer_position():
                break
        return wait_stable(lambda: zoomed_width(krema, "Alpha", rest), duration=0.3)

    rest = wait_stable(lambda: Rect.of(krema.item("Alpha")), duration=0.3)
    assert rest.width == 48
    zoomed_before = peak_width(rest)
    krema.move_away()
    wait_until(lambda: zoomed_width(krema, "Alpha", rest) == 48, message=lambda: f"zoom reset (width {zoomed_width(krema, 'Alpha', rest)})")

    open_settings(krema)
    spin = krema.wait_for(f"{SETTINGS}//list_item[label[@name='Icon size']]//spin_button")
    click_el(krema, spin)
    for expected in (52, 56, 60, 64):  # stepSize 4: every key press resizes the dock
        inp.key("up")
        wait_until(lambda: float(spin.get_attribute("value")) == expected, message=f"spin box at {expected}")
        wait_until(lambda: item_width(krema, "Alpha") == expected, timeout=5, message=f"dock item {expected}px wide")

    assert config_value(krema, "IconSize") == "64"
    assert krema.pid == pid and krema.is_running(), "icon size change must not restart krema"
    zoomed_after = peak_width(wait_stable(lambda: Rect.of(krema.item("Alpha")), duration=0.3))
    # The item width is an integer, so the ratio is off by at most ~0.5/base;
    # a residual pointer offset of <=1 px lowers it by <0.03. 0.05 is safe.
    for base, zoomed in ((48, zoomed_before), (64, zoomed_after)):
        assert abs(zoomed / base - max_zoom) <= 0.05, f"{base}px icon zooms to {zoomed}px, expected ~{max_zoom}x"
    assert zoomed_after > zoomed_before


# ------------------------------------------------------------------- SET-003
def test_set003_auto_hide_applies_immediately_and_persists(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    pid = krema.pid
    open_settings(krema)
    open_page(krema, "Behavior")
    assert current_choice(krema, "Visibility mode") == "Always visible"

    choose(krema, "Visibility mode", "Auto hide")
    wait_until(lambda: config_value(krema, "VisibilityMode") == str(kcfg.AUTO_HIDE), message="kremarc VisibilityMode=1")
    # The auto-hide-only delay rows appear at once: the mode is live in the UI.
    assert krema.find(f"{SETTINGS}//list_item[label[@name='Hide delay (ms)']]") is not None

    # By design the open Settings dialog holds the dock shown (SET-008).
    pointer_to_center()
    holds(lambda: has_state(krema.item("Alpha"), "showing"), 1.0, "dock hid while Settings is open")
    close_settings(krema)

    # Without a restart the dock now hides when the pointer is away ...
    pointer_to_center()
    wait_until(lambda: not has_state(krema.item("Alpha"), "showing"), timeout=5, message="dock to auto-hide")
    assert Rect.of(krema.item("Alpha")).y >= krema.surface_rect("dock").height, "hidden item must be pushed off the surface"
    # ... and shows again on the screen edge.
    reveal_dock(krema, "Alpha")
    assert krema.pid == pid
    assert config_value(krema, "VisibilityMode") == str(kcfg.AUTO_HIDE)


# ------------------------------------------------------------------- SET-004
def test_set004_acrylic_background_applies_live(krema: Krema, apps: TestWindows) -> None:
    requires_capture()
    apps.open("Alpha")
    krema.move_away()
    before = panel_band(krema, "style-panel-inherit")
    assert len(set(before)) <= 3, f"Panel Inherit background should be flat: {sorted(set(before))[:10]}"
    assert min(sum(p) for p in before) > 300, "panel background should be opaque-looking over the black desktop"

    open_settings(krema)
    choose(krema, "Style", "Acrylic")
    wait_until(lambda: config_value(krema, "BackgroundStyle") == "3", message="kremarc BackgroundStyle=3")
    inp.move(W - 20, H // 3)

    # Acrylic = KWin blur + translucent tint + per-pixel noise grain (acrylic_overlay.frag).
    def grainy():
        px = panel_band(krema, "style-acrylic")
        return px if len(set(px)) >= 8 else None

    after = wait_until(
        grainy,
        timeout=5,
        message="acrylic noise grain on the dock panel",
    )
    m0, m1 = mean(before), mean(after)
    assert max(abs(a - b) for a, b in zip(m0, m1)) < 16, f"acrylic tint should keep the panel colour: {m0} -> {m1}"
    assert settings_windows(krema), "style change must not close Settings"


# ------------------------------------------------------------------- SET-005
def test_set005_changed_settings_persist_across_restart(krema: Krema, apps: TestWindows) -> None:
    requires_capture()
    apps.open("Alpha")
    open_settings(krema)
    spin = krema.wait_for(f"{SETTINGS}//list_item[label[@name='Icon size']]//spin_button")
    click_el(krema, spin)
    for _ in range(4):
        inp.key("up")
    wait_until(lambda: float(spin.get_attribute("value")) == 64.0)
    relaxed = scroll_into_view(krema, preset_radio("Relaxed"))
    click_el(krema, relaxed)
    wait_until(lambda: has_state(krema.wait_for(preset_radio("Relaxed")), "checked"), message="Relaxed preset selected")
    wait_until(lambda: config_value(krema, "ZoomAnimationPreset") == "2", message="zoom animation preset saved")
    choose(krema, "Style", "Acrylic")
    open_page(krema, "Behavior")
    choose(krema, "Visibility mode", "Auto hide")
    saved = {k: config_value(krema, k) for k in ("IconSize", "ZoomAnimationPreset", "BackgroundStyle", "VisibilityMode")}
    assert saved == {"IconSize": "64", "ZoomAnimationPreset": "2", "BackgroundStyle": "3", "VisibilityMode": "1"}
    close_settings(krema)

    old_pid = krema.pid
    krema.restart()
    assert krema.pid != old_pid
    assert {k: config_value(krema, k) for k in saved} == saved

    # Dock appearance matches: auto-hidden, 64 px icons, acrylic panel.
    pointer_to_center()
    wait_until(lambda: not has_state(krema.wait_for_item("Alpha"), "showing"), timeout=5, message="restored auto-hide")
    reveal_dock(krema, "Alpha")
    assert wait_stable(lambda: item_width(krema, "Alpha")) == 64
    band = panel_band(krema, "restored-acrylic")
    assert len(set(band)) >= 8, "restored panel should show the acrylic grain"

    open_settings(krema)
    spin = krema.wait_for(f"{SETTINGS}//list_item[label[@name='Icon size']]//spin_button")
    assert float(spin.get_attribute("value")) == 64.0
    assert has_state(scroll_into_view(krema, preset_radio("Relaxed")), "checked")
    assert not has_state(krema.wait_for(preset_radio("Natural")), "checked")
    assert zoom_tab_selected(krema.wait_for(PRESET_TAB))
    wait_until(lambda: current_choice(krema, "Style") == "Acrylic", message="Style shows Acrylic")
    open_page(krema, "Behavior")
    assert current_choice(krema, "Visibility mode") == "Auto hide"


# ------------------------------------------------------------------- SET-006
def test_set006_screen_edge_top_moves_dock_to_top(krema: Krema, apps: TestWindows) -> None:
    requires_capture()
    apps.open("Alpha")
    dock = krema.surface_rect("dock")
    assert (dock.y + dock.height, dock.width) == (H, W)
    open_settings(krema)
    open_page(krema, "Behavior")
    assert current_choice(krema, "Screen edge") == "Bottom"

    choose(krema, "Screen edge", "Top")
    wait_until(lambda: config_value(krema, "Edge") == str(kcfg.EDGE_TOP), message="kremarc Edge=0")
    def top_dock():
        r = krema.surface_rect("dock")
        return r if r is not None and r.y == 0 else None

    top = wait_until(
        top_dock,
        timeout=5,
        message="dock surface anchored to the top edge",
    )
    assert (top.x, top.width) == (0, W)
    assert not [w for w in krema.windows() if w.skip_taskbar and w.client_y + w.client_height == H], "a dock surface is left at the bottom"

    item = krema.item("Alpha")
    assert has_state(item, "showing")
    ir = wait_stable(lambda: krema.screen_rect(krema.item("Alpha")))
    assert 0 <= ir.y and ir.y + ir.height <= top.height and ir.width == 48
    img = krema.screenshot("dock-top", ir)
    colors = img.crop((ir.x, ir.y, ir.x + ir.width, ir.y + ir.height)).getcolors(4096)
    assert colors is None or len(colors) > 16, f"icon at the top edge looks blank: {colors}"


# ------------------------------------------------------------------- SET-007
@pytest.mark.kremarc({"PinnedLaunchers": [], "BackgroundStyle": 2})
def test_set007_custom_tint_color_is_applied_and_saved(krema: Krema, apps: TestWindows) -> None:
    requires_capture()
    apps.open("Alpha")
    krema.move_away()
    r0, g0, b0 = mean(panel_band(krema, "tint-system"))
    assert max(r0, g0, b0) - min(r0, g0, b0) < 20, f"system-colour tint should be neutral: {(r0, g0, b0)}"

    open_settings(krema)
    assert current_choice(krema, "Style") == "Tinted"
    use_system = scroll_into_view(krema, f"{SETTINGS}//check_box[@name='Use system color']")
    assert has_state(use_system, "checked")
    click_el(krema, use_system)
    wait_until(lambda: not has_state(use_system, "checked"), message="Use system color off")

    tint = scroll_into_view(krema, f"{SETTINGS}//button[starts-with(@name, 'Tint color')]")
    click_el(krema, tint)
    dialog = wait_until(
        lambda: next((w for w in krema.windows() if w.title.startswith("Choose tint color")), None), message="colour dialog window"
    )
    hex_field = krema.wait_for(f"{COLOR_DIALOG}//combo_box[@name='Hex']/following-sibling::text")

    def dialog_click(el) -> None:
        r = Rect.of(el)
        inp.click(dialog.client_x + r.center[0], dialog.client_y + r.center[1])

    dialog_click(hex_field)
    inp.key("ctrl", "a")
    inp.type_text("#c81e1e")
    inp.key("tab")
    dialog_click(krema.wait_for(f"{COLOR_DIALOG}//button[@name='OK']"))
    wait_until(lambda: not [w for w in krema.windows() if w.title.startswith("Choose tint color")], message="colour dialog closed")

    wait_until(lambda: (config_value(krema, "TintColor") or "").lower() == "#c81e1e", message="kremarc TintColor")
    assert config_value(krema, "UseSystemColor") == "false"
    assert krema.find(f"{SETTINGS}//button[@name='Tint color: #c81e1e']") is not None
    inp.move(W - 20, H // 3)
    r1, g1, b1 = wait_until(
        lambda: (m := mean(panel_band(krema, "tint-custom")))[0] > m[1] + 60 and m[0] > m[2] + 60 and m,
        timeout=5,
        message="dock panel tinted red",
    )
    # #c81e1e has almost no green/blue: both drop well below the neutral tint.
    assert g1 < g0 - 80 and b1 < b0 - 80, f"panel {(r1, g1, b1)} vs system tint {(r0, g0, b0)}"


# ------------------------------------------------------------------- SET-008
@pytest.mark.outputs(2)
@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": kcfg.AUTO_HIDE, "MonitorMode": 0})
def test_set008_monitor_mode_all_monitors_from_open_settings(krema: Krema, apps: TestWindows) -> None:
    outputs = kwin.evaluate("report(workspace.screens.map(s => ({name: s.name, x: s.geometry.x, y: s.geometry.y})));")
    assert [(o["x"], o["y"]) for o in outputs] == [(0, 0), (W, 0)], f"expected two side-by-side outputs: {outputs}"
    apps.open("Alpha")
    (primary,) = wait_until(lambda: dock_surfaces(krema) if len(dock_surfaces(krema)) == 1 else None)
    assert primary.client_x == 0
    reveal_dock(krema, "Alpha")
    win = open_settings(krema)
    open_page(krema, "Behavior")
    pid = krema.pid

    choose(krema, "Monitor mode", "All monitors")
    wait_until(lambda: config_value(krema, "MonitorMode") == "1", message="kremarc MonitorMode=1")
    docks = wait_until(lambda: d if len(d := dock_surfaces(krema)) == 2 else None, timeout=5, message="one dock per output")
    assert krema.pid == pid and krema.is_running()
    assert [d.client_x for d in docks] == [0, W] and docks[0].output != docks[1].output, docks
    assert [w.internal_id for w in settings_windows(krema)] == [win.internal_id], "the same dialog stays open"

    # Both docks are shown while the dialog is open, though mode is auto-hide.
    alpha_xpath = TOOLBAR_XPATH + "/button[@name='Alpha']"
    wait_until(lambda: len(krema.find_all(TOOLBAR_XPATH)) == 2, message="two dock tool bars")
    wait_until(
        lambda: len(e := krema.find_all(alpha_xpath)) == 2 and all(has_state(i, "showing") for i in e),
        timeout=5,
        message="Alpha on both docks",
    )
    pointer_to_center()

    def both_shown() -> bool:
        items = krema.find_all(alpha_xpath)
        return len(items) == 2 and all(has_state(i, "showing") for i in items)

    holds(both_shown, 1.0, "a dock hid while Settings is open")
    krema.screenshot("two-docks-and-settings")

    # Right-click the dock on the second output -> Settings... raises the same dialog.
    # Each dock window is positioned at its screen's top-left, and Qt adds that
    # position to the AT-SPI rects: the second dock's items come out shifted by W.
    first, second = sorted((Rect.of(i) for i in krema.find_all(alpha_xpath)), key=lambda r: r.x)
    assert second == Rect(first.x + W, first.y, first.width, first.height), f"docks lay out differently: {first} {second}"
    x, y = second.center[0], docks[1].client_y + second.center[1]
    assert x >= W
    kwin.activate(apps.open_windows[0].internal_id)

    def second_alpha() -> Rect:
        return max((Rect.of(i) for i in krema.find_all(alpha_xpath)), key=lambda r: r.x)

    # Glide onto the item like a user (a teleport + click in one chain onto
    # the just-created surface was occasionally dropped) and wait until that
    # dock reports the hover (its item zooms) before right-clicking.
    inp.move_path(inp.line((x, docks[1].client_y - 40), (x, y), 5), step_ms=40)
    wait_until(lambda: painted_rect(second_alpha(), second).width > second.width, timeout=5, message="second dock item hovered")
    r = wait_stable(second_alpha, duration=0.3)
    before = {w.internal_id for w in krema.windows()}
    inp.click(r.center[0], docks[1].client_y + r.center[1], button="right")
    menu = wait_until(
        lambda: next((w for w in krema.windows() if w.internal_id not in before and not w.normal_window), None),
        message="context menu on the second output",
    )
    assert menu.client_x >= W, f"menu opened on the wrong output: {menu}"
    krema.choose_context_menu_entry("Settings...", ENTRIES)
    wait_until(lambda: kwin.active_window() is not None and kwin.active_window().internal_id == win.internal_id, message="dialog raised")
    assert [w.internal_id for w in settings_windows(krema)] == [win.internal_id]

    # Back to primary only, close the dialog: the dock auto-hides again.
    choose(krema, "Monitor mode", "Primary monitor only")
    wait_until(lambda: len(dock_surfaces(krema)) == 1 and dock_surfaces(krema)[0].client_x == 0, timeout=5, message="one dock left")
    close_settings(krema)
    pointer_to_center()
    wait_until(lambda: not has_state(krema.item("Alpha"), "showing"), timeout=5, message="dock to auto-hide after close")

    # Reopening Settings after switching back works.
    reveal_dock(krema, "Alpha")
    reopened = open_settings(krema)
    assert len(settings_windows(krema)) == 1 and reopened.title == SETTINGS_TITLE
    assert krema.pid == pid


FOLLOW_ACTIVE = {"PinnedLaunchers": [], "VisibilityMode": kcfg.AUTO_HIDE, "MonitorMode": 2, "ScreenTransition": 2}
ALPHA_ITEMS = TOOLBAR_XPATH + "/button[@name='Alpha']"


def mapped_dock_xs(krema: Krema) -> list[int]:
    return [d.client_x for d in dock_surfaces(krema)]


def shown_alpha(krema: Krema) -> Rect | None:
    """Rect of the one shown Alpha item (x >= W: the second output's dock)."""
    items = [i for i in krema.find_all(ALPHA_ITEMS) if has_state(i, "showing")]
    return Rect.of(items[0]) if len(items) == 1 else None


@pytest.mark.outputs(2)
@pytest.mark.kremarc({**FOLLOW_ACTIVE, "FollowActiveTrigger": 0})
def test_set008_follow_active_mouse_opening_settings_keeps_dock_on_its_screen(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    wait_until(lambda: mapped_dock_xs(krema) == [0], message="dock on the primary output")
    reveal_dock(krema, "Alpha")
    open_settings(krema)

    # The open dialog's interaction lock must not count as pointer activity on
    # the other screen's (hidden) dock: the pointer crosses the second output,
    # including its bottom edge, and the dock stays on the primary output.
    inp.move_path(inp.line((W // 2, H // 3), (W + W // 2, H // 3), 10))
    inp.move(W + W // 2, H - 1)
    holds(lambda: mapped_dock_xs(krema) == [0], 1.5, "opening Settings moved the dock to another screen")
    assert has_state(krema.item("Alpha"), "showing"), "dock hid while Settings is open"
    assert len(settings_windows(krema)) == 1


@pytest.mark.outputs(2)
@pytest.mark.kremarc({**FOLLOW_ACTIVE, "FollowActiveTrigger": 0})
def test_set008_follow_active_mouse_trigger_moves_dock_to_the_pointer_screen(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    wait_until(lambda: mapped_dock_xs(krema) == [0], message="dock on the primary output")
    inp.move_path(inp.line((W // 2, H // 3), (W + 60, H - 1), 10))
    wait_until(lambda: mapped_dock_xs(krema) == [W], timeout=5, message="dock follows the pointer to the second output")


@pytest.mark.outputs(2)
@pytest.mark.kremarc({**FOLLOW_ACTIVE, "FollowActiveTrigger": 1})
def test_set008_follow_active_shortcuts_act_on_the_shown_dock(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    wait_until(lambda: mapped_dock_xs(krema) == [0], message="dock on the primary output")

    # Focus trigger: activating a window on the second output moves the dock there.
    # Move first and wait until KWin reports the window on output 2: krema's
    # follow-active watches IsActive dataChanged and reads ScreenGeometry then,
    # so the geometry must be settled before the activation edge.
    beta = apps.open("Beta", app_id=env.TEST_APP2_ID)
    kwin.evaluate(
        f"const w = workspace.windowList().find(w => String(w.internalId) === {beta.internal_id!r});"
        f"w.frameGeometry = {{x: {W + 300}, y: 200, width: 400, height: 300}}; report(true);"
    )
    wait_until(lambda: (w := beta.refresh()) is not None and w.client_x >= W, message="Beta moved to output 2")
    # apps.open returns once KWin maps the window; whether Beta auto-activated
    # already is a coin flip, so a bare activeWindow=w can be a no-op. Activate
    # Alpha first: the second assignment always produces an IsActive edge.
    kwin.activate(apps.open_windows[0].internal_id)
    kwin.activate(beta.internal_id)
    wait_until(
        lambda: (aw := kwin.active_window()) is not None and aw.internal_id == beta.internal_id,
        message="Beta active",
    )
    wait_until(lambda: mapped_dock_xs(krema) == [W], timeout=5, message="dock follows the active window")
    inp.move(W + W // 2, H // 3)
    wait_until(lambda: shown_alpha(krema) is None, timeout=5, message="auto-hide dock hidden")

    # Toggle Dock shows the dock on the second output, not the hidden primary one.
    invoke_shortcut("toggle-dock")
    wait_until(lambda: (r := shown_alpha(krema)) is not None and r.x >= W, timeout=5, message="Toggle Dock shows the second output's dock")
    assert mapped_dock_xs(krema) == [W]
    invoke_shortcut("toggle-dock")
    wait_until(lambda: shown_alpha(krema) is None, timeout=5, message="Toggle Dock hides it again")

    # Focus Dock focuses a button of the shown dock.
    invoke_shortcut("focus-dock")
    focused = wait_until(lambda: krema.find(TOOLBAR_XPATH + "/button[contains(@states, 'focused')]"), message="focused dock button")
    assert Rect.of(focused).x >= W, f"Focus Dock focused the hidden primary dock: {Rect.of(focused)}"
    wait_until(lambda: (r := shown_alpha(krema)) is not None and r.x >= W, timeout=5, message="focused dock shown")
    inp.key("escape")
    wait_until(lambda: krema.focused_item() is None, message="Escape ends keyboard navigation")
    assert mapped_dock_xs(krema) == [W]


# ------------------------------------------------------------------- SET-009
def _quit_via_menu(k: Krema) -> int:
    open_menu(k)
    k.choose_context_menu_entry("Quit", ENTRIES)
    assert k.process is not None
    return k.process.wait(20)


def _assert_clean_exit(k: Krema, code: int) -> None:
    log = Path(k.log_path).read_text(errors="replace")
    assert code == 0, f"krema exit status {code}; log {k.log_path}"
    for marker in ("KCrash", "Segmentation fault", "Aborted", "ASSERT"):
        assert marker not in log, f"crash marker {marker!r} in {k.log_path}"
    wait_until(lambda: not [w for w in kwin.windows() if w.pid == k.process.pid], message="all krema windows gone")


def test_set009_quit_while_settings_is_open_exits_cleanly(tmp_path: Path, apps: TestWindows, request: pytest.FixtureRequest) -> None:
    apps.open("Alpha")
    name = "test_06_settings_set009"
    first = Krema(tmp_path / "home", name=f"{name}-a")
    try:
        # 1. Settings... then immediately Quit (settings window still being created).
        first.start()
        open_menu(first)
        first.choose_context_menu_entry("Settings...", ENTRIES)
        code = _quit_via_menu(first)
        _assert_clean_exit(first, code)
    finally:
        first.stop()

    second = Krema(tmp_path / "home", name=f"{name}-b")
    try:
        # 2. Settings fully drawn, then Quit.
        second.start()
        open_settings(second)
        wait_until(lambda: has_state(second.wait_for(f"{SETTINGS}//list_item[label[@name='Icon size']]"), "showing"))
        code = _quit_via_menu(second)
        _assert_clean_exit(second, code)
    finally:
        second.stop()


# ------------------------------------------------------------------- SET-010
ZOOM_STYLE = "Zoom style"
#: Zoom animation card (Appearance): Preset/Custom TabButtons (AT-SPI
#: ``page_tab``) and one FormRadioDelegate (``radio_button``) per preset.
PRESET_TAB = f"{SETTINGS}//page_tab[@name='Preset']"
CUSTOM_TAB = f"{SETTINGS}//page_tab[@name='Custom']"
PARABOLIC, IN_PLACE = "Parabolic - neighbors move aside", "In place - icons overlap"


def preset_radio(name: str) -> str:
    return f"{SETTINGS}//radio_button[@name='{name}']"


def zoom_tab_selected(tab) -> bool:
    """A checkable TabButton reports its current tab as checked (or selected)."""
    return has_state(tab, "checked") or has_state(tab, "selected")


def hovered_middle_layout(krema: Krema) -> tuple[int, int, list[Rect], list[Rect]]:
    """Hover the middle dock item from rest; returns (index, base size, rest
    rects, drawn rects once the zoom settled)."""
    krema.move_away()
    rest = wait_stable(lambda: [Rect.of(e) for e in krema.items()])
    mid = len(rest) // 2
    krema.hover_item(krema.item_names()[mid])
    drawn = wait_stable(lambda: [painted_rect(Rect.of(e), r0) for e, r0 in zip(krema.items(), rest, strict=True)], duration=0.3)
    return mid, rest[mid].width, rest, drawn


def centre_shifts(rest: list[Rect], drawn: list[Rect]) -> list[int]:
    return [d.center[0] - r.center[0] for r, d in zip(rest, drawn)]


@pytest.mark.kremarc(
    {
        "PinnedLaunchers": [kcfg.launcher(i) for i in (env.TEST_APP2_ID, "org.kde.kwrite", "org.kde.kfind", "qt6-designer")],
        "MaxZoomFactor": 1.6,
        "PreviewEnabled": False,
    }
)
def test_set010_zoom_style_combo_switches_zoom_live_and_persists(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    krema.wait_for_item("Alpha")
    open_settings(krema)
    open_page(krema, "Appearance")
    row_xpath = f"{SETTINGS}//list_item[@name='{ZOOM_STYLE}']"

    # Two entries, Parabolic by default.
    assert current_choice(krema, ZOOM_STYLE) == PARABOLIC
    click_el(krema, scroll_into_view(krema, row_xpath))
    popup_xpath = (
        f"{SETTINGS}/dialog[.//*[self::list_item or self::menu_item][@name='{PARABOLIC}']]"
    )
    first = krema.wait_for(
        f"{popup_xpath}//*[self::list_item or self::menu_item][@name='{PARABOLIC}']"
    )
    wait_until(lambda: has_state(first, "showing") and Rect.of(first).width > 0, message="zoom style options shown")
    options = [
        e.get_attribute("name")
        for e in krema.find_all(f"{popup_xpath}//*[self::list_item or self::menu_item]")
        if has_state(e, "showing")
    ]
    assert len(options) == 2 and set(options) == {PARABOLIC, IN_PLACE}, f"zoom style options: {options}"
    click_el(krema, first)
    wait_until(lambda: current_choice(krema, ZOOM_STYLE) == PARABOLIC, message="combo closed on Parabolic")
    # Instant zoom keeps the hovered layouts below free of transitions.
    instant_xpath = preset_radio("Instant")
    instant = scroll_into_view(krema, instant_xpath)
    assert has_state(instant, "enabled") and has_state(krema.wait_for(PRESET_TAB), "enabled")
    assert has_state(krema.wait_for(preset_radio("Natural")), "checked")
    click_el(krema, instant)
    wait_until(lambda: has_state(krema.wait_for(instant_xpath), "checked"), message="instant zoom selected")
    wait_until(lambda: config_value(krema, "ZoomAnimationPreset") == "3", message="instant zoom saved")

    # Parabolic: neighbours move aside.
    mid, base, rest, drawn = hovered_middle_layout(krema)
    shifts = centre_shifts(rest, drawn)
    assert drawn[mid].width >= base * 1.5, f"middle item not zoomed: {drawn[mid]} (rest {rest[mid]})"
    assert shifts[mid - 1] < -2 and shifts[mid + 1] > 2, f"Parabolic: neighbours not pushed aside, centre shifts {shifts}"

    # In place applies live (no restart), persists, and scales icons where they are.
    pid = krema.pid
    choose(krema, ZOOM_STYLE, IN_PLACE)
    wait_until(lambda: config_value(krema, "ZoomStyle") == "1", message="kremarc ZoomStyle=1")
    mid, base, rest, drawn = hovered_middle_layout(krema)
    shifts = centre_shifts(rest, drawn)
    assert drawn[mid].width >= base * 1.5, f"middle item not zoomed: {drawn[mid]} (rest {rest[mid]})"
    assert all(abs(s) <= 2 for s in shifts), f"In place: icons moved, centre shifts {shifts}"
    assert drawn[mid - 1].x + drawn[mid - 1].width > drawn[mid].x, f"In place: no overlap with the magnified icon: {drawn}"
    assert krema.pid == pid and krema.is_running(), "zoom style change must not restart krema"

    # Back to Parabolic: 0 persisted (or the key dropped as the default).
    choose(krema, ZOOM_STYLE, PARABOLIC)
    wait_until(lambda: config_value(krema, "ZoomStyle") in ("0", None), message="kremarc ZoomStyle=0")
    mid, base, rest, drawn = hovered_middle_layout(krema)
    shifts = centre_shifts(rest, drawn)
    assert shifts[mid - 1] < -2 and shifts[mid + 1] > 2, f"Parabolic again: neighbours not pushed aside, centre shifts {shifts}"
    krema.move_away()

    # Zoom factor 1.0 (a press at the slider's left end) disables the combo;
    # raising it again (arrow keys on the focused slider) re-enables it.
    assert has_state(krema.wait_for(row_xpath), "enabled")
    slider = scroll_into_view(krema, f"{SETTINGS}//slider[@name='Zoom factor']")
    r = krema.screen_rect(slider, "settings")
    inp.click(r.x + 1, r.center[1])
    wait_until(lambda: float(config_value(krema, "MaxZoomFactor") or 0) == 1.0, message="kremarc MaxZoomFactor=1")
    wait_until(lambda: not has_state(krema.wait_for(row_xpath), "enabled"), message="zoom style combo disabled at zoom 1.0")
    assert not has_state(krema.wait_for(instant_xpath), "enabled"), "zoom animation presets must be disabled when zoom is off"
    assert not has_state(krema.wait_for(PRESET_TAB), "enabled"), "zoom animation tabs must be disabled when zoom is off"
    for _ in range(6):
        inp.key("right")
    wait_until(lambda: abs(float(config_value(krema, "MaxZoomFactor") or 0) - 1.6) < 1e-6, message="kremarc MaxZoomFactor=1.6")
    wait_until(lambda: has_state(krema.wait_for(row_xpath), "enabled"), message="zoom style combo enabled again")
    wait_until(lambda: has_state(krema.wait_for(instant_xpath), "enabled"), message="zoom animation presets enabled again")
    assert has_state(krema.wait_for(PRESET_TAB), "enabled"), "zoom animation tabs enabled again"
    assert has_state(krema.wait_for(instant_xpath), "checked"), "disabling zoom must preserve the zoom animation preset"
    assert config_value(krema, "ZoomAnimationPreset") == "3"


# ------------------------------------------------------------------- SET-011
ZOOM_IN_DURATION = f"{SETTINGS}//list_item[label[@name='Zoom-in duration (ms)']]//spin_button"
ZOOM_OUT_EASING = "Zoom-out easing"


@pytest.mark.kremarc({"PinnedLaunchers": [], "PreviewEnabled": False})
def test_set011_zoom_animation_preset_and_custom_tabs_apply_and_persist(krema: Krema, apps: TestWindows) -> None:
    apps.open("Alpha")
    open_settings(krema)
    quick_xpath = preset_radio("Quick")
    click_el(krema, scroll_into_view(krema, quick_xpath))
    wait_until(lambda: has_state(krema.wait_for(quick_xpath), "checked"), message="Quick preset selected")
    wait_until(lambda: config_value(krema, "ZoomAnimationPreset") == "1", message="kremarc ZoomAnimationPreset=1")

    # Custom tab: stores the Custom preset and shows the custom controls,
    # whose defaults equal Natural.
    click_el(krema, scroll_into_view(krema, CUSTOM_TAB))
    wait_until(lambda: zoom_tab_selected(krema.wait_for(CUSTOM_TAB)), message="Custom tab selected")
    wait_until(lambda: config_value(krema, "ZoomAnimationPreset") == "4", message="kremarc ZoomAnimationPreset=4")
    spin = scroll_into_view(krema, ZOOM_IN_DURATION)
    assert float(spin.get_attribute("value")) == 180.0
    assert current_choice(krema, ZOOM_OUT_EASING) == "Ease in and out"
    click_el(krema, spin)
    for _ in range(2):
        inp.key("up")
    wait_until(lambda: float(spin.get_attribute("value")) == 200.0, message="zoom-in duration changed in 10 ms steps")
    wait_until(lambda: config_value(krema, "ZoomInDuration") == "200", message="kremarc ZoomInDuration=200")
    choose(krema, ZOOM_OUT_EASING, "Linear")
    wait_until(lambda: config_value(krema, "ZoomOutEasing") == "0", message="kremarc ZoomOutEasing=0")

    # Smoke only: hover still magnifies without a restart after the custom
    # edits. Whether the custom timing really drives the dock is covered by
    # tests/qml/tst_dock_main.qml::test_customZoomTimingDrivesProductionDock.
    pid = krema.pid
    hover_ready(krema, "Alpha")
    krema.move_away()
    assert krema.pid == pid and krema.is_running(), "zoom animation change must not restart krema"

    # Preset tab restores the preset chosen before Custom, and only that
    # radio is checked.
    click_el(krema, scroll_into_view(krema, PRESET_TAB))
    wait_until(lambda: zoom_tab_selected(krema.wait_for(PRESET_TAB)), message="Preset tab selected")
    wait_until(lambda: config_value(krema, "ZoomAnimationPreset") == "1", message="Quick preset restored")
    assert has_state(scroll_into_view(krema, quick_xpath), "checked")
    for name in ("Natural", "Relaxed", "Instant"):
        assert not has_state(krema.wait_for(preset_radio(name)), "checked"), f"{name} must stay unchecked"

    # A second preset switch: exactly the selected radio is checked.
    relaxed_xpath = preset_radio("Relaxed")
    click_el(krema, scroll_into_view(krema, relaxed_xpath))
    wait_until(lambda: has_state(krema.wait_for(relaxed_xpath), "checked"), message="Relaxed preset selected")
    wait_until(lambda: config_value(krema, "ZoomAnimationPreset") == "2", message="kremarc ZoomAnimationPreset=2")
    for name in ("Natural", "Quick", "Instant"):
        assert not has_state(krema.wait_for(preset_radio(name)), "checked"), f"{name} must stay unchecked"
    click_el(krema, scroll_into_view(krema, quick_xpath))
    wait_until(lambda: config_value(krema, "ZoomAnimationPreset") == "1", message="Quick preset selected again")
    assert has_state(krema.wait_for(quick_xpath), "checked")
    assert not has_state(krema.wait_for(relaxed_xpath), "checked")
    # Clicking the already-checked radio keeps it checked and the preset unchanged.
    click_el(krema, scroll_into_view(krema, quick_xpath))
    holds(
        lambda: has_state(krema.wait_for(quick_xpath), "checked") and config_value(krema, "ZoomAnimationPreset") == "1",
        1.0,
        "re-clicking the checked Quick radio must keep it checked and ZoomAnimationPreset=1",
    )

    # Regression: wheel over the Preset/Custom tab bar scrolls the page (the
    # org.kde.desktop style's TabBar wheel handler once hijacked the scroll);
    # it must not switch the tab or rewrite the preset.
    tab = scroll_into_view(krema, PRESET_TAB)
    wheel_x, wheel_y = krema.screen_rect(tab, "settings").center
    tab_y0 = Rect.of(krema.wait_for(PRESET_TAB)).y
    # Let Kirigami's WheelHandler leave its scrolling state (400 ms after the
    # last step): until then its filter item covers the page and takes the
    # wheel before the tab bar could.
    time.sleep(0.5)
    inp.scroll(wheel_x, wheel_y, dy=60)
    wait_until(
        lambda: Rect.of(krema.wait_for(PRESET_TAB)).y < tab_y0 - 10,
        message="wheel over the tab bar scrolls the page",
    )
    assert config_value(krema, "ZoomAnimationPreset") == "1", "wheel over the tab bar must not switch to Custom"
    assert zoom_tab_selected(krema.wait_for(PRESET_TAB)) and not zoom_tab_selected(krema.wait_for(CUSTOM_TAB))
    assert has_state(krema.wait_for(quick_xpath), "checked")
    # Scroll back up over the scroll bar: the old tab centre now lies over
    # whatever row scrolled under it, possibly a ComboBox that takes the wheel.
    tab_y1 = wait_stable(lambda: Rect.of(krema.wait_for(PRESET_TAB)).y, duration=0.3)
    inp.scroll(*page_wheel_point(krema), dy=-60)
    wait_until(
        lambda: Rect.of(krema.wait_for(PRESET_TAB)).y > tab_y1 + 10,
        message="wheel up over the scroll bar scrolls the page back",
    )
    assert config_value(krema, "ZoomAnimationPreset") == "1"
    assert zoom_tab_selected(krema.wait_for(PRESET_TAB)) and not zoom_tab_selected(krema.wait_for(CUSTOM_TAB))
    assert has_state(krema.wait_for(quick_xpath), "checked")

    # Custom tab keeps the custom values.
    click_el(krema, scroll_into_view(krema, CUSTOM_TAB))
    wait_until(lambda: config_value(krema, "ZoomAnimationPreset") == "4", message="Custom preset selected again")
    assert float(scroll_into_view(krema, ZOOM_IN_DURATION).get_attribute("value")) == 200.0
    assert current_choice(krema, ZOOM_OUT_EASING) == "Linear"
    close_settings(krema)

    saved = {k: config_value(krema, k) for k in ("ZoomAnimationPreset", "ZoomInDuration", "ZoomOutEasing")}
    assert saved == {"ZoomAnimationPreset": "4", "ZoomInDuration": "200", "ZoomOutEasing": "0"}
    old_pid = krema.pid
    krema.restart()
    assert krema.pid != old_pid
    assert {k: config_value(krema, k) for k in saved} == saved

    open_settings(krema)
    wait_until(lambda: zoom_tab_selected(scroll_into_view(krema, CUSTOM_TAB)), message="Custom tab restored")
    assert not zoom_tab_selected(krema.wait_for(PRESET_TAB))
    assert float(scroll_into_view(krema, ZOOM_IN_DURATION).get_attribute("value")) == 200.0
    assert current_choice(krema, ZOOM_OUT_EASING) == "Linear"

    # A new Settings session has no earlier preset: Preset falls back to Natural.
    click_el(krema, scroll_into_view(krema, PRESET_TAB))
    wait_until(lambda: config_value(krema, "ZoomAnimationPreset") in ("0", None), message="Natural preset restored")
    assert has_state(scroll_into_view(krema, preset_radio("Natural")), "checked")
    close_settings(krema)


# ------------------------------------------------------------------- QA-CLK-002 / QA-CLK-011
SINGLE_CLICK_ROW = "Single window click action"
GROUPED_CLICK_ROW = "Grouped window click action"
SINGLE_CLICK_CHOICES = ("Activate window", "Minimize active window")
GROUPED_CLICK_CHOICES = ("Cycle through windows", "Show window previews", "Minimize active window")


def click_item_on_output(krema: Krema, name: str, output_x: int = 0) -> None:
    """Click the real item on one output; AT-SPI includes its horizontal offset."""
    xpath = TOOLBAR_XPATH + f"/button[@name='{name}']"

    def item_rect() -> Rect | None:
        return next(
            (
                Rect.of(item)
                for item in krema.find_all(xpath)
                if has_state(item, "showing") and output_x <= Rect.of(item).center[0] < output_x + W
            ),
            None,
        )

    dock = wait_until(
        lambda: next((d for d in dock_surfaces(krema) if d.client_x == output_x), None),
        message=f"dock on output at {output_x}",
    )
    rect = wait_until(item_rect, message=f"{name} on output at {output_x}")
    inp.move(rect.center[0], dock.client_y + rect.center[1])
    rect = wait_stable(item_rect)
    inp.click(rect.center[0], dock.client_y + rect.center[1])


def visible_preview(krema: Krema):
    """The shown popup, including when another output's popup is hidden first."""
    return next(
        (popup for popup in krema.find_all(PREVIEW_XPATH) if has_state(popup, "showing") and Rect.of(popup).width > 0),
        None,
    )


def assert_click_action_effects(
    krema: Krema,
    single_action: int,
    grouped_action: int,
    solo: TestWindow,
    alpha: TestWindow,
    beta: TestWindow,
    output_x: int = 0,
) -> None:
    """Observe KWin window state and the actual popup, not action dispatch."""
    krema.move_away(close_preview=False)
    wait_until(lambda: visible_preview(krema) is None, message="all output previews closed before the next click")
    kwin.activate(solo.internal_id)
    wait_until(solo.is_active, message="Solo active before its dock click")
    click_item_on_output(krema, "Solo", output_x)
    if single_action == 1:
        wait_until(lambda: (w := solo.refresh()) is not None and w.minimized, message="active Solo minimized")
        assert visible_preview(krema) is None, "the grouped preview choice must not apply to Solo"
        click_item_on_output(krema, "Solo", output_x)
        wait_until(
            lambda: (w := solo.refresh()) is not None and w.active and not w.minimized,
            message="minimized Solo restored and activated",
        )
    else:
        holds(
            lambda: (w := solo.refresh()) is not None and w.active and not w.minimized and visible_preview(krema) is None,
            0.5,
            "Activate window must keep the active single window unminimized",
        )

    krema.move_away()
    for target in (alpha, beta, solo):
        kwin.activate(target.internal_id)
        wait_until(target.is_active, message=f"{target.title} active during MRU setup")
        assert wait_stable(target.is_active, duration=0.5), f"{target.title} active during MRU setup"
    wait_until(
        lambda: _item_has_description(krema, "Solo", "Active")
        and not _item_has_description(krema, env.TEST_APP_NAME, "Active"),
        message="dock model to observe Solo backgrounding the group",
    )
    assert wait_stable(
        lambda: _item_has_description(krema, "Solo", "Active")
        and not _item_has_description(krema, env.TEST_APP_NAME, "Active"),
        duration=0.5,
    ), "dock model to observe Solo backgrounding the group"
    if grouped_action == 0:
        for target in (beta, alpha, beta):
            click_item_on_output(krema, env.TEST_APP_NAME, output_x)
            wait_until(target.is_active, message=f"group click cycles to {target.title}")
            assert not alpha.refresh().minimized and not beta.refresh().minimized
            assert visible_preview(krema) is None
    elif grouped_action == 1:
        click_item_on_output(krema, env.TEST_APP_NAME, output_x)
        wait_until(lambda: visible_preview(krema), message="explicit grouped preview opens")
        wait_until(
            lambda: {
                label.get_attribute("name")
                for label in krema.find_all(pv.THUMB_XPATH + "/label")
                if has_state(label, "showing")
            }
            == {"Alpha", "Beta"},
            message="both group windows in the shown popup",
        )
        click_item_on_output(krema, env.TEST_APP_NAME, output_x)
        assert visible_preview(krema) is not None and solo.is_active(), "repeated preview clicks must not activate a group child"
        assert not alpha.refresh().minimized and not beta.refresh().minimized
        if output_x == 0:
            # The existing painted-popup oracle is limited to the primary output.
            popup = visible_preview(krema)
            pv.wait_on_screen(krema, popup)
            target = pv.screen_rect(krema, krema.wait_for(pv.thumb_xpath("Alpha") + "[contains(@states, 'showing')]")).center
            pv.glide_into(krema, target)
            inp.click()
            wait_until(alpha.is_active, message="chosen preview thumbnail activates Alpha")
            wait_until(lambda: visible_preview(krema) is None, message="thumbnail activation closes the popup")
    else:
        kwin.activate(beta.internal_id)
        wait_until(beta.is_active, message="Beta active before grouped minimize")
        click_item_on_output(krema, env.TEST_APP_NAME, output_x)
        wait_until(lambda: (w := beta.refresh()) is not None and w.minimized, message="only the active group child minimized")
        assert not alpha.refresh().minimized and not solo.refresh().minimized
        assert visible_preview(krema) is None
        kwin.activate(beta.internal_id)
        wait_until(beta.is_active, message="Beta restored for the next output or restart")
    krema.move_away(close_preview=False)
    wait_until(lambda: visible_preview(krema) is None, message="shown output preview closes after leaving")


@pytest.mark.parametrize(
    ("single_action", "grouped_action"),
    [
        pytest.param(
            single,
            grouped,
            id=f"single-{single}-group-{grouped}",
            marks=pytest.mark.kremarc(
                {
                    "PinnedLaunchers": [],
                    "PreviewEnabled": False,
                    "SingleWindowClickAction": 1 - single,
                    "GroupedWindowClickAction": (grouped + 1) % 3,
                }
            ),
        )
        for single in range(2)
        for grouped in range(3)
    ],
)
def test_clk002_click_action_combinations_apply_live_persist_and_restore(
    krema: Krema, apps: TestWindows, single_action: int, grouped_action: int
) -> None:
    alpha = apps.open("Alpha")
    beta = apps.open("Beta")
    solo = apps.open("Solo", app_id=env.TEST_APP2_ID)
    krema.wait_for_item(env.TEST_APP_NAME)
    open_settings(krema, "Solo")
    open_page(krema, "Behavior")
    original_group = (grouped_action + 1) % 3
    assert current_choice(krema, SINGLE_CLICK_ROW) == SINGLE_CLICK_CHOICES[1 - single_action]
    assert current_choice(krema, GROUPED_CLICK_ROW) == GROUPED_CLICK_CHOICES[original_group]
    pid = krema.pid

    choose(krema, SINGLE_CLICK_ROW, SINGLE_CLICK_CHOICES[single_action])
    assert current_choice(krema, GROUPED_CLICK_ROW) == GROUPED_CLICK_CHOICES[original_group]
    wait_until(
        lambda: int(config_value(krema, "SingleWindowClickAction") or 0) == single_action
        and int(config_value(krema, "GroupedWindowClickAction") or 0) == original_group,
        message="the single choice saves without changing the grouped choice",
    )
    choose(krema, GROUPED_CLICK_ROW, GROUPED_CLICK_CHOICES[grouped_action])
    assert current_choice(krema, SINGLE_CLICK_ROW) == SINGLE_CLICK_CHOICES[single_action]
    wait_until(
        lambda: int(config_value(krema, "SingleWindowClickAction") or 0) == single_action
        and int(config_value(krema, "GroupedWindowClickAction") or 0) == grouped_action,
        message="both independent click choices saved",
    )
    close_settings(krema)
    assert_click_action_effects(krema, single_action, grouped_action, solo, alpha, beta)
    assert krema.pid == pid, "both choices must take effect without restarting the dock"

    krema.restart()
    open_settings(krema, "Solo")
    open_page(krema, "Behavior")
    assert current_choice(krema, SINGLE_CLICK_ROW) == SINGLE_CLICK_CHOICES[single_action]
    assert current_choice(krema, GROUPED_CLICK_ROW) == GROUPED_CLICK_CHOICES[grouped_action]
    close_settings(krema)
    assert_click_action_effects(krema, single_action, grouped_action, solo, alpha, beta)


@pytest.mark.outputs(2)
@pytest.mark.kremarc(
    {"PinnedLaunchers": [], "PreviewEnabled": False, "MonitorMode": 0, "SingleWindowClickAction": 0, "GroupedWindowClickAction": 0}
)
def test_clk002_all_screens_share_live_click_choices_and_recreated_dock_restores_them(krema: Krema, apps: TestWindows) -> None:
    alpha = apps.open("Alpha")
    beta = apps.open("Beta")
    solo = apps.open("Solo", app_id=env.TEST_APP2_ID)
    open_settings(krema, "Solo")
    open_page(krema, "Behavior")
    choose(krema, SINGLE_CLICK_ROW, SINGLE_CLICK_CHOICES[1])
    choose(krema, GROUPED_CLICK_ROW, GROUPED_CLICK_CHOICES[1])
    choose(krema, "Monitor mode", "All monitors")
    wait_until(lambda: mapped_dock_xs(krema) == [0, W], message="both All monitors docks mapped")
    close_settings(krema)
    for output_x in (0, W):
        assert_click_action_effects(krema, 1, 1, solo, alpha, beta, output_x)

    open_settings(krema, "Solo")
    open_page(krema, "Behavior")
    choose(krema, GROUPED_CLICK_ROW, GROUPED_CLICK_CHOICES[2])
    assert current_choice(krema, SINGLE_CLICK_ROW) == SINGLE_CLICK_CHOICES[1]
    wait_until(
        lambda: config_value(krema, "SingleWindowClickAction") == "1" and config_value(krema, "GroupedWindowClickAction") == "2",
        message="shared independent settings saved",
    )
    close_settings(krema)
    for output_x in (0, W):
        assert_click_action_effects(krema, 1, 2, solo, alpha, beta, output_x)

    open_settings(krema, "Solo")
    open_page(krema, "Behavior")
    choose(krema, "Monitor mode", "Primary monitor only")
    wait_until(lambda: mapped_dock_xs(krema) == [0], message="secondary dock removed")
    choose(krema, "Monitor mode", "All monitors")
    wait_until(lambda: mapped_dock_xs(krema) == [0, W], message="secondary dock recreated")
    assert current_choice(krema, SINGLE_CLICK_ROW) == SINGLE_CLICK_CHOICES[1]
    assert current_choice(krema, GROUPED_CLICK_ROW) == GROUPED_CLICK_CHOICES[2]
    close_settings(krema)
    assert_click_action_effects(krema, 1, 2, solo, alpha, beta, W)


@pytest.mark.parametrize(
    ("hover_enabled", "grouped_action"),
    [
        pytest.param(
            hover,
            grouped,
            id=f"hover-{int(hover)}-group-{grouped}",
            marks=pytest.mark.kremarc(
                {
                    "PinnedLaunchers": [],
                    "PreviewEnabled": not hover,
                    "PreviewThumbnailSize": 200,
                    "PreviewHoverDelay": 500,
                    "PreviewHideDelay": 200,
                    "GroupedWindowClickAction": (grouped + 1) % 3,
                }
            ),
        )
        for hover in (False, True)
        for grouped in range(3)
    ],
)
def test_clk011_preview_controls_follow_hover_and_explicit_group_choice(
    krema: Krema, apps: TestWindows, hover_enabled: bool, grouped_action: int
) -> None:
    apps.open("Alpha")
    apps.open("Beta")
    apps.open("Solo", app_id=env.TEST_APP2_ID)
    open_settings(krema, "Solo")
    open_page(krema, "Behavior")
    choose(krema, GROUPED_CLICK_ROW, GROUPED_CLICK_CHOICES[grouped_action])
    open_page(krema, "Window Preview")
    switch_xpath = f"{SETTINGS}//check_box[@name='Show window previews on hover']"
    switch = scroll_into_view(krema, switch_xpath)
    click_el(krema, switch)
    wait_until(lambda: has_state(krema.wait_for(switch_xpath), "checked") == hover_enabled, message="hover previews changed")
    wait_until(
        lambda: (config_value(krema, "PreviewEnabled") or "true").lower() == str(hover_enabled).lower(),
        message="hover preview setting saved",
    )

    preview_controls_enabled = hover_enabled or grouped_action == 1
    for label, enabled, key in (
        ("Thumbnail width (px)", preview_controls_enabled, "PreviewThumbnailSize"),
        ("Hide delay (ms)", preview_controls_enabled, "PreviewHideDelay"),
        ("Hover delay (ms)", hover_enabled, "PreviewHoverDelay"),
    ):
        xpath = f"{SETTINGS}//list_item[label[@name='{label}']]//spin_button"
        spin = scroll_into_view(krema, xpath)
        assert has_state(spin, "enabled") == enabled, f"{label} availability must match how previews can be opened"
        if enabled:
            old_value = float(spin.get_attribute("value"))
            click_el(krema, spin)
            inp.key("up")
            step = 20 if key == "PreviewThumbnailSize" else 50
            wait_until(lambda: float(spin.get_attribute("value")) == old_value + step, message=f"{label} changed through the UI")
            wait_until(lambda: int(config_value(krema, key) or 0) == int(old_value + step), message=f"{label} saved")
    close_settings(krema)

    krema.hover_item(env.TEST_APP_NAME)
    if not preview_controls_enabled:
        holds(lambda: not krema.preview_visible(), 0.8, "disabled hover previews must not open on a group")
        return
    if not hover_enabled:
        holds(lambda: not krema.preview_visible(), 0.8, "explicit preview mode must not enable hover previews")
        krema.click_item(env.TEST_APP_NAME)
    wait_until(krema.preview_visible, message="preview opens through the enabled interaction")
    wait_until(lambda: set(pv.thumb_titles(krema)) == {"Alpha", "Beta"}, message="group preview children")
    width = wait_stable(lambda: Rect.of(krema.wait_for(pv.thumb_xpath("Alpha") + "/label")).width)
    assert width == 220, f"UI-selected thumbnail width was not applied to the real popup: {width}"
    started = time.monotonic()
    inp.move(W // 2, 20)
    wait_until(lambda: not krema.preview_visible(), message="popup closes using its UI-selected hide delay")
    elapsed = time.monotonic() - started
    assert elapsed >= 0.25 * 0.95, f"popup closed before the selected 250 ms hide delay: {elapsed:.3f}s"
# ------------------------------------------------------------------- SET-012
@pytest.mark.outputs(2)
@pytest.mark.no_krema_autostart
def test_set012_selected_monitors_toggle_keeps_settings_open(krema: Krema, apps: TestWindows) -> None:
    outputs = kwin.evaluate("report(workspace.screens.map(s => ({name: s.name, x: s.geometry.x, y: s.geometry.y})));")
    assert [(o["x"], o["y"]) for o in outputs] == [(0, 0), (W, 0)], outputs
    first, second = [o["name"] for o in outputs]
    krema.write_config(
        {"PinnedLaunchers": [], "VisibilityMode": kcfg.AUTO_HIDE, "MonitorMode": 0, "SelectedOutputs": [first]}
    )
    krema.start()
    apps.open("Alpha")
    wait_until(lambda: mapped_dock_xs(krema) == [0], timeout=5, message="primary-only control dock")
    reveal_dock(krema, "Alpha")
    win = open_settings(krema)
    open_page(krema, "Behavior")

    # Healthy existing-mode control before exercising the new native option.
    choose(krema, "Monitor mode", "All monitors")
    wait_until(lambda: mapped_dock_xs(krema) == [0, W], timeout=5, message="all-monitors control docks")
    choose(krema, "Monitor mode", "Selected monitors")
    wait_until(lambda: mapped_dock_xs(krema) == [0], timeout=5, message="only the saved selected output")

    second_xpath = f"{SETTINGS}//check_box[@name='{second}']"
    click_el(krema, scroll_into_view(krema, second_xpath))
    wait_until(lambda: has_state(krema.wait_for(second_xpath), "checked"), message="second output selected")
    wait_until(lambda: mapped_dock_xs(krema) == [0, W], timeout=5, message="both selected docks mapped")
    retained_dock = next(d for d in dock_surfaces(krema) if d.output == second)
    retained_preview = wait_until(
        lambda: next((p for p in preview_surfaces(krema) if p.output == second), None),
        message="new output's preview surface",
    )
    wait_until(
        lambda: len(items := krema.find_all(ALPHA_ITEMS)) == 2 and all(has_state(i, "showing") for i in items),
        message="new selected dock inherits Settings interaction lock",
    )
    pointer_to_center()
    holds(
        lambda: len(items := krema.find_all(ALPHA_ITEMS)) == 2 and all(has_state(i, "showing") for i in items),
        1.0,
        "a newly selected dock hid while Settings is open",
    )
    first_xpath = f"{SETTINGS}//check_box[@name='{first}']"
    click_el(krema, scroll_into_view(krema, first_xpath))
    wait_until(lambda: not has_state(krema.wait_for(first_xpath), "checked"), message="first output deselected")
    wait_until(lambda: mapped_dock_xs(krema) == [W], timeout=5, message="exactly one dock on output 2")
    (dock,) = dock_surfaces(krema)
    assert dock.output == second and dock.client_x == W, dock
    assert dock.internal_id == retained_dock.internal_id, "deselecting another output rebuilt the retained dock"
    wait_until(
        lambda: len(previews := preview_surfaces(krema)) == 1
        and previews[0].output == second
        and previews[0].internal_id == retained_preview.internal_id,
        message="only the retained output's original preview surface",
    )
    assert [w.internal_id for w in settings_windows(krema)] == [win.internal_id], "originating dock removal closed Settings"
    wait_until(
        lambda: (active := kwin.active_window()) is not None and active.internal_id == win.internal_id,
        message="the same Settings window remains focused",
    )
    wait_until(
        lambda: config_value(krema, "MonitorMode") == "3" and kcfg.as_list(config_value(krema, "SelectedOutputs") or "") == [second],
        message="selected output names persisted exactly",
    )
    pointer_to_center()
    holds(lambda: has_state(krema.wait_for_item("Alpha"), "showing"), 1.0, "retained dock lost Settings interaction lock")
    close_settings(krema)
    wait_until(lambda: shown_alpha(krema) is None, timeout=5, message="selected dock auto-hides once Settings closes")

    # Cold restart must restore mode 3 and the exact output name, not clamp it.
    old_pid = krema.pid
    krema.restart()
    assert krema.pid != old_pid and krema.is_running()
    wait_until(lambda: mapped_dock_xs(krema) == [W], timeout=5, message="restarted selected dock on output 2")
    assert config_value(krema, "MonitorMode") == "3"
    assert kcfg.as_list(config_value(krema, "SelectedOutputs") or "") == [second]
    assert not settings_windows(krema)

    # The pointer is on unselected output 1. Focus Dock still targets output 2.
    pointer_to_center()
    invoke_shortcut("focus-dock")
    wait_until(lambda: krema.focused_item() == "Alpha", message="selected dock keyboard navigation")
    (dock,) = dock_surfaces(krema)
    wait_until(
        lambda: (active := kwin.active_window()) is not None and active.internal_id == dock.internal_id,
        message="KWin keyboard focus on selected output's dock",
    )
    inp.key("Up")
    wait_until(krema.preview_visible, message="selected output's preview opens through keyboard navigation")
    (preview,) = wait_until(
        lambda: p if len(p := preview_surfaces(krema)) == 1 else None,
        message="one preview surface for the one selected output",
    )
    assert preview.output == second and preview.client_x == W, preview
    wait_until(
        lambda: krema.find("//popup_menu/button/label[@name='Alpha']") is not None,
        message="selected dock's preview contains the actual app window",
    )


FALLBACK_WARNING = SETTINGS + "//*[contains(@name, 'temporary dock') and contains(@name, 'primary')]"
DISCONNECTED_OUTPUT = "KREMA-disconnected-output"


def selected_outputs(krema: Krema) -> list[str]:
    return kcfg.as_list(config_value(krema, "SelectedOutputs") or "")


def fallback_warning_visible(krema: Krema) -> bool:
    return any(has_state(el, "showing") for el in krema.find_all(FALLBACK_WARNING))


def set_output_selected(krema: Krema, name: str, selected: bool) -> None:
    xpath = f"{SETTINGS}//check_box[@name='{name}']"
    row = scroll_into_view(krema, xpath)
    if has_state(row, "checked") != selected:
        click_el(krema, row)
    wait_until(
        lambda: has_state(krema.wait_for(xpath), "checked") == selected,
        message=f"output {name} selected={selected}",
    )


@pytest.mark.outputs(2)
@pytest.mark.no_krema_autostart
@pytest.mark.parametrize("saved_names", [[], [DISCONNECTED_OUTPUT]], ids=["empty", "disconnected"])
def test_set012_selected_monitors_fallback_warns_and_keeps_saved_names(
    krema: Krema, apps: TestWindows, saved_names: list[str]
) -> None:
    outputs = kwin.evaluate("report(workspace.screens.map(s => ({name: s.name, x: s.geometry.x, y: s.geometry.y})));")
    assert [(o["x"], o["y"]) for o in outputs] == [(0, 0), (W, 0)], outputs
    first, second = [o["name"] for o in outputs]
    assert DISCONNECTED_OUTPUT not in (first, second)
    krema.write_config(
        {"PinnedLaunchers": [], "VisibilityMode": kcfg.AUTO_HIDE, "MonitorMode": 3, "SelectedOutputs": saved_names}
    )
    krema.start()
    apps.open("Alpha")
    wait_until(lambda: mapped_dock_xs(krema) == [0], timeout=5, message="one temporary primary dock")
    assert dock_surfaces(krema)[0].output == first
    reveal_dock(krema, "Alpha")
    win = open_settings(krema)
    open_page(krema, "Behavior")
    assert current_choice(krema, "Monitor mode") == "Selected monitors"
    scroll_into_view(krema, FALLBACK_WARNING)
    wait_until(lambda: fallback_warning_visible(krema), message="temporary primary fallback warning")
    assert selected_outputs(krema) == saved_names, "fallback rewrote the user's saved selection"
    for name in (first, second):
        assert not has_state(krema.wait_for(f"{SETTINGS}//check_box[@name='{name}']"), "checked")

    if saved_names:
        missing_xpath = f"{SETTINGS}//check_box[@name='{DISCONNECTED_OUTPUT}']"
        assert has_state(krema.wait_for(missing_xpath), "checked"), "saved disconnected output is not removable"
        choose(krema, "Monitor mode", "All monitors")
        wait_until(lambda: mapped_dock_xs(krema) == [0, W], timeout=5, message="old all-monitors mode remains healthy")
        assert selected_outputs(krema) == saved_names
        assert not fallback_warning_visible(krema), "selected-mode warning remained visible in All monitors"
        assert not any(has_state(el, "showing") for el in krema.find_all(missing_xpath)), "output switches escaped Selected mode"
        choose(krema, "Monitor mode", "Primary monitor only")
        wait_until(lambda: mapped_dock_xs(krema) == [0], timeout=5, message="old primary-only mode remains healthy")
        assert selected_outputs(krema) == saved_names
        choose(krema, "Monitor mode", "Selected monitors")
        scroll_into_view(krema, FALLBACK_WARNING)
        wait_until(lambda: fallback_warning_visible(krema), message="fallback warning restored with saved unavailable selection")

        # Real native toggle, then the unavailable row disappears rather than
        # leaving an unremovable stale screen entry.
        click_el(krema, scroll_into_view(krema, missing_xpath))
        wait_until(lambda: selected_outputs(krema) == [], message="disconnected name removed from kremarc")
        wait_until(lambda: krema.find(missing_xpath) is None, message="removed disconnected output row disappears")
        wait_until(lambda: mapped_dock_xs(krema) == [0], timeout=5, message="one primary fallback after pruning")

    # A live selection removes fallback and its warning without closing the
    # dialog whose originating primary dock has just been destroyed.
    set_output_selected(krema, second, True)
    wait_until(lambda: mapped_dock_xs(krema) == [W], timeout=5, message="live selection replaces primary fallback")
    wait_until(lambda: selected_outputs(krema) == [second], message="only the user's live selection saved")
    wait_until(lambda: not fallback_warning_visible(krema), message="no warning for a connected selected output")
    assert [w.internal_id for w in settings_windows(krema)] == [win.internal_id]

    set_output_selected(krema, second, False)
    wait_until(lambda: mapped_dock_xs(krema) == [0], timeout=5, message="empty selection restores exactly one fallback")
    wait_until(lambda: selected_outputs(krema) == [], message="empty selection saved without inventing primary")
    scroll_into_view(krema, FALLBACK_WARNING)
    wait_until(lambda: fallback_warning_visible(krema), message="empty selection warning returns")
    assert [w.internal_id for w in settings_windows(krema)] == [win.internal_id]


@pytest.mark.outputs(3)
@pytest.mark.no_krema_autostart
def test_set012_selected_subset_preserves_docks_and_routes_shortcuts(krema: Krema, apps: TestWindows) -> None:
    outputs = kwin.evaluate("report(workspace.screens.map(s => ({name: s.name, x: s.geometry.x, y: s.geometry.y})));")
    assert [(o["x"], o["y"]) for o in outputs] == [(0, 0), (W, 0), (2 * W, 0)], outputs
    first, second, third = [o["name"] for o in outputs]
    # Save the opposite of compositor order to prove shortcut routing does
    # not accidentally follow list insertion or unordered-map iteration.
    krema.write_config(
        {"PinnedLaunchers": [], "VisibilityMode": kcfg.AUTO_HIDE, "MonitorMode": 0, "SelectedOutputs": [third, second]}
    )
    krema.start()
    apps.open("Alpha")
    wait_until(lambda: mapped_dock_xs(krema) == [0], timeout=5, message="primary-only control on three outputs")
    reveal_dock(krema, "Alpha")
    win = open_settings(krema)
    open_page(krema, "Behavior")
    choose(krema, "Monitor mode", "Selected monitors")
    wait_until(lambda: mapped_dock_xs(krema) == [W, 2 * W], timeout=5, message="selected subset excludes primary")
    assert [d.output for d in dock_surfaces(krema)] == [second, third]
    wait_until(
        lambda: [p.output for p in preview_surfaces(krema)] == [second, third],
        message="one correctly aligned preview surface per selected output",
    )
    assert selected_outputs(krema) == [third, second]
    assert not has_state(krema.wait_for(f"{SETTINGS}//check_box[@name='{first}']"), "checked")
    retained = next(d for d in dock_surfaces(krema) if d.output == third)
    retained_preview = next(p for p in preview_surfaces(krema) if p.output == third)

    set_output_selected(krema, second, False)
    wait_until(lambda: mapped_dock_xs(krema) == [2 * W], timeout=5, message="deselected output removed from subset")
    assert dock_surfaces(krema)[0].internal_id == retained.internal_id, "selection update rebuilt unrelated dock"
    wait_until(
        lambda: len(previews := preview_surfaces(krema)) == 1 and previews[0].internal_id == retained_preview.internal_id,
        message="selection update retains unrelated preview and removes deselected preview",
    )
    set_output_selected(krema, second, True)
    wait_until(lambda: mapped_dock_xs(krema) == [W, 2 * W], timeout=5, message="selected subset restored")
    assert next(d for d in dock_surfaces(krema) if d.output == third).internal_id == retained.internal_id
    wait_until(lambda: selected_outputs(krema) == [third, second], message="exact ordered selected names autosaved")
    assert [w.internal_id for w in settings_windows(krema)] == [win.internal_id]
    pointer_to_center()
    wait_until(
        lambda: len(items := krema.find_all(ALPHA_ITEMS)) == 2 and all(has_state(i, "showing") for i in items),
        message="restored selected dock inherits open Settings lock",
    )
    holds(
        lambda: len(items := krema.find_all(ALPHA_ITEMS)) == 2 and all(has_state(i, "showing") for i in items),
        1.0,
        "selected subset hid while Settings was still open",
    )

    close_settings(krema)
    pointer_to_center()
    wait_until(
        lambda: len(items := krema.find_all(ALPHA_ITEMS)) == 2 and all(not has_state(i, "showing") for i in items),
        timeout=5,
        message="both selected docks auto-hide after Settings closes",
    )
    # Pointer on unselected primary output: first live compositor output
    # wins, even though the saved selection starts with output 3.
    invoke_shortcut("focus-dock")
    wait_until(
        lambda: krema.find(TOOLBAR_XPATH + "/button[contains(@states, 'focused')]"),
        message="a selected dock button focused from unselected output",
    )
    target = next(d for d in dock_surfaces(krema) if d.output == second)
    wait_until(
        lambda: (active := kwin.active_window()) is not None and active.internal_id == target.internal_id,
        message="Focus Dock selects output 2 rather than saved-list-first output 3",
    )
    assert mapped_dock_xs(krema) == [W, 2 * W]
    inp.key("Escape")
    wait_until(lambda: krema.focused_item() is None, message="Escape leaves selected dock keyboard navigation")


# ------------------------------------------------------------------- SET-015
RESERVE_SWITCH = f"{SETTINGS}//check_box[@name='Reserve screen space']"


@pytest.mark.parametrize(
    "reserved",
    [
        pytest.param(False, marks=pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": kcfg.ALWAYS_VISIBLE, "ReserveScreenSpace": False}), id="off"),
        pytest.param(True, marks=pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": kcfg.ALWAYS_VISIBLE, "ReserveScreenSpace": True}), id="on"),
    ],
)
def test_set015_reservation_switch_is_native_conditional_and_autosaves(
    krema: Krema, apps: TestWindows, reserved: bool
) -> None:
    apps.open("Alpha")
    krema.wait_for_item("Alpha")
    open_settings(krema)
    open_page(krema, "Behavior")
    switch = scroll_into_view(krema, RESERVE_SWITCH)
    assert has_state(switch, "checked") == reserved
    assert "Toggle" in atspi_actions(krema, "check box", "Reserve screen space")
    pid = krema.pid
    click_el(krema, switch)
    wait_until(
        lambda: has_state(krema.wait_for(RESERVE_SWITCH), "checked") == (not reserved),
        message="reservation switch toggled",
    )
    wait_until(
        lambda: kcfg.as_bool(config_value(krema, "ReserveScreenSpace") or "true") == (not reserved),
        message="reservation change autosaved before Settings closes",
    )
    for mode in ("Auto hide", "Dodge windows"):
        choose(krema, "Visibility mode", mode)
        assert not any(has_state(row, "showing") for row in krema.find_all(RESERVE_SWITCH)), (
            f"reservation switch must not be available in {mode}"
        )
        assert kcfg.as_bool(config_value(krema, "ReserveScreenSpace") or "true") == (not reserved)
    choose(krema, "Visibility mode", "Always visible")
    assert has_state(scroll_into_view(krema, RESERVE_SWITCH), "checked") == (not reserved)
    assert krema.pid == pid, "changing visibility policy must not restart Krema or discard the reservation preference"
