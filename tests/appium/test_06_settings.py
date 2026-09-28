# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""E2E automation of tests/e2e/scenarios/06-settings.md (SET-001..SET-010).

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
  options are ``list_item`` elements without AT-SPI actions, so they are
  clicked with the real pointer.
* Rows below the fold have no ``showing`` state and a 0x0 rect until the page
  is wheel-scrolled.
* The QColorDialog is a separate toplevel ``frame[@name='Choose tint color']``.

SET-008 needs two outputs and runs in its own session:
``KREMA_E2E_OUTPUT_COUNT=2 tests/appium/run-e2e.sh test_06_settings.py``.
"""

from __future__ import annotations

import time
from pathlib import Path
from typing import Callable

import pyatspi
import pytest
from PIL import Image

from krema_e2e import config as kcfg
from krema_e2e import env, kwin
from krema_e2e import input as inp
from krema_e2e.krema import SETTINGS_STACK_XPATH, TOOLBAR_XPATH, Krema, Rect, context_menu_entries, has_state, painted_rect
from krema_e2e.shortcuts import invoke_shortcut
from krema_e2e.waits import WaitTimeout, wait_stable, wait_until
from krema_e2e.windows import TestWindows

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


def dock_surfaces(krema: Krema) -> list[kwin.Window]:
    """Mapped dock surfaces of this krema (layer-shell, full output width,
    anchored to the bottom edge), sorted left to right."""
    out = [
        w
        for w in krema.windows()
        if w.skip_taskbar and not w.desktops and w.client_width == W and w.client_y + w.client_height == H
    ]
    return sorted(out, key=lambda w: w.client_x)


def click_el(krema: Krema, el, surface: str = "settings", button: str = "left") -> None:
    inp.click(*krema.screen_rect(el, surface).center, button=button)


def holds(predicate: Callable[[], bool], duration: float, message: str) -> None:
    """Assert ``predicate`` stays true for ``duration`` seconds (polled)."""
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        assert predicate(), message
        time.sleep(0.05)


def scroll_into_view(krema: Krema, xpath: str):
    """Wheel-scroll the settings page until ``xpath`` is fully visible."""
    page = krema.wait_for(SETTINGS_STACK_XPATH)
    view = Rect.of(page)
    for _ in range(60):
        el = krema.wait_for(xpath)
        r = Rect.of(el)
        if has_state(el, "showing") and r.width and r.y >= view.y and r.y + r.height <= view.y + view.height:
            return el
        below = r.width == 0 or r.y + r.height > view.y + view.height
        # Wheel over the page centre; the left margin did not reliably scroll.
        # Sliders/spin boxes ignore the wheel: QQC2 Control.wheelEnabled
        # defaults to false.
        inp.scroll(*krema.to_screen(Rect(view.x + view.width // 2, view.y + view.height // 2, 1, 1), "settings")[:2], dy=60 if below else -60)
        time.sleep(0.1)
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
    opt = krema.wait_for(f"{SETTINGS}/dialog//list_item[@name='{option}']")
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
    dialog is used, or it takes the wheel and clicks aimed at the dialog. The
    dock first finishes re-centring for krema's own new Settings item: a
    leave that sweeps over the dock and preview while the icons still move
    under the stale pointer position was seen to leave the preview open
    afterwards (see README "Known krema bugs")."""
    hover_ready(k, name)
    before = len(k.items())
    win = k.open_settings(name, ENTRIES)
    wait_until(lambda: len(k.items()) > before, message="krema's Settings item in the dock")
    wait_stable(lambda: [Rect.of(e) for e in k.items()], duration=0.5)
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
    img = Image.open(krema.screenshot(tag)).convert("RGB")
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


#: krema built against LayerShellQt < 6.4 (KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE)
#: cannot resize its dock surface at runtime.
RESIZE_DEADLOCK = pytest.mark.xfail(
    env.LAYERSHELLQT_VERSION < (6, 4),
    strict=True,
    raises=WaitTimeout,
    reason=(
        "krema bug: on the KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE path (LayerShellQt < 6.4) "
        "WaylandDockPlatform::setSize() calls QWindow::resize(QSize(0, h)) after DockView::updateSize() "
        "set the width to the screen width, so the dock window ends up 0 px wide and Qt Quick stops "
        "rendering it. The layer surface's set_size(0, h) is never committed (WAYLAND_DEBUG: no "
        "wl_surface.commit after it), KWin sends no configure and the surface stays at its old size: "
        "icon size and screen edge changes never reach the screen until krema restarts"
    ),
)


