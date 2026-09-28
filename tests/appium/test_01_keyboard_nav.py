# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""tests/e2e/scenarios/01-keyboard-nav.md (KBD-001..KBD-009): dock and
preview keyboard navigation driven by real key presses, checked through the
AT-SPI ``focused``/``showing`` states and KWin's window list.

The preview-entry key points away from the dock edge (src/qml/main.qml,
``previewKey``): Down on a top dock, Up on a bottom dock. The scenario's
steps press Down, so the preview tests put the dock on the top edge.
"""

from __future__ import annotations

import time
from typing import Callable

import pytest
from PIL import Image

from krema_e2e import config, env, kwin
from krema_e2e import input as inp
from krema_e2e.krema import Krema, Rect, has_state, painted_rect
from krema_e2e.shortcuts import FOCUS_DOCK_KEY_DROPPED, FOCUS_DOCK_KEY_DROPPED_REASON, invoke_shortcut, set_shortcut_keys, shortcut_keys
from krema_e2e.waits import WaitTimeout, wait_stable, wait_until
from krema_e2e.windows import TestWindows

# Three distinct app ids so every window is its own (ungrouped) dock item,
# named after the window title.
APP_IDS = (env.TEST_APP_ID, env.TEST_APP2_ID, "org.kde.kwrite")

TOP = {"Edge": config.EDGE_TOP}

#: Screen centre (the scenario's "mouse_move x=400 y=300" target area).
CENTRE = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)
#: Neutral pointer spot: right edge, mid-height. Off the dock, its edge
#: trigger strip and the preview's input region (which is centred on the
#: dock item and much deeper than the visible popup, see KBD-007 xfail).
PARK = (env.SCREEN_WIDTH - 10, env.SCREEN_HEIGHT // 2)


# --------------------------------------------------------------------- helpers
def park_pointer() -> None:
    """Put the pointer where no krema surface receives it before keyboard
    navigation starts."""
    inp.move(*PARK)


def open_items(krema: Krema, apps: TestWindows, titles: list[str]) -> None:
    for title, app_id in zip(titles, APP_IDS):
        apps.open(title, app_id=app_id)
    for title in titles:
        krema.wait_for_item(title)
    wait_until(lambda: krema.item_names() == titles, message=f"dock items {titles} in order")


def focused_items(krema: Krema) -> list[str]:
    return [e.get_attribute("name") for e in krema.items() if has_state(e, "focused")]


def wait_focused(krema: Krema, name: str) -> None:
    wait_until(lambda: focused_items(krema) == [name], message=f"only {name!r} to be focused (have {focused_items(krema)})")


def item_rects(krema: Krema) -> dict[str, Rect]:
    """Settled surface-local rects of the dock items by name."""
    return wait_stable(lambda: {e.get_attribute("name"): Rect.of(e) for e in krema.items()})


def wait_widest(krema: Krema, name: str, rest: dict[str, Rect]) -> dict[str, Rect]:
    """Wait for the zoom animation to settle and check ``name`` is the widest
    item. ``rest``: :func:`item_rects` before keyboard navigation zoomed any."""
    rects = {n: painted_rect(r, rest[n]) for n, r in item_rects(krema).items()}
    widest = max(rects.values(), key=lambda r: r.width)
    assert rects[name].width == widest.width, rects
    others = [r.width for n, r in rects.items() if n != name]
    assert all(rects[name].width > w for w in others), f"{name!r} not zoomed beyond its neighbours: {rects}"
    return rects


def focused_thumbnails(krema: Krema) -> list[str]:
    return [e.get_attribute("name") for e in krema.thumbnails() if has_state(e, "focused")]


def thumbnail_title(name: str) -> str:
    """Window title of a thumbnail (its name is "<title>[, Active][, Minimized]")."""
    return name.split(", ")[0]


def assert_holds(predicate: Callable[[], bool], duration: float, message: str) -> None:
    """Poll ``predicate`` for ``duration`` seconds; fail as soon as it is false."""
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        assert predicate(), message
        time.sleep(0.2)
    assert predicate(), message


def item_is_shown(krema: Krema, name: str) -> bool:
    el = krema.item(name)
    return el is not None and has_state(el, "showing") and has_state(el, "visible")


def enter_preview(krema: Krema, target: str) -> None:
    """Keyboard mode -> focus ``target`` with Right -> Down opens its preview."""
    invoke_shortcut("focus-dock")

    def steps() -> int | None:
        names, focused = krema.item_names(), focused_items(krema)
        if target not in names or len(focused) != 1 or focused[0] not in names:
            return None
        return names.index(target) - names.index(focused[0]) + 1  # +1: truthy for 0 steps

    right = wait_until(steps, message=f"a focused dock button and item {target!r}") - 1
    krema.wait_keyboard_focus()
    for _ in range(right):
        inp.key("Right")
    wait_focused(krema, target)
    inp.key("Down")
    wait_until(krema.preview_visible, message="preview popup to open")
    wait_until(lambda: len(focused_thumbnails(krema)) == 1, message="a focused thumbnail")


def blueish_pixels(img: Image.Image, rect: Rect) -> int:
    """Pixels in ``rect`` whose blue channel clearly dominates (focus ring)."""
    crop = img.convert("RGB").crop((rect.x, rect.y, rect.x + rect.width, rect.y + rect.height))
    return sum(1 for r, g, b in crop.getdata() if b > 150 and b > r + 60 and b > g + 20)


# ---------------------------------------------------------------------- KBD-001
def _assert_dock_keyboard_entry(krema: Krema, first: str, rest: dict[str, Rect]) -> None:
    wait_focused(krema, first)
    krema.wait_keyboard_focus()
    toolbar = krema.toolbar()
    assert has_state(toolbar, "focused"), "tool bar 'Krema Dock' lacks the focused state"
    for el in krema.items():
        assert has_state(el, "focusable"), el.get_attribute("name")
        assert has_state(el, "showing") and has_state(el, "visible"), el.get_attribute("name")
    rects = wait_widest(krema, first, rest)

    if kwin.can_capture():
        # Focus ring: a highlight-coloured border drawn only while the item
        # has keyboard focus. Same screen region with and without focus.
        region = krema.to_screen(rects[first])
        ring = blueish_pixels(Image.open(krema.screenshot("keyboard-focus")), region)
        inp.key("Escape")
        wait_until(lambda: focused_items(krema) == [], message="Escape to end keyboard navigation")
        wait_stable(lambda: Rect.of(krema.item(first)))
        plain = blueish_pixels(Image.open(krema.screenshot("no-keyboard-focus")), region)
        assert ring > 200 and ring > 3 * plain, f"no focus ring on {first!r}: {ring} blue px focused vs {plain} unfocused"


@pytest.mark.xfail(FOCUS_DOCK_KEY_DROPPED, strict=True, raises=WaitTimeout, reason=FOCUS_DOCK_KEY_DROPPED_REASON)
def test_kbd001_meta_f5_focuses_first_dock_item(krema: Krema, apps: TestWindows) -> None:
    open_items(krema, apps, ["First", "Second"])
    park_pointer()
    rest = item_rects(krema)
    assert focused_items(krema) == []

    # KWin's default "Move Mouse to Focus" also owns Meta+F5 and wins; free it.
    kwin_keys = shortcut_keys("MoveMouseToFocus", component="kwin")
    set_shortcut_keys("MoveMouseToFocus", [], component="kwin")
    try:
        inp.key("Meta", "F5")
        _assert_dock_keyboard_entry(krema, "First", rest)
    finally:
        set_shortcut_keys("MoveMouseToFocus", kwin_keys, component="kwin")


def test_kbd001_focus_dock_shortcut_focuses_first_dock_item(krema: Krema, apps: TestWindows) -> None:
    open_items(krema, apps, ["First", "Second"])
    park_pointer()
    rest = item_rects(krema)
    assert focused_items(krema) == []

    invoke_shortcut("focus-dock")

    _assert_dock_keyboard_entry(krema, "First", rest)


# ---------------------------------------------------------------------- KBD-002
def test_kbd002_arrow_keys_move_focus_between_items(krema: Krema, apps: TestWindows) -> None:
    open_items(krema, apps, ["One", "Two", "Three"])
    park_pointer()
    rest = item_rects(krema)
    invoke_shortcut("focus-dock")
    wait_focused(krema, "One")
    krema.wait_keyboard_focus()

    inp.key("Right")
    wait_focused(krema, "Two")
    wait_widest(krema, "Two", rest)

    inp.key("Right")
    wait_focused(krema, "Three")
    wait_widest(krema, "Three", rest)

    inp.key("Left")
    wait_focused(krema, "Two")
    wait_widest(krema, "Two", rest)


# ---------------------------------------------------------------------- KBD-003
@pytest.mark.kremarc({**TOP, "PinnedLaunchers": []})
def test_kbd003_down_opens_preview_with_first_thumbnail_focused(krema: Krema, apps: TestWindows) -> None:
    open_items(krema, apps, ["Other", "Calc"])
    park_pointer()
    assert not krema.preview_visible()

    enter_preview(krema, "Calc")

    popup = krema.preview_popup()
    assert popup.get_attribute("name") == f"Preview for {env.TEST_APP2_NAME}"
    assert has_state(popup, "showing") and has_state(popup, "visible")
    assert krema.find(f"//popup_menu/label[@name='{env.TEST_APP2_NAME}']") is not None, "app name label"
    assert krema.find("//popup_menu/separator") is not None, "separator"
    thumbs = krema.thumbnails()
    assert len(thumbs) == 1
    assert thumbnail_title(thumbs[0].get_attribute("name")) == "Calc"
    assert has_state(thumbs[0], "focused")
    assert krema.find("//popup_menu/button/button[@name='Close Calc']") is not None, "close button"
    assert krema.find("//popup_menu/button/label[@name='Calc']") is not None, "title label"

    if kwin.can_capture():
        # Live thumbnail: the popup area must contain rendered, varied content.
        img = Image.open(krema.screenshot("preview-open")).convert("RGB")
        r = krema.screen_rect(thumbs[0], surface="preview")
        colors = img.crop((r.x, r.y, r.x + r.width, r.y + r.height)).getcolors(maxcolors=1 << 16)
        assert colors is None or len(colors) > 16, f"thumbnail region looks blank: {colors}"


# ---------------------------------------------------------------------- KBD-004
@pytest.mark.kremarc({**TOP, "PinnedLaunchers": [], "VisibilityMode": config.AUTO_HIDE})
def test_kbd004_enter_activates_focused_thumbnail_window(krema: Krema, apps: TestWindows) -> None:
    a = apps.open("Window A", app_id=env.TEST_APP_ID)
    b = apps.open("Window B", app_id=env.TEST_APP_ID)
    park_pointer()
    app = env.TEST_APP_NAME
    krema.wait_for_item(app)
    wait_until(lambda: b.is_active(), message="last opened window to be active")

    enter_preview(krema, app)
    names = wait_until(
        lambda: (t := [thumbnail_title(e.get_attribute("name")) for e in krema.thumbnails()]) and sorted(t) == ["Window A", "Window B"] and t,
        message="thumbnails of both windows",
    )
    # Move focus to the inactive window so activation is observable.
    target = "Window A"
    if thumbnail_title(focused_thumbnails(krema)[0]) != target:
        inp.key("Right" if names.index(target) > 0 else "Left")
    wait_until(lambda: [thumbnail_title(n) for n in focused_thumbnails(krema)] == [target], message=f"{target!r} thumbnail focused")

    inp.key("Return")

    wait_until(lambda: a.is_active(), message=f"{target!r} to become the active KWin window")
    active = kwin.active_window()
    assert active is not None and active.title == target
    wait_until(lambda: focused_items(krema) == [], message="keyboard mode to end")
    wait_until(lambda: not krema.preview_visible(), message="preview to close")
    popup = krema.preview_popup()
    assert popup is not None and not has_state(popup, "showing")
    wait_until(lambda: not item_is_shown(krema, app), message="dock to slide out")


# ---------------------------------------------------------------------- KBD-005
def test_kbd005_escape_exits_keyboard_navigation(krema: Krema, apps: TestWindows) -> None:
    open_items(krema, apps, ["Esc One", "Esc Two"])
    park_pointer()
    invoke_shortcut("focus-dock")
    wait_focused(krema, "Esc One")
    krema.wait_keyboard_focus()

    inp.key("Escape")

    wait_until(lambda: focused_items(krema) == [], message="no focused dock button after Escape")
    # Keyboard mode is over: arrows no longer move a focus.
    inp.key("Right")
    assert_holds(lambda: focused_items(krema) == [], 1.0, "Right after Escape re-focused a button")


# ---------------------------------------------------------------------- KBD-006
@pytest.mark.kremarc({**TOP, "PinnedLaunchers": []})
def test_kbd006_left_right_move_between_thumbnails(krema: Krema, apps: TestWindows) -> None:
    apps.open("Thumb A", app_id=env.TEST_APP_ID)
    apps.open("Thumb B", app_id=env.TEST_APP_ID)
    park_pointer()
    app = env.TEST_APP_NAME
    krema.wait_for_item(app)

    enter_preview(krema, app)
    wait_until(lambda: len(krema.thumbnails()) == 2, message="two thumbnails")
    first, second = (thumbnail_title(e.get_attribute("name")) for e in krema.thumbnails())
    assert [thumbnail_title(n) for n in focused_thumbnails(krema)] == [first]

    inp.key("Right")
    wait_until(lambda: [thumbnail_title(n) for n in focused_thumbnails(krema)] == [second], message="second thumbnail focused")

    if kwin.can_capture():
        img = Image.open(krema.screenshot("thumbnail-focus"))
        thumbs = {thumbnail_title(e.get_attribute("name")): e for e in krema.thumbnails()}
        ring = blueish_pixels(img, krema.screen_rect(thumbs[second], surface="preview"))
        plain = blueish_pixels(img, krema.screen_rect(thumbs[first], surface="preview"))
        assert ring > 40 and ring > 4 * plain, f"no focus ring on {second!r}: {ring} vs {plain}"

    inp.key("Left")
    wait_until(lambda: [thumbnail_title(n) for n in focused_thumbnails(krema)] == [first], message="first thumbnail focused again")


# ---------------------------------------------------------------------- KBD-007
PREVIEW_REGION_BUG = (
    "krema bug: with a top dock the preview input region is the full 400 px surface depth under the "
    "popup (PreviewController::updateInputRegion, regionY=0/regionH=surfaceH), far larger than the "
    "visible popup. A pointer resting there gets wl_pointer.enter when KWin re-picks pointer focus "
    "after the window closes; PreviewPopup's HoverHandler then calls endPreviewKeyboardNav(), so no "
    "thumbnail keeps the focused state (krema log: 'setPreviewHovered: true' right after Delete)"
)


@pytest.mark.kremarc({**TOP, "PinnedLaunchers": []})
@pytest.mark.parametrize(
    "pointer",
    [
        pytest.param(PARK, id="pointer-parked"),
        pytest.param(CENTRE, id="pointer-at-centre", marks=pytest.mark.xfail(strict=True, reason=PREVIEW_REGION_BUG)),
    ],
)
def test_kbd007_delete_closes_focused_thumbnail_window(krema: Krema, apps: TestWindows, pointer: tuple[int, int]) -> None:
    other = apps.open("Neighbour", app_id=env.TEST_APP2_ID)
    del_a = apps.open("Delete A", app_id=env.TEST_APP_ID)
    del_b = apps.open("Delete B", app_id=env.TEST_APP_ID)
    inp.move(*pointer)
    app = env.TEST_APP_NAME
    krema.wait_for_item("Neighbour")
    krema.wait_for_item(app)

    enter_preview(krema, app)
    wait_until(lambda: len(krema.thumbnails()) == 2, message="two thumbnails")
    doomed = thumbnail_title(focused_thumbnails(krema)[0])
    survivor = {"Delete A": del_b, "Delete B": del_a}[doomed]
    doomed_win = {"Delete A": del_a, "Delete B": del_b}[doomed]

    inp.key("Delete")

    wait_until(lambda: doomed_win.refresh() is None, message=f"{doomed!r} to leave KWin's window list")
    assert all(w.title != doomed for w in kwin.windows())
    wait_until(
        lambda: [thumbnail_title(e.get_attribute("name")) for e in krema.thumbnails()] == [survivor.title],
        message="thumbnail of the closed window to be removed",
    )
    wait_until(lambda: [thumbnail_title(n) for n in focused_thumbnails(krema)] == [survivor.title], message="focus on the remaining thumbnail")

    # Last window: the popup closes and the keyboard returns to the dock buttons.
    inp.key("Delete")

    wait_until(lambda: survivor.refresh() is None, message=f"{survivor.title!r} to close")
    wait_until(lambda: krema.item(app) is None, message="app item to disappear")
    wait_until(lambda: not krema.preview_visible(), message="preview to close")
    assert not has_state(krema.preview_popup(), "showing")
    assert other.refresh() is not None
    inp.key("Left")
    wait_focused(krema, "Neighbour")


# ---------------------------------------------------------------------- KBD-008
@pytest.mark.xfail(
    strict=True,
    reason=(
        "krema bug: keyboard mode is only cancelled by dockMouseArea.onPositionChanged (src/qml/main.qml), "
        "i.e. pointer motion over the dock surface. Wayland delivers motion only to the surface under the "
        "pointer, so moving the mouse elsewhere on screen (400,300) leaves keyboard mode on and the dock "
        "button keeps the focused state"
    ),
)
def test_kbd008_mouse_movement_cancels_keyboard_mode(krema: Krema, apps: TestWindows) -> None:
    open_items(krema, apps, ["Mouse One", "Mouse Two"])
    inp.move(40, 40)
    invoke_shortcut("focus-dock")
    wait_focused(krema, "Mouse One")

    inp.move(400, 300, duration_ms=300)

    wait_until(lambda: focused_items(krema) == [], timeout=3, message="pointer motion to cancel keyboard mode")
    # Cancelled for good: arrow keys no longer navigate.
    inp.key("Right")
    assert_holds(lambda: focused_items(krema) == [], 1.0, "Right re-focused a button after the mouse moved")


def test_kbd008_mouse_movement_over_dock_cancels_keyboard_mode(krema: Krema, apps: TestWindows) -> None:
    open_items(krema, apps, ["Mouse One", "Mouse Two"])
    park_pointer()
    invoke_shortcut("focus-dock")
    wait_focused(krema, "Mouse One")

    # Glide along the item row onto the dock, then away again.
    (x1, y), (x2, _) = krema.item_center("Mouse One"), krema.item_center("Mouse Two")
    inp.move(x1 - 80, y)
    inp.move_path(inp.line((x1 - 80, y), (x2, y), 8))
    inp.move(*PARK, duration_ms=200)

    wait_until(lambda: focused_items(krema) == [], message="pointer motion over the dock to cancel keyboard mode")
    inp.key("Right")
    assert_holds(lambda: focused_items(krema) == [], 1.0, "Right re-focused a button after the mouse moved")


# ---------------------------------------------------------------------- KBD-009
AUTOHIDE = {"VisibilityMode": config.AUTO_HIDE}
DODGE = {"VisibilityMode": config.DODGE_WINDOWS}
#: SmartHide = DodgeWindows restricted to the active window.
SMARTHIDE = {"VisibilityMode": config.DODGE_WINDOWS, "DodgeActiveOnly": True}


def _keyboard_mode_keeps_dock_shown(krema: Krema, apps: TestWindows, visibility: dict) -> str:
    """Hidden dock + overlapping active window -> focus-dock shows it and it
    stays shown and focused past the hide delay. Returns the focused item."""
    krema.stop()
    krema.write_config({"PinnedLaunchers": [], **visibility})
    apps.open("Side", app_id=env.TEST_APP2_ID)
    # Full-screen window overlapping the dock area, opened last so it is the
    # active window (the Dodge/SmartHide trigger).
    cover = apps.open("Cover", app_id=env.TEST_APP_ID, width=env.SCREEN_WIDTH, height=env.SCREEN_HEIGHT)
    wait_until(cover.is_active, message="covering window to be active")
    park_pointer()
    krema.start()
    krema.wait_for_item("Cover")
    krema.wait_for_item("Side")
    first = krema.item_names()[0]
    wait_until(lambda: not item_is_shown(krema, first), message="dock to start hidden")

    invoke_shortcut("focus-dock")

    wait_until(lambda: item_is_shown(krema, first), message="keyboard mode to slide the dock in")
    wait_focused(krema, first)
    krema.wait_keyboard_focus()
    if kwin.can_capture():
        wait_stable(lambda: Rect.of(krema.item(first)))  # slide-in finished
        img = Image.open(krema.screenshot("keyboard-shown"))
        r = krema.screen_rect(krema.item(first))
        assert 0 <= r.y and r.y + r.height <= env.SCREEN_HEIGHT, f"item off screen: {r}"
        colors = img.convert("RGB").crop((r.x, r.y, r.x + r.width, r.y + r.height)).getcolors(maxcolors=1 << 16)
        assert colors is None or len(colors) > 16, "dock item region looks blank"
    # Longer than HideDelay (400 ms) plus the slide-out: the dock stays.
    assert_holds(
        lambda: item_is_shown(krema, first) and focused_items(krema) == [first],
        3.5,
        "dock hid (or lost focus) while in keyboard mode",
    )
    return first


def _escape_resumes_auto_hide(krema: Krema, first: str) -> None:
    inp.key("Escape")
    inp.move(CENTRE[0] + 50, CENTRE[1] + 50, duration_ms=200)

    wait_until(lambda: not item_is_shown(krema, first), message="dock to auto-hide again after Escape")


@pytest.mark.parametrize(
    "visibility",
    [pytest.param(AUTOHIDE, id="autohide"), pytest.param(DODGE, id="dodge"), pytest.param(SMARTHIDE, id="smarthide")],
)
def test_kbd009_keyboard_mode_keeps_hidden_dock_visible(krema: Krema, apps: TestWindows, visibility: dict) -> None:
    _keyboard_mode_keeps_dock_shown(krema, apps, visibility)


@pytest.mark.parametrize(
    "visibility",
    [
        pytest.param(AUTOHIDE, id="autohide"),
        pytest.param(DODGE, id="dodge"),
        pytest.param(
            SMARTHIDE,
            id="smarthide",
            marks=pytest.mark.xfail(
                strict=True,
                reason=(
                    "krema bug: ending keyboard navigation (Escape) only drops the dock's layer-shell keyboard "
                    "interactivity; focus is never handed back to the previously active window. KWin keeps the "
                    "dock surface as the active window (kwin-windows.json: active=True on krema's 1024x108 "
                    "surface, Cover active=False), so SmartHide (DodgeActiveOnly) sees no active overlapping "
                    "window and the dock never hides again"
                ),
            ),
        ),
    ],
)
def test_kbd009_dock_auto_hides_again_after_escape(krema: Krema, apps: TestWindows, visibility: dict) -> None:
    first = _keyboard_mode_keeps_dock_shown(krema, apps, visibility)
    _escape_resumes_auto_hide(krema, first)
