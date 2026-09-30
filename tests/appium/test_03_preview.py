# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Scenario 03: preview popup (tests/e2e/scenarios/03-preview-popup.md).

Real pointer/keyboard input; oracles are krema's AT-SPI tree (popup, thumbnail
buttons, states, geometry), KWin (active window, window list), KWin
ScreenShot2 pixels of the live PipeWire thumbnails, and AT-SPI announcement
events for ``Accessible.announce``.
"""

from __future__ import annotations

import time
import warnings

import pytest
from PIL import Image

from krema_e2e import config, env, kwin
from krema_e2e import input as inp
from krema_e2e import preview as pv
from krema_e2e.krema import Krema, Rect, has_state
from krema_e2e.shortcuts import invoke_shortcut
from krema_e2e.waits import wait_until
from krema_e2e.windows import TestWindow, TestWindows

APP = env.TEST_APP_NAME


def _open_group(apps: TestWindows, titles: list[str], colors: list[str | None] | None = None) -> list[TestWindow]:
    colors = colors or [None] * len(titles)
    return [apps.open(t, app_id=env.TEST_APP_ID, color=c) for t, c in zip(titles, colors)]


def _app_window_titles(app_id: str = env.TEST_APP_ID) -> set[str]:
    return {w.title for w in kwin.app_windows() if w.app_id == app_id}


def _thumb_center(krema: Krema, title: str) -> tuple[int, int]:
    return pv.screen_rect(krema, krema.wait_for(pv.thumb_xpath(title))).center


def _require_capture() -> None:
    # Hard requirement, not a skip: live thumbnails need an OpenGL KWin (DRM
    # render node); run-e2e.sh passes /dev/dri when the host has one.
    assert kwin.can_capture(), f"KWin compositing is {kwin.compositing_type()}: PipeWire thumbnails need a DRM render node"


def _assert_stays(predicate, duration: float, message: str) -> None:
    """Assert ``predicate`` holds on every poll for ``duration`` seconds."""
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        assert predicate(), message
        time.sleep(0.05)


def _wait_thumbnail_color(krema: Krema, title: str, matches, shot: str) -> Image.Image:
    """Wait until the thumbnail of ``title`` shows the window's solid content
    color over most of its image (the live PipeWire frame, not the icon).

    A stream whose buffers PipeWire delivered untyped never shows a frame
    (pv.UNTYPED_BUFFER_WARNING; a PipeWire/KWin negotiation race, not krema).
    Only when KPipeWire reported exactly that is the source window resized
    once, which renegotiates the stream; the live content is still required."""
    last: list[float] = []
    renegotiated = False

    def check() -> Image.Image | None:
        nonlocal renegotiated
        rect = pv.thumbnail_image_rect(pv.screen_rect(krema, krema.wait_for(pv.thumb_xpath(title))))
        image = krema.screenshot(shot)
        last[:] = [pv.dominant_fraction(image, rect, matches)]
        if last[0] > 0.6:
            return image
        if not renegotiated and pv.dropped_untyped_buffers(krema):
            warnings.warn(f"thumbnail {title!r}: KPipeWire dropped untyped buffers; renegotiating the screencast")
            pv.renegotiate_screencast(title)
            renegotiated = True
        return None

    return wait_until(
        check,
        timeout=15,
        interval=0.5,
        message=lambda: f"thumbnail {title!r} to show its live content (match fraction {last}, renegotiated: {renegotiated})",
    )


# ---------------------------------------------------------------- PREV-001


def test_prev001_hover_opens_preview_above_dock_with_live_thumbnails(krema: Krema, apps: TestWindows) -> None:
    _require_capture()
    _open_group(apps, ["Red", "Blue"], ["red", "blue"])
    krema.wait_for_item(APP)
    krema.move_away()
    assert not krema.preview_visible()

    popup = pv.open_by_hover(krema, APP)

    assert popup.get_attribute("name") == f"Preview for {APP}"
    assert has_state(popup, "showing") and has_state(popup, "visible")
    assert krema.find(pv.HEADER_XPATH).get_attribute("name") == APP
    assert krema.find(pv.SEPARATOR_XPATH) is not None
    wait_until(lambda: sorted(pv.thumb_titles(krema)) == ["Blue", "Red"], message="one thumbnail per window")
    for title in ("Red", "Blue"):
        assert krema.find(pv.close_xpath(title)) is not None, f"close button of {title!r}"
        assert krema.find(f"{pv.thumb_xpath(title)}/label").get_attribute("name") == title

    # Above the (bottom) dock, horizontally over the hovered item, right where
    # the dock surface (panel bar + zoom headroom) ends: no gap in between, in
    # the default AlwaysVisible mode too, where the dock reserves its panel
    # bar as exclusive zone.
    popup_rect = pv.screen_rect(krema, popup)
    item = krema.screen_rect(krema.item(APP))
    assert popup_rect.y + popup_rect.height <= item.y, f"popup {popup_rect} not above item {item}"
    assert popup_rect.x <= item.center[0] <= popup_rect.x + popup_rect.width
    dock = krema.surface_rect("dock")
    assert abs(dock.y - (popup_rect.y + popup_rect.height)) <= 8, f"popup {popup_rect} detached from the dock surface {dock}"

    # Live PipeWire thumbnails: each shows its own window's content color.
    image = _wait_thumbnail_color(krema, "Red", pv.is_red, "prev001-red")
    blue_rect = pv.thumbnail_image_rect(pv.screen_rect(krema, krema.find(pv.thumb_xpath("Blue"))))
    image = _wait_thumbnail_color(krema, "Blue", pv.is_blue, "prev001-blue")
    red_rect = pv.thumbnail_image_rect(pv.screen_rect(krema, krema.find(pv.thumb_xpath("Red"))))
    assert pv.dominant_fraction(image, red_rect, pv.is_red) > 0.6
    assert pv.dominant_fraction(image, blue_rect, pv.is_red) < 0.05, "Blue thumbnail shows the red window"


# ---------------------------------------------------------------- PREV-002


def test_prev002_grouped_app_shows_one_thumbnail_per_window_in_a_row(krema: Krema, apps: TestWindows) -> None:
    _open_group(apps, ["Alpha", "Beta", "Gamma"])
    krema.wait_for_item(APP)
    krema.move_away()

    pv.open_by_hover(krema, APP)

    windows = _app_window_titles()
    assert windows == {"Alpha", "Beta", "Gamma"}
    wait_until(lambda: len(krema.thumbnails()) == len(windows), message="thumbnail count == window count")
    assert set(pv.thumb_titles(krema)) == windows
    rects = sorted((Rect.of(krema.find(pv.thumb_xpath(t))) for t in windows), key=lambda r: r.x)
    assert len({r.y for r in rects}) == 1, f"thumbnails not in one row: {rects}"
    for left, right in zip(rects, rects[1:]):
        assert left.x + left.width <= right.x, f"thumbnails overlap: {rects}"


# ---------------------------------------------------------------- PREV-003


def test_prev003_clicking_a_thumbnail_activates_that_window(krema: Krema, apps: TestWindows) -> None:
    alpha, beta, gamma = _open_group(apps, ["Alpha", "Beta", "Gamma"])
    krema.wait_for_item(APP)
    wait_until(gamma.is_active, message="newest window to be active")
    krema.move_away()

    popup = pv.open_by_hover(krema, APP)
    wait_until(lambda: len(pv.thumb_titles(krema)) == 3, message="three thumbnails")
    second = pv.thumb_titles(krema)[1]
    target = {"Alpha": alpha, "Beta": beta, "Gamma": gamma}[second]
    assert not target.is_active()

    pv.wait_on_screen(krema, popup)
    pv.glide_into(krema, _thumb_center(krema, second))
    assert krema.preview_visible(), "preview closed while moving the pointer into it"
    inp.click()

    active = wait_until(
        lambda: (w := kwin.active_window()) is not None and w.pid == target.pid and w,
        message=lambda: f"{second!r} to become active (active: {kwin.active_window()})",
    )
    assert active.title == second
    wait_until(lambda: not krema.preview_visible(), message="preview to close after activation")
    popup = krema.preview_popup()
    assert Rect.of(popup)[2:] == (0, 0)


# ---------------------------------------------------------------- PREV-004


def test_prev004_close_button_closes_that_window(krema: Krema, apps: TestWindows) -> None:
    _open_group(apps, ["Alpha", "Beta", "Gamma"])
    krema.wait_for_item(APP)
    krema.move_away()
    popup = pv.open_by_hover(krema, APP)
    wait_until(lambda: len(krema.thumbnails()) == 3, message="three thumbnails")

    pv.wait_on_screen(krema, popup)
    close = pv.screen_rect(krema, krema.wait_for(pv.close_xpath("Beta")))
    pv.glide_into(krema, close.center)
    assert krema.preview_visible(), "preview closed while moving the pointer into it"
    inp.click()

    wait_until(lambda: _app_window_titles() == {"Alpha", "Gamma"}, message=lambda: f"Beta to close (windows: {_app_window_titles()})")
    wait_until(lambda: sorted(pv.thumb_titles(krema)) == ["Alpha", "Gamma"], message=lambda: f"Beta thumbnail removed ({pv.thumb_titles(krema)})")
    assert krema.preview_visible(), "preview must stay open while windows remain"


def test_prev004_delete_key_closes_focused_thumbnail_window(krema: Krema, apps: TestWindows) -> None:
    _open_group(apps, ["Alpha", "Beta", "Gamma"])
    krema.wait_for_item(APP)
    krema.move_away()

    invoke_shortcut("focus-dock")
    wait_until(lambda: krema.focused_item() == APP, message="dock item focused")
    krema.wait_keyboard_focus()
    inp.key("Up")  # bottom dock: Up opens the preview in keyboard mode
    wait_until(krema.preview_visible, message="preview to open from the keyboard")
    wait_until(lambda: len(krema.thumbnails()) == 3, message="three thumbnails")
    inp.key("Right")

    def focused_thumb() -> str | None:
        el = krema.find(f"{pv.THUMB_XPATH}[contains(@states, 'focused')]/label")
        return el.get_attribute("name") if el is not None else None

    ordered = pv.thumb_titles(krema)
    wait_until(lambda: focused_thumb() == ordered[1], message=lambda: f"second thumbnail focused (focused: {focused_thumb()})")
    inp.key("Delete")

    remaining = set(ordered) - {ordered[1]}
    wait_until(lambda: _app_window_titles() == remaining, message=lambda: f"{ordered[1]!r} to close (windows: {_app_window_titles()})")
    wait_until(lambda: set(pv.thumb_titles(krema)) == remaining, message=lambda: f"thumbnail removed ({pv.thumb_titles(krema)})")
    assert krema.preview_visible(), "preview must stay open while windows remain"


@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher(env.TEST_APP_ID)]})
def test_prev004_closing_last_window_closes_preview_and_returns_to_dock(krema: Krema, apps: TestWindows) -> None:
    apps.open("Solo", app_id=env.TEST_APP_ID)
    wait_until(lambda: len(krema.items()) == 1 and krema.item_names() != [APP], message=lambda: f"launcher to show the window ({krema.item_names()})")
    krema.move_away()

    invoke_shortcut("focus-dock")
    wait_until(krema.focused_item, message="dock item focused")
    krema.wait_keyboard_focus()
    inp.key("Up")
    wait_until(krema.preview_visible, message="preview to open from the keyboard")
    wait_until(lambda: pv.thumb_titles(krema) == ["Solo"], message="one thumbnail")
    inp.key("Delete")

    wait_until(lambda: "Solo" not in _app_window_titles(), message="Solo to close")
    wait_until(lambda: not krema.preview_visible(), message="preview to close when no windows remain")
    assert krema.focused_item() == APP, f"keyboard focus not back on the dock item ({krema.item_names()})"


# ---------------------------------------------------------------- PREV-005


def test_prev005_preview_closes_when_pointer_leaves(krema: Krema, apps: TestWindows) -> None:
    _open_group(apps, ["Alpha", "Beta"])
    krema.wait_for_item(APP)
    krema.move_away()
    popup = pv.open_by_hover(krema, APP)
    pv.wait_on_screen(krema, popup)
    pv.glide_into(krema, _thumb_center(krema, pv.thumb_titles(krema)[0]))
    assert krema.preview_visible()

    inp.move(env.SCREEN_WIDTH // 2, 300)  # outside dock and preview input area

    wait_until(lambda: not krema.preview_visible(), timeout=5, message="preview to close after the pointer left")
    popup = krema.preview_popup()
    assert popup is not None, "popup element must stay in the AT-SPI tree"
    assert Rect.of(popup)[2:] == (0, 0)
    assert not has_state(popup, "showing")


@pytest.mark.kremarc({"PinnedLaunchers": [], "PreviewHideDelay": 1500})
def test_prev005_close_on_leave_is_delayed(krema: Krema, apps: TestWindows) -> None:
    _open_group(apps, ["Alpha", "Beta"])
    krema.wait_for_item(APP)
    krema.move_away()
    popup = pv.open_by_hover(krema, APP)
    pv.wait_on_screen(krema, popup)
    pv.glide_into(krema, _thumb_center(krema, pv.thumb_titles(krema)[0]))

    t0 = time.monotonic()
    inp.move(env.SCREEN_WIDTH // 2, 300)
    assert krema.preview_visible(), "preview closed instantly instead of after the hide delay"
    wait_until(lambda: not krema.preview_visible(), timeout=10, message="preview to close after the hide delay")
    # PreviewController's hide timer is a default (Qt::CoarseTimer) QTimer,
    # which may fire up to 5% early: 1500 ms can elapse as 1425 ms. t0 is
    # taken before the move, so it never overstates the delay.
    elapsed = time.monotonic() - t0
    assert elapsed >= 1.5 * 0.95, f"preview closed {elapsed:.3f}s after leaving, before the 1.5 s hide delay"


@pytest.mark.kremarc({"PinnedLaunchers": [], "PreviewHoverDelay": 2000})
def test_prev005_preview_stays_closed_when_a_task_row_appears_while_leaving(krema: Krema, apps: TestWindows) -> None:
    """The pointer leaves the dock through the open preview while a new task
    row makes the dock re-centre. The dock must not hit-test its last pointer
    position again as the icons move: that re-hovered the item and, one
    PreviewHoverDelay later, reopened its preview with the pointer far from
    the dock, where nothing closes it any more."""
    apps.open("Alpha", app_id=env.TEST_APP_ID)
    krema.wait_for_item("Alpha")
    krema.move_away()
    popup = pv.open_by_hover(krema, "Alpha")
    # Only a popup on screen takes the pointer (see pv.wait_on_screen).
    pv.wait_on_screen(krema, popup)

    # A fast flick from the item onto the preview: the first motion event
    # already lands on the preview, so the dock's last pointer position is
    # the zoomed item's centre (a glide would leave the dock from above the
    # icon, where the hit test finds nothing). The second event is motion
    # inside the preview, which its HoverHandler needs to see the pointer.
    x, y = pv.screen_rect(krema, popup).center
    inp.move_path([(x, y), (x + 4, y)], step_ms=20)
    # Past the 200 ms hide delay the dock started on leave: the preview holds
    # the pointer.
    _assert_stays(krema.preview_visible, 0.5, "preview closed with the pointer resting on it")

    # Beta's new task row re-centres the dock under the stale position; the
    # pointer then leaves the preview well within PreviewHoverDelay.
    apps.open("Beta", app_id=env.TEST_APP2_ID)
    krema.wait_for_item("Beta")
    inp.move(env.SCREEN_WIDTH // 2, 20)  # outside dock and preview input area
    wait_until(lambda: not krema.preview_visible(), timeout=5, message="preview to close after the pointer left")

    # Longer than PreviewHoverDelay (2 s) since Beta's row appeared.
    _assert_stays(lambda: not krema.preview_visible(), 3.0, "preview reopened with the pointer away from the dock")


# ---------------------------------------------------------------- PREV-006


def test_prev006_single_window_preview(krema: Krema, apps: TestWindows) -> None:
    _require_capture()
    solo = apps.open("Solo", app_id=env.TEST_APP_ID, color="#00c000")
    apps.open("Other", app_id=env.TEST_APP2_ID)
    krema.wait_for_item("Solo")
    wait_until(lambda: not solo.is_active(), message="another window to be active")
    krema.move_away()

    popup = pv.open_by_hover(krema, "Solo")

    assert popup.get_attribute("name") == f"Preview for {APP}"
    wait_until(lambda: len(krema.thumbnails()) == 1, message="exactly one thumbnail")
    assert pv.thumb_titles(krema) == ["Solo"]
    assert krema.find(pv.close_xpath("Solo")) is not None
    _wait_thumbnail_color(krema, "Solo", pv.is_green, "prev006")

    pv.glide_into(krema, _thumb_center(krema, "Solo"))
    inp.click()
    wait_until(solo.is_active, message=lambda: f"Solo to become active (active: {kwin.active_window()})")
    wait_until(lambda: not krema.preview_visible(), message="preview to close after activation")


# ---------------------------------------------------------------- PREV-007


def test_prev007_opening_preview_announces_window_count(krema: Krema, apps: TestWindows) -> None:
    announcements = pv.Announcements()
    try:
        _open_group(apps, ["Alpha", "Beta"])
        krema.wait_for_item(APP)
        krema.move_away()
        announcements.clear()

        pv.open_by_hover(krema, APP)

        expected = f"Preview for {APP}, 2 windows"
        wait_until(lambda: expected in announcements.messages(), message=lambda: f"announcement {expected!r} (got {announcements.messages()})")
    finally:
        announcements.close()


# --------------------------------------------------------------- PREV-008/009
def _configure_explicit_preview(krema: Krema, hide_delay: int = 200, max_zoom: float | None = None) -> None:
    """Use GroupedWindowClickAction=1 while keeping hover previews opt-in."""
    settings = {
        "PinnedLaunchers": [],
        "PreviewEnabled": False,
        "PreviewHideDelay": hide_delay,
        "SingleWindowClickAction": 0,
        "GroupedWindowClickAction": 1,
    }
    if max_zoom is not None:
        settings["MaxZoomFactor"] = max_zoom
    krema.write_config(settings)
    krema.restart()
    wait_until(
        lambda: krema.read_config().get("General", {}).get("GroupedWindowClickAction") == "1",
        message="explicit group-preview policy persisted",
    )


def test_prev008_explicit_group_click_shows_all_thumbnails_and_selected_child_closes(
    krema: Krema, apps: TestWindows
) -> None:
    """Group action 1 is a popup action: it does not activate the group."""
    _require_capture()
    _configure_explicit_preview(krema)
    alpha, beta, gamma = _open_group(apps, ["Alpha", "Beta", "Gamma"], ["#d02020", "#2020d0", "#20a020"])
    krema.wait_for_item(APP)
    for child in (beta, gamma):
        kwin.set_minimized(child.internal_id, True)
    wait_until(
        lambda: all((w := child.refresh()) is not None and w.minimized for child in (beta, gamma)),
        message="unselected children to be minimized before explicit preview",
    )
    kwin.activate(alpha.internal_id)
    wait_until(alpha.is_active, message="first grouped child to become active")
    krema.move_away(close_preview=False)

    krema.click_item(APP)
    wait_until(krema.preview_visible, message="explicit group click to show its popup")
    popup = krema.preview_popup()
    assert popup is not None and has_state(popup, "showing")
    wait_until(lambda: set(pv.thumb_titles(krema)) == {"Alpha", "Beta", "Gamma"}, message="all grouped thumbnails")
    pv.wait_on_screen(krema, popup)
    _wait_thumbnail_color(krema, "Alpha", pv.is_red, "prev008-explicit-alpha")
    assert alpha.is_active(), "showing the popup must not activate a different child"

    # Repeated explicit clicks are idempotent: one visible popup with the same
    # thumbnail set, rather than a second popup or an activation.
    krema.click_item(APP)
    wait_until(krema.preview_visible, message="repeated explicit click to keep popup visible")
    assert set(pv.thumb_titles(krema)) == {"Alpha", "Beta", "Gamma"}
    assert alpha.is_active()

    pv.glide_into(krema, _thumb_center(krema, "Beta"))
    inp.click()
    wait_until(
        lambda: (w := beta.refresh()) is not None and w.active and not w.minimized,
        message=lambda: f"selected minimized child Beta to restore and activate (active: {kwin.active_window()})",
    )
    wait_until(lambda: not krema.preview_visible(), message="selected thumbnail to close popup")
    assert (w := gamma.refresh()) is not None and w.minimized, "unselected minimized child must remain minimized"


def test_prev009_explicit_group_pending_hide_retargets_after_reenter(
    krema: Krema, apps: TestWindows
) -> None:
    """An explicit second-group click cancels a pending first-group hide."""
    _require_capture()
    _configure_explicit_preview(krema, hide_delay=1000, max_zoom=1.0)
    _open_group(apps, ["Alpha", "Beta"])
    other = apps.open("Other A", app_id=env.TEST_APP2_ID)
    apps.open("Other B", app_id=env.TEST_APP2_ID)
    krema.wait_for_item(APP)
    krema.wait_for_item(env.TEST_APP2_NAME)
    krema.move_away(close_preview=False)
    krema.click_item(APP)
    wait_until(krema.preview_visible, message="explicit first-group popup to show")
    wait_until(lambda: set(pv.thumb_titles(krema)) == {"Alpha", "Beta"}, message="first-group thumbnails")
    pv.wait_on_screen(krema, krema.preview_popup())

    # Re-enter along the bottom edge, below the preview surface. Resolve the
    # target geometry before leaving the popup: once the pointer leaves the
    # first group, the configured hide timer is already running, so a
    # settled_item_center() lookup here would consume that budget on a slow
    # AT-SPI session.
    target_x, target_y = krema.item_center(env.TEST_APP2_NAME)
    edge_y = env.SCREEN_HEIGHT - 1
    reentry_started = time.monotonic()
    inp.move_path(
        [
            *inp.line((env.SCREEN_WIDTH // 2, edge_y), (target_x, edge_y), 3),
            *inp.line((target_x, edge_y), (target_x, target_y), 3),
        ],
        step_ms=40,
    )
    reentry_elapsed = time.monotonic() - reentry_started
    assert reentry_elapsed < 1.0, f"re-entry path exceeded the configured 1000 ms hide delay ({reentry_elapsed:.3f}s)"
    assert krema.preview_visible(), "first popup closed before the configured hide delay"
    assert set(pv.thumb_titles(krema)) == {"Alpha", "Beta"}, "hover disabled must not retarget the popup"
    inp.click()
    wait_until(
        lambda: krema.preview_visible() and set(pv.thumb_titles(krema)) == {"Other A", "Other B"},
        message="explicit click to retarget the second group",
    )
    pv.wait_on_screen(krema, krema.preview_popup())
    _assert_stays(
        lambda: krema.preview_visible() and set(pv.thumb_titles(krema)) == {"Other A", "Other B"},
        1.2,
        "pending first-group hide dismissed or retargeted the second-group popup",
    )
    pv.glide_into(krema, _thumb_center(krema, "Other A"))
    inp.click()
    wait_until(other.is_active, message="retargeted second-group thumbnail to activate its child")
    wait_until(lambda: not krema.preview_visible(), message="retargeted selection to close popup")