# ------------------------------------------------------------------- SET-002
@RESIZE_DEADLOCK
@pytest.mark.kremarc({"PinnedLaunchers": [], "MaxZoomFactor": 1.6})
def test_set002_icon_size_spinbox_resizes_dock_live_and_keeps_zoom_proportion(krema: Krema, apps: TestWindows) -> None:
    max_zoom = 1.6
    apps.open("Alpha")
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
        return wait_stable(lambda: zoomed_width(krema, "Alpha", rest))

    rest = wait_stable(lambda: Rect.of(krema.item("Alpha")))
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
    zoomed_after = peak_width(wait_stable(lambda: Rect.of(krema.item("Alpha"))))
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
    choose(krema, "Style", "Acrylic")
    open_page(krema, "Behavior")
    choose(krema, "Visibility mode", "Auto hide")
    saved = {k: config_value(krema, k) for k in ("IconSize", "BackgroundStyle", "VisibilityMode")}
    assert saved == {"IconSize": "64", "BackgroundStyle": "3", "VisibilityMode": "1"}
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
    wait_until(lambda: current_choice(krema, "Style") == "Acrylic", message="Style shows Acrylic")
    open_page(krema, "Behavior")
    assert current_choice(krema, "Visibility mode") == "Auto hide"


# ------------------------------------------------------------------- SET-006
@RESIZE_DEADLOCK
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
    img = Image.open(krema.screenshot("dock-top")).convert("RGB")
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
    r = wait_stable(second_alpha)
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
@pytest.mark.xfail(
    strict=True,
    raises=WaitTimeout,
    reason="krema bug: Follow active + Mouse trigger never follows the pointer. setShellVisible(false) "
    "unmaps the other screens' docks (view()->hide(), multidockmanager.cpp:363), but the mouse trigger "
    "only reacts to a hover detected by that dock's visibility controller (multidockmanager.cpp:271-281); "
    "an unmapped surface gets no pointer events, so the pointer at the second output's bottom edge for 5 s "
    "leaves no dock surface there and logs no 'Active screen changing'.",
)
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
PARABOLIC, IN_PLACE = "Parabolic - neighbors move aside", "In place - icons overlap"


def hovered_middle_layout(krema: Krema) -> tuple[int, int, list[Rect], list[Rect]]:
    """Hover the middle dock item from rest; returns (index, base size, rest
    rects, drawn rects once the zoom settled)."""
    krema.move_away()
    rest = wait_stable(lambda: [Rect.of(e) for e in krema.items()])
    mid = len(rest) // 2
    krema.hover_item(krema.item_names()[mid])
    drawn = wait_stable(lambda: [painted_rect(Rect.of(e), r0) for e, r0 in zip(krema.items(), rest, strict=True)])
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
    first = krema.wait_for(f"{SETTINGS}/dialog//list_item[@name='{PARABOLIC}']")
    wait_until(lambda: has_state(first, "showing") and Rect.of(first).width > 0, message="zoom style options shown")
    options = [
        n for e in krema.find_all(f"{SETTINGS}/dialog//list_item") if has_state(e, "showing") and (n := e.get_attribute("name")) not in PAGES
    ]
    assert options == [PARABOLIC, IN_PLACE]
    click_el(krema, first)
    wait_until(lambda: current_choice(krema, ZOOM_STYLE) == PARABOLIC, message="combo closed on Parabolic")

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
    for _ in range(6):
        inp.key("right")
    wait_until(lambda: abs(float(config_value(krema, "MaxZoomFactor") or 0) - 1.6) < 1e-6, message="kremarc MaxZoomFactor=1.6")
    wait_until(lambda: has_state(krema.wait_for(row_xpath), "enabled"), message="zoom style combo enabled again")
