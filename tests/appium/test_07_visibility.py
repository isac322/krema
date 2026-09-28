# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Scenario 07 (tests/e2e/scenarios/07-visibility.md): visibility modes.

Oracles:

* Dock shown/hidden: the dock item's AT-SPI ``showing`` state together with
  its surface-local rect. The panel slides on the edge axis inside the
  fixed-size layer surface (src/qml/main.qml ``_panelEdgePos``): shown, the
  item lies inside the surface; hidden, it is pushed below the surface's
  bottom edge and loses ``showing``.
* Window geometry, stacking and activation: KWin scripting.
* Where the expectation is visual (the dock is not covered): a screenshot
  crop of the dock item compared against the same crop with the dock shown
  and no window around.

Timings (src/shell/dockvisibilitycontroller.cpp, krema.kcfg): show dwell
200 ms (ShowDelay), hide delay 400 ms (HideDelay), window-change debounce
300 ms, slide/fade Kirigami.Units.longDuration (250 ms).
"""

from __future__ import annotations

import time

import pytest
from PIL import Image, ImageChops, ImageStat

from krema_e2e import config, env, kwin, shortcuts
from krema_e2e import input as inp
from krema_e2e.krema import Krema, Rect, has_state
from krema_e2e.waits import WaitTimeout, wait_stable, wait_until
from krema_e2e.windows import TestWindow, TestWindows

# Hide delay + slide + debounce, with headroom for a loaded VM.
SETTLE_TIMEOUT = 5.0
#: Pointer positions: screen centre (away from the dock) and the bottom-edge
#: trigger strip (4 px strip at the bottom of the dock surface).
CENTRE = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)
EDGE = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT - 1)

# Qt key combos (QKeyCombination::toCombined()) for Meta+F5.
_META_F5 = 0x10000000 | 0x01000034


# --------------------------------------------------------------------------- oracles


def _item_state(krema: Krema, name: str) -> tuple[bool, Rect] | None:
    item = krema.item(name)
    if item is None:
        return None
    return has_state(item, "showing"), Rect.of(item)


def dock_shown(krema: Krema, name: str) -> bool:
    """The dock panel is fully slid in: the item is ``showing`` and lies
    inside the dock surface."""
    state = _item_state(krema, name)
    surface = krema.surface_rect("dock")
    if state is None or surface is None:
        return False
    showing, r = state
    return showing and r.y >= 0 and r.y + r.height <= surface.height


def dock_hidden(krema: Krema, name: str) -> bool:
    """The dock panel is fully slid out: the item is not ``showing`` and
    lies entirely below the dock surface's bottom edge."""
    state = _item_state(krema, name)
    surface = krema.surface_rect("dock")
    if state is None or surface is None:
        return False
    showing, r = state
    return not showing and r.y >= surface.height


def wait_shown(krema: Krema, name: str, timeout: float = SETTLE_TIMEOUT, why: str = "") -> None:
    wait_until(
        lambda: dock_shown(krema, name),
        timeout=timeout,
        message=lambda: f"dock to be shown{why} (item state: {_item_state(krema, name)}, surface: {krema.surface_rect('dock')})",
    )


def wait_hidden(krema: Krema, name: str, timeout: float = SETTLE_TIMEOUT, why: str = "") -> None:
    wait_until(
        lambda: dock_hidden(krema, name),
        timeout=timeout,
        message=lambda: f"dock to be hidden{why} (item state: {_item_state(krema, name)}, surface: {krema.surface_rect('dock')})",
    )


def assert_stays(predicate, duration: float, what: str) -> None:
    """``predicate`` holds on every poll for ``duration`` seconds."""
    try:
        wait_until(lambda: not predicate(), timeout=duration, interval=0.05)
    except WaitTimeout:
        return
    raise AssertionError(f"{what} did not hold for {duration}s")


# --------------------------------------------------------------------------- KWin setup helpers


