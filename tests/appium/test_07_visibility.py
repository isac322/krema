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
from typing import Callable

import pyatspi
import pytest
from PIL import Image, ImageChops, ImageStat

from krema_e2e import config, env, kwin, shortcuts
from krema_e2e import input as inp
from krema_e2e import preview as pv
from krema_e2e.krema import ITEMS_XPATH, PREVIEW_XPATH, Krema, Rect, _xpath_str, has_state
from krema_e2e.waits import WaitTimeout, wait_stable, wait_until
from krema_e2e.windows import TestWindow, TestWindows

# Hide delay + slide + debounce, with headroom for a loaded VM.
SETTLE_TIMEOUT = 5.0
#: Pointer positions: screen centre (away from the dock) and the bottom-edge
#: trigger strip (4 px strip at the bottom of the dock surface).
CENTRE = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)
EDGE = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT - 1)


def _hover_revealed_item(krema: Krema, name: str) -> None:
    """Reach a revealed item without leaving the dock's edge trigger strip."""
    x, y = krema.settled_item_center(name)
    inp.move_path(
        [
            *inp.line(EDGE, (x, EDGE[1]), 3),
            *inp.line((x, EDGE[1]), (x, y), 3),
        ],
        40,
    )


# --------------------------------------------------------------------------- oracles


def _item_state(krema: Krema, name: str) -> tuple[bool, Rect] | None:
    surface = krema.surface_rect("dock")
    if surface is None:
        return None
    item = next(
        (
            el
            for el in krema.find_all(f"{ITEMS_XPATH}[@name={_xpath_str(name)}]")
            if surface.x <= Rect.of(el).center[0] < surface.x + surface.width
        ),
        None,
    )
    return None if item is None else (has_state(item, "showing"), Rect.of(item))


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


#: Pause between slide samples: Kirigami longDuration slides take 200 ms, so
#: this still yields dozens of samples per slide without keeping krema's GUI
#: thread busy answering AT-SPI calls.
SLIDE_SAMPLE_INTERVAL = 0.005


def _slide_samples(krema: Krema, name: str, surface: Rect, until_shown: bool, trigger: Callable[[], None]) -> list[int]:
    """Run ``trigger`` (the pointer input that starts the slide), then sample
    the item's y while the panel slides until it is fully shown (or hidden).
    Reads the same AT-SPI extents and ``showing`` state as
    :func:`_item_state`, but on an in-process accessible looked up before
    ``trigger``: a webdriver lookup serializes krema's whole tree per call
    and, on a loaded runner, takes longer than the whole slide. The samples
    (with their times) go to ``slide-samples.txt`` in the test's artifacts."""
    item = wait_until(lambda: krema.item_accessible(name), message=f"dock item {name!r} over AT-SPI")
    component = item.queryComponent()
    trigger()
    samples: list[tuple[float, int]] = []
    start = time.monotonic()
    deadline = start + SETTLE_TIMEOUT
    while time.monotonic() < deadline:
        item.clear_cache()
        showing = item.getState().contains(pyatspi.STATE_SHOWING)
        r = component.getExtents(pyatspi.XY_SCREEN)
        if until_shown:
            done = showing and r.y >= 0 and r.y + r.height <= surface.height
        else:
            done = not showing and r.y >= surface.height
        if done:
            break
        samples.append((time.monotonic() - start, r.y))
        time.sleep(SLIDE_SAMPLE_INTERVAL)
    env.artifact_path(f"{krema.name}/slide-samples.txt").write_text(
        f"{'show' if until_shown else 'hide'}: {len(samples)} samples in {time.monotonic() - start:.3f}s until done\n"
        + "".join(f"{t:.3f} {y}\n" for t, y in samples)
    )
    return [y for _t, y in samples]


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


def crop(image: Image.Image, rect: Rect) -> Image.Image:
    return image.crop((rect.x, rect.y, rect.x + rect.width, rect.y + rect.height))


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
    area = kwin.bounds(item, control)
    if capture:
        before = krema.screenshot("vis001-before", area)

    maximize(win)
    wait_until(win.is_active, message="maximized window to be active")
    assert_stays(lambda: dock_shown(krema, "Always"), 0.5, "dock shown after maximizing a window")
    if capture:
        # The window is not drawn over the dock: the dock item's pixels are
        # unchanged while the control region above now shows the window.
        after = krema.screenshot("vis001-maximized", area)
        item_diff = mean_diff(crop(before, item), crop(after, item))
        control_diff = mean_diff(crop(before, control), crop(after, control))
        assert control_diff > 20, f"control region did not change ({control_diff:.1f}): window not drawn there?"
        assert item_diff < 8, f"dock item pixels changed ({item_diff:.1f}): the maximized window covers the dock"

    inp.move(*CENTRE)
    assert_stays(lambda: dock_shown(krema, "Always"), 2.0, "dock shown 2 s after moving the pointer to the centre")
    if capture:
        later = krema.screenshot("vis001-pointer-away", area)
        item_diff = mean_diff(crop(before, item), crop(later, item))
        assert item_diff < 8, f"dock item pixels changed ({item_diff:.1f}) with the pointer away"


@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.ALWAYS_VISIBLE, "ReserveScreenSpace": True})
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
    samples = _slide_samples(krema, "Hider", surface, until_shown=False, trigger=lambda: inp.move(*CENTRE))
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
    surface = krema.surface_rect("dock")
    assert surface is not None

    # Real pointer approach from above down to the last pixel row: the
    # trigger strip at the bottom edge catches it and the dock slides in.
    samples = _slide_samples(
        krema, "Edge", surface, until_shown=True, trigger=lambda: inp.move_path(inp.line(CENTRE, EDGE, 10), step_ms=50)
    )
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


@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.AUTO_HIDE})
def test_vis006_keyboard_navigation_keeps_auto_hide_dock_visible(krema: Krema, apps: TestWindows) -> None:
    apps.open("Keys")
    krema.wait_for_item("Keys")
    wait_until(
        lambda: shortcuts.shortcut_keys("focus-dock") == [shortcuts.META_ALT_D],
        message=lambda: f"krema focus-dock bound to Meta+Alt+D (keys: {shortcuts.shortcut_keys('focus-dock')})",
    )
    inp.move(*CENTRE)
    wait_hidden(krema, "Keys", why=" at start with the pointer away")

    inp.key("Meta", "Alt", "d")
    wait_until(lambda: krema.focused_item() == "Keys", message=lambda: f"Meta+Alt+D to focus the dock item (focused: {krema.focused_item()})")
    wait_shown(krema, "Keys", why=" during keyboard navigation")
    krema.wait_keyboard_focus()
    assert kwin.cursor_pos() == CENTRE, "pointer is away from the dock: only keyboard navigation keeps it shown"

    # Well past the hide delay: keyboard navigation locks visibility.
    assert_stays(lambda: dock_shown(krema, "Keys") and krema.focused_item() == "Keys", 5.0, "dock shown during keyboard navigation")

    inp.key("Escape")
    wait_until(lambda: krema.focused_item() is None, message="Escape to end keyboard navigation")
    inp.move(*CENTRE)
    wait_hidden(krema, "Keys", why=" after keyboard navigation ended")


# --------------------------------------------------------------------------- QA-CLK-012


def _visible_preview(krema: Krema):
    """Select the shown popup rather than a hidden other-output popup."""
    return next(
        (popup for popup in krema.find_all(PREVIEW_XPATH) if has_state(popup, "showing") and Rect.of(popup).width > 0),
        None,
    )