def _find_js(tw: TestWindow) -> str:
    return (
        f"const w = workspace.windowList().find(w => w.pid === {tw.pid} && w.normalWindow);"
        "if (!w) throw new Error('window not found');"
    )


def set_geometry(tw: TestWindow, rect: Rect) -> kwin.Window:
    """Move/resize a fixture window through KWin and wait until KWin reports
    the new frame geometry."""
    kwin.evaluate(
        _find_js(tw)
        + f"w.frameGeometry = {{x: {rect.x}, y: {rect.y}, width: {rect.width}, height: {rect.height}}};"
        "report(true);"
    )
    return wait_until(
        lambda: (w := tw.refresh()) is not None and (w.x, w.y, w.width, w.height) == tuple(rect) and w,
        timeout=5,
        message=lambda: f"{tw.title!r} frame geometry to become {rect} (now {tw.refresh()})",
    )


def maximize(tw: TestWindow) -> kwin.Window:
    """Maximize a fixture window through KWin and wait until its geometry
    settles on the maximized size."""
    before = tw.refresh()
    kwin.evaluate(_find_js(tw) + "w.setMaximize(true, true); report(true);")
    wait_until(
        lambda: (w := tw.refresh()) is not None and before is not None and (w.width, w.height) != (before.width, before.height),
        timeout=5,
        message=lambda: f"{tw.title!r} to be maximized (now {tw.refresh()})",
    )
    return wait_stable(tw.refresh)


def rects_intersect(a: Rect, b: Rect) -> bool:
    return a.x < b.x + b.width and b.x < a.x + a.width and a.y < b.y + b.height and b.y < a.y + a.height


def frame(w: kwin.Window) -> Rect:
    return Rect(w.x, w.y, w.width, w.height)


def settle_item_rect(krema: Krema, name: str) -> Rect:
    """Screen rect of a dock item once its slide/zoom animation settled."""
    wait_stable(lambda: Rect.of(krema.item(name)))
    return krema.screen_rect(krema.item(name))


def crop(path, rect: Rect) -> Image.Image:
    return Image.open(path).convert("RGB").crop((rect.x, rect.y, rect.x + rect.width, rect.y + rect.height))


def mean_diff(a: Image.Image, b: Image.Image) -> float:
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3


# --------------------------------------------------------------------------- VIS-001


# Opaque panel: the window behind a translucent one would change the item's
# pixels even though the dock stays on top.
@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.ALWAYS_VISIBLE, "BackgroundOpacity": 1.0})
def test_vis001_always_visible_dock_stays_shown_over_a_maximized_window(krema: Krema, apps: TestWindows) -> None:
    win = apps.open("Always", width=400, height=300)
    krema.wait_for_item("Always")
    set_geometry(win, Rect(40, 40, 400, 300))
    inp.move(*CENTRE)
    wait_shown(krema, "Always", why=" in AlwaysVisible mode")
    item = settle_item_rect(krema, "Always")
    # Control region: same size, just above the dock surface, where the
    # maximized window certainly draws.
    surface = krema.surface_rect("dock")
    control = Rect(item.x, surface.y - item.height - 10, item.width, item.height)
    capture = kwin.can_capture()
    if capture:
        before = krema.screenshot("vis001-before")

    maximize(win)
    wait_until(win.is_active, message="maximized window to be active")
    assert_stays(lambda: dock_shown(krema, "Always"), 0.5, "dock shown after maximizing a window")
    if capture:
        # The window is not drawn over the dock: the dock item's pixels are
        # unchanged while the control region above now shows the window.
        after = krema.screenshot("vis001-maximized")
        item_diff = mean_diff(crop(before, item), crop(after, item))
        control_diff = mean_diff(crop(before, control), crop(after, control))
        assert control_diff > 20, f"control region did not change ({control_diff:.1f}): window not drawn there?"
        assert item_diff < 8, f"dock item pixels changed ({item_diff:.1f}): the maximized window covers the dock"

    inp.move(*CENTRE)
    assert_stays(lambda: dock_shown(krema, "Always"), 2.0, "dock shown 2 s after moving the pointer to the centre")
    if capture:
        later = krema.screenshot("vis001-pointer-away")
        item_diff = mean_diff(crop(before, item), crop(later, item))
        assert item_diff < 8, f"dock item pixels changed ({item_diff:.1f}) with the pointer away"


@pytest.mark.xfail(
    strict=True,
    reason="krema bug: AlwaysVisible reserves no exclusive zone. WaylandDockPlatform::setVisibilityMode(AlwaysVisible) "
    "applies m_exclusiveZone only if > 0, but nothing ever sets a positive zone (only MultiDockManager sets -1/0), "
    "so KWin maximizes windows to the full 1024x768 underneath the dock panel",
)
@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.ALWAYS_VISIBLE})
def test_vis001_always_visible_reserves_the_dock_area_for_maximized_windows(krema: Krema, apps: TestWindows) -> None:
    win = apps.open("Reserve", width=400, height=300)
    krema.wait_for_item("Reserve")
    inp.move(*CENTRE)
    wait_shown(krema, "Reserve", why=" in AlwaysVisible mode")
    # The panel (icon + padding) is what an exclusive zone must reserve; the
    # icon's top edge lies inside it, so the window must end above it.
    icon = settle_item_rect(krema, "Reserve")

    maximized = maximize(win)
    assert dock_shown(krema, "Reserve")
    assert maximized.y + maximized.height <= icon.y, (
        f"maximized window {frame(maximized)} extends under the dock item {icon}: no exclusive zone reserved"
    )


# --------------------------------------------------------------------------- VIS-002


@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.AUTO_HIDE})
def test_vis002_auto_hide_hides_after_timeout_and_frees_the_screen(krema: Krema, apps: TestWindows) -> None:
    win = apps.open("Hider")
    krema.wait_for_item("Hider")
    inp.move(*CENTRE)
    wait_hidden(krema, "Hider", why=" at start with the pointer away")

    # Pointer onto the dock area (edge trigger strip) shows it.
    inp.move(*EDGE)
    wait_shown(krema, "Hider", why=" with the pointer on the dock area")
    shown_y = wait_stable(lambda: Rect.of(krema.item("Hider"))).y
    surface = krema.surface_rect("dock")

    # Pointer away: after the hide delay the panel slides out.
    inp.move(*CENTRE)
    samples: list[int] = []
    deadline = time.monotonic() + SETTLE_TIMEOUT
    while time.monotonic() < deadline and not dock_hidden(krema, "Hider"):
        samples.append(Rect.of(krema.item("Hider")).y)
    assert dock_hidden(krema, "Hider"), f"dock did not hide within {SETTLE_TIMEOUT}s (y samples {samples})"
    hidden_y = Rect.of(krema.item("Hider")).y
    assert hidden_y >= surface.height
    # Slide animation: the item passed through intermediate positions.
    assert any(shown_y < y < hidden_y for y in samples), f"no intermediate slide positions between {shown_y} and {hidden_y}: {samples}"
    assert_stays(lambda: dock_hidden(krema, "Hider"), 1.0, "dock hidden with the pointer away")

    # Dock area reclaimed: a maximized window uses the full screen height.
    maximized = maximize(win)
    assert (maximized.y, maximized.height) == (0, env.SCREEN_HEIGHT), f"maximized window does not use the full screen: {maximized}"


# --------------------------------------------------------------------------- VIS-003