@pytest.mark.parametrize("close_mode", ("leave", "thumbnail"))
@pytest.mark.parametrize(
    "hide_mode",
    [
        pytest.param(
            "Auto hide",
            id="auto-hide",
            marks=pytest.mark.kremarc(
                {"PinnedLaunchers": [], "VisibilityMode": config.AUTO_HIDE, "PreviewEnabled": False, "GroupedWindowClickAction": 1}
            ),
        ),
        pytest.param(
            "Dodge windows",
            id="dodge",
            marks=pytest.mark.kremarc(
                {
                    "PinnedLaunchers": [],
                    "VisibilityMode": config.DODGE_WINDOWS,
                    "DodgeActiveOnly": False,
                    "PreviewEnabled": False,
                    "GroupedWindowClickAction": 1,
                }
            ),
        ),
        pytest.param(
            "Only dodge active window",
            id="smart-hide",
            marks=pytest.mark.kremarc(
                {
                    "PinnedLaunchers": [],
                    "VisibilityMode": config.DODGE_WINDOWS,
                    "DodgeActiveOnly": True,
                    "PreviewEnabled": False,
                    "GroupedWindowClickAction": 1,
                }
            ),
        ),
    ],
)
def test_clk012_repeated_explicit_preview_releases_visibility_hold(
    krema: Krema, apps: TestWindows, hide_mode: str, close_mode: str
) -> None:
    alpha = apps.open("Alpha")
    beta = apps.open("Beta")
    foreground = apps.open("Overlap", app_id=env.TEST_APP2_ID)
    set_geometry(alpha, Rect(40, 40, 400, 300))
    set_geometry(beta, Rect(460, 40, 400, 300))
    # Overlap the dock without covering the popup's whole painted-background oracle.
    set_geometry(foreground, Rect(0, env.SCREEN_HEIGHT - 120, env.SCREEN_WIDTH, 120))
    kwin.activate(foreground.internal_id)
    wait_until(foreground.is_active, message="overlapping window active")
    krema.wait_for_item(env.TEST_APP_NAME)
    inp.move(*CENTRE)
    wait_hidden(krema, env.TEST_APP_NAME, why=f" before explicit preview in {hide_mode}")
    inp.move(*EDGE)
    wait_shown(krema, env.TEST_APP_NAME)
    _hover_revealed_item(krema, env.TEST_APP_NAME)

    for _ in range(3):
        krema.click_item(env.TEST_APP_NAME)
        wait_until(lambda: _visible_preview(krema), message="explicit group popup visible after each click")
        assert foreground.is_active(), "showing previews must not activate or minimize a child"
    wait_until(lambda: set(pv.thumb_titles(krema)) == {"Alpha", "Beta"}, message="both group children previewed")
    popup = _visible_preview(krema)
    pv.wait_on_screen(krema, popup)
    pv.glide_into(krema, pv.screen_rect(krema, krema.wait_for(pv.thumb_xpath("Alpha") + "[contains(@states, 'showing')]")).center)
    assert_stays(
        lambda: _visible_preview(krema) is not None and dock_shown(krema, env.TEST_APP_NAME),
        1.0,
        f"{hide_mode} dock and popup held while the pointer is in the popup, outside the dock",
    )

    if close_mode == "thumbnail":
        inp.click()
        wait_until(alpha.is_active, message="thumbnail activates Alpha")
    inp.move(*CENTRE)
    wait_until(lambda: _visible_preview(krema) is None, message=f"popup closes after {close_mode}")
    kwin.activate(foreground.internal_id)
    wait_until(foreground.is_active, message="overlapping window active after popup closes")
    wait_hidden(krema, env.TEST_APP_NAME, why=f" after repeated explicit popup closes in {hide_mode}")
    assert_stays(lambda: dock_hidden(krema, env.TEST_APP_NAME), 1.0, f"{hide_mode} resumes after the popup releases its hold")

    # The released hold must leave the normal reveal/hide lifecycle usable.
    inp.move(*EDGE)
    wait_shown(krema, env.TEST_APP_NAME)
    inp.move(*CENTRE)
    wait_hidden(krema, env.TEST_APP_NAME, why=" after the subsequent edge reveal")


def _mapped_dock_xs(krema: Krema) -> list[int]:
    """Mapped bottom docks, excluding the inactive output's 4 px edge trigger."""
    return sorted(
        w.client_x
        for w in krema.windows()
        if w.skip_taskbar
        and not w.desktops
        and w.client_width == env.SCREEN_WIDTH
        and w.client_y + w.client_height == env.SCREEN_HEIGHT
        and w.client_height > 4
    )


@pytest.mark.outputs(2)
@pytest.mark.parametrize("close_mode", ("leave", "thumbnail"))
@pytest.mark.parametrize(
    "follow_trigger",
    [
        pytest.param(
            trigger,
            id=name,
            marks=pytest.mark.kremarc(
                {
                    "PinnedLaunchers": [],
                    "VisibilityMode": config.AUTO_HIDE,
                    "MonitorMode": 2,
                    "FollowActiveTrigger": trigger,
                    "ScreenTransition": 2,
                    "PreviewEnabled": False,
                    "GroupedWindowClickAction": 1,
                }
            ),
        )
        for trigger, name in ((0, "mouse"), (1, "focus"))
    ],
)
def test_clk012_repeated_explicit_preview_releases_follow_active_screen_hold(
    krema: Krema, apps: TestWindows, follow_trigger: int, close_mode: str
) -> None:
    width, height = env.SCREEN_WIDTH, env.SCREEN_HEIGHT
    alpha = apps.open("Alpha")
    apps.open("Beta")
    other = apps.open("Other output", app_id=env.TEST_APP2_ID)
    set_geometry(other, Rect(width + 300, 200, 400, 300))
    kwin.activate(alpha.internal_id)
    wait_until(alpha.is_active, message="primary-output group child active")
    wait_until(lambda: _mapped_dock_xs(krema) == [0], message="follow-active dock on the primary output")
    krema.wait_for_item(env.TEST_APP_NAME)
    inp.move(*CENTRE)
    wait_hidden(krema, env.TEST_APP_NAME)
    inp.move(*EDGE)
    wait_shown(krema, env.TEST_APP_NAME)
    _hover_revealed_item(krema, env.TEST_APP_NAME)
    for _ in range(3):
        krema.click_item(env.TEST_APP_NAME)
        wait_until(lambda: _visible_preview(krema), message="repeated explicit popup remains open")
        assert alpha.is_active(), "explicit preview must preserve the current window"
    popup = _visible_preview(krema)
    pv.wait_on_screen(krema, popup)
    pv.glide_into(krema, pv.screen_rect(krema, krema.wait_for(pv.thumb_xpath("Alpha") + "[contains(@states, 'showing')]")).center)
    assert_stays(
        lambda: _visible_preview(krema) is not None and dock_shown(krema, env.TEST_APP_NAME) and _mapped_dock_xs(krema) == [0],
        1.0,
        "popup interaction holds the shown follow-active dock",
    )
    if close_mode == "thumbnail":
        inp.click()
        wait_until(alpha.is_active, message="thumbnail keeps Alpha active")
    inp.move(*CENTRE)
    wait_until(lambda: _visible_preview(krema) is None, message=f"follow-active popup closes after {close_mode}")
    wait_hidden(krema, env.TEST_APP_NAME, why=" after the explicit popup releases visibility")

    if follow_trigger == 1:
        kwin.activate(other.internal_id)
        wait_until(other.is_active, message="window on the second output active")
        wait_until(lambda: _mapped_dock_xs(krema) == [width], message="dock follows focus after popup closes")
    inp.move_path(inp.line(CENTRE, (width + width // 2, height - 1), 10))
    wait_until(lambda: _mapped_dock_xs(krema) == [width], message="second-output dock available after popup closes")
    wait_shown(krema, env.TEST_APP_NAME, why=" on the second output")
    assert_stays(lambda: dock_shown(krema, env.TEST_APP_NAME), 1.0, "second-output edge can show the dock after the hold releases")
    inp.move(width + width // 2, height // 2)
    wait_hidden(krema, env.TEST_APP_NAME, why=" after leaving the second-output dock")