@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.AUTO_HIDE})
def test_vis003_auto_hide_shows_on_screen_edge_approach(krema: Krema, apps: TestWindows) -> None:
    apps.open("Edge")
    krema.wait_for_item("Edge")
    inp.move(*CENTRE)
    wait_hidden(krema, "Edge", why=" at start with the pointer away")
    hidden_y = Rect.of(krema.item("Edge")).y

    # Real pointer approach from above down to the last pixel row: the
    # trigger strip at the bottom edge catches it and the dock slides in.
    inp.move_path(inp.line(CENTRE, EDGE, 10), step_ms=50)
    samples: list[int] = []
    deadline = time.monotonic() + SETTLE_TIMEOUT
    while time.monotonic() < deadline and not dock_shown(krema, "Edge"):
        samples.append(Rect.of(krema.item("Edge")).y)
    assert dock_shown(krema, "Edge"), f"dock did not show on edge approach within {SETTLE_TIMEOUT}s (y samples {samples})"
    shown_y = Rect.of(krema.item("Edge")).y
    assert any(shown_y < y < hidden_y for y in samples), f"no intermediate slide positions between {hidden_y} and {shown_y}: {samples}"

    # It stays visible while the pointer remains in the dock area, well
    # past the 400 ms hide delay.
    assert_stays(lambda: dock_shown(krema, "Edge"), 2.0, "dock shown with the pointer in the dock area")
    assert kwin.cursor_pos() == EDGE

    # Leaving hides it again (auto-hide resumes).
    inp.move(*CENTRE)
    wait_hidden(krema, "Edge", why=" after leaving the edge")


# --------------------------------------------------------------------------- VIS-004


@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.DODGE_WINDOWS, "DodgeActiveOnly": False})
def test_vis004_dodge_windows_hides_while_a_window_overlaps_the_dock(krema: Krema, apps: TestWindows) -> None:
    win = apps.open("Dodger")
    krema.wait_for_item("Dodger")
    inp.move(*CENTRE)
    away = set_geometry(win, Rect(40, 40, 400, 300))
    wait_shown(krema, "Dodger", why=" with no window overlapping")
    item = settle_item_rect(krema, "Dodger")
    assert not rects_intersect(frame(away), item)
    assert_stays(lambda: dock_shown(krema, "Dodger"), 1.0, "dock shown with no overlapping window")

    # Move/resize the window over the dock area.
    over = set_geometry(win, Rect(0, env.SCREEN_HEIGHT - 400, env.SCREEN_WIDTH, 400))
    assert rects_intersect(frame(over), item)
    wait_hidden(krema, "Dodger", why=" while a window overlaps it")
    assert_stays(lambda: dock_hidden(krema, "Dodger"), 1.0, "dock hidden while the window overlaps")

    # Move it away again.
    back = set_geometry(win, Rect(40, 40, 400, 300))
    assert not rects_intersect(frame(back), item)
    wait_shown(krema, "Dodger", why=" after the window moved away")


@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.DODGE_WINDOWS, "DodgeActiveOnly": False})
def test_vis004_dodge_windows_hides_for_an_inactive_overlapping_window(krema: Krema, apps: TestWindows) -> None:
    """DodgeWindows reacts to ANY overlapping window, not only the active one."""
    over = apps.open("Overlapper")
    front = apps.open("Front", app_id=env.TEST_APP2_ID)
    krema.wait_for_item("Front")
    inp.move(*CENTRE)
    set_geometry(front, Rect(40, 40, 400, 300))
    set_geometry(over, Rect(40, 40, 400, 300))
    kwin.activate(front.internal_id)
    wait_until(front.is_active, message="non-overlapping window to be active")
    wait_shown(krema, "Front", why=" with no window overlapping")

    set_geometry(over, Rect(0, env.SCREEN_HEIGHT - 400, env.SCREEN_WIDTH, 400))
    assert front.is_active() and not over.is_active()
    wait_hidden(krema, "Front", why=" while an inactive window overlaps it")


# --------------------------------------------------------------------------- VIS-005


@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.DODGE_WINDOWS, "DodgeActiveOnly": True})
def test_vis005_smart_hide_hides_only_for_the_active_overlapping_window(krema: Krema, apps: TestWindows) -> None:
    near = apps.open("Near", app_id=env.TEST_APP_ID)
    over = apps.open("Over", app_id=env.TEST_APP2_ID)
    krema.wait_for_item("Near")
    krema.wait_for_item("Over")
    inp.move(*CENTRE)
    set_geometry(near, Rect(40, 40, 400, 300))
    overlap = set_geometry(over, Rect(0, env.SCREEN_HEIGHT - 400, env.SCREEN_WIDTH, 400))

    # Activate the window that does NOT overlap: the dock is visible even
    # though the (now inactive) other window overlaps it.
    kwin.activate(near.internal_id)
    wait_until(near.is_active, message="non-overlapping window to be active")
    wait_shown(krema, "Near", why=" with only an inactive window overlapping")
    item = settle_item_rect(krema, "Near")
    assert rects_intersect(frame(overlap), item)
    assert_stays(lambda: dock_shown(krema, "Near"), 1.0, "dock shown while the overlapping window is inactive")

    # Activate the overlapping window: the dock hides.
    kwin.activate(over.internal_id)
    wait_until(over.is_active, message="overlapping window to be active")
    wait_hidden(krema, "Near", why=" with the active window overlapping")
    assert_stays(lambda: dock_hidden(krema, "Near"), 1.0, "dock hidden while the active window overlaps")

    # And back.
    kwin.activate(near.internal_id)
    wait_until(near.is_active, message="non-overlapping window to be active again")
    wait_shown(krema, "Near", why=" after the non-overlapping window became active again")


# --------------------------------------------------------------------------- VIS-006


@pytest.fixture
def real_meta_f5():
    """Let a real Meta+F5 reach krema: KWin's own MoveMouseToFocus binding
    on the same key wins otherwise (README, investigation 2). Restored after
    the test because KWin outlives it."""
    old = shortcuts.shortcut_keys("MoveMouseToFocus", component="kwin")
    shortcuts.set_shortcut_keys("MoveMouseToFocus", [], component="kwin")
    try:
        yield
    finally:
        shortcuts.set_shortcut_keys("MoveMouseToFocus", old, component="kwin")


@pytest.mark.xfail(shortcuts.FOCUS_DOCK_KEY_DROPPED, strict=True, raises=WaitTimeout, reason=shortcuts.FOCUS_DOCK_KEY_DROPPED_REASON)
@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.AUTO_HIDE})
def test_vis006_keyboard_navigation_keeps_auto_hide_dock_visible(krema: Krema, apps: TestWindows, real_meta_f5) -> None:
    apps.open("Keys")
    krema.wait_for_item("Keys")
    wait_until(
        lambda: shortcuts.shortcut_keys("focus-dock") == [_META_F5],
        message=lambda: f"krema focus-dock bound to Meta+F5 (keys: {shortcuts.shortcut_keys('focus-dock')})",
    )
    inp.move(*CENTRE)
    wait_hidden(krema, "Keys", why=" at start with the pointer away")

    inp.key("Meta", "F5")
    wait_until(lambda: krema.focused_item() == "Keys", message=lambda: f"Meta+F5 to focus the dock item (focused: {krema.focused_item()})")
    wait_shown(krema, "Keys", why=" during keyboard navigation")
    krema.wait_keyboard_focus()
    assert kwin.cursor_pos() == CENTRE, "pointer is away from the dock: only keyboard navigation keeps it shown"

    # Well past the hide delay: keyboard navigation locks visibility.
    assert_stays(lambda: dock_shown(krema, "Keys") and krema.focused_item() == "Keys", 5.0, "dock shown during keyboard navigation")

    inp.key("Escape")
    wait_until(lambda: krema.focused_item() is None, message="Escape to end keyboard navigation")
    inp.move(*CENTRE)
    wait_hidden(krema, "Keys", why=" after keyboard navigation ended")
