# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""TC DND-001..DND-004 (tests/e2e/scenarios/05-drag-and-drop.md): reordering
dock items with a real press-hold-move-release pointer drag.

main.qml starts an internal drag when the left button stays pressed on an
item for 300 ms (dragHoldTimer) and the pointer then moves more than 10 px.
Every drag here is ONE action chain for the session's persistent
``inputsynth --stdin`` process: a button held across separate chains lasts
only as long as that process (a crash or a timeout restarts it and drops the
button), so a mid-drag check runs the chain in the background with an
interruptible in-chain pause and inspects the screen and the AT-SPI tree
during that pause; the pause ends (and the chain goes on to the drop) as
soon as the check is done.

The drag ghost, the drop indicator and the dimmed source are
``Accessible.ignored`` QML items, so those checks are pixel checks on KWin
screenshots (needs the OpenGL compositor, i.e. a DRM render node).
"""

from __future__ import annotations

import time
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Callable, Iterator, Sequence

import numpy as np
import pytest

from krema_e2e import config, env, kwin
from krema_e2e import input as inp
from krema_e2e.krema import Krema, Rect, painted_rect
from krema_e2e.waits import wait_stable, wait_until
from krema_e2e.windows import TestWindow, TestWindows

ICON = 48  # krema.kcfg IconSize default; IconSpacing default is 4
HOLD_MS = 450  # > main.qml dragHoldTimer (300 ms)
STEP_MS = 40
PAUSE_MS = 8000  # upper bound of the mid-drag pause; the body's end cuts it short

KWRITE, KFIND = "KWrite", "KFind"
TW, TW2 = env.TEST_APP_NAME, env.TEST_APP2_NAME  # icons: utilities-terminal (dark), accessories-text-editor
DESKTOP_IDS = {
    KWRITE: "org.kde.kwrite",
    KFIND: "org.kde.kfind",
    TW: env.TEST_APP_ID,
    TW2: env.TEST_APP2_ID,
}


def kremarc(*pinned: str) -> dict:
    # The hover tooltip / preview (PreviewHoverDelay, 500 ms) would be drawn
    # over the pixels these tests inspect; push it out of every test's span.
    return {"PinnedLaunchers": [config.launcher(DESKTOP_IDS[n]) for n in pinned], "PreviewHoverDelay": 60000}


def pinned_launchers(krema: Krema) -> list[str]:
    return config.as_list(krema.read_config()["General"]["PinnedLaunchers"])


# --------------------------------------------------------------------- input


def _move(x: int, y: int) -> dict:
    return {"type": "pointerMove", "x": int(x), "y": int(y), "duration": 0, "origin": "viewport"}


def _pause(ms: int, interruptible: bool = False) -> dict:
    return {"type": "pause", "duration": int(ms), "interruptible": interruptible}


def drag_actions(start: tuple[int, int], legs: Sequence[tuple[int, int] | int], steps: int = 8) -> list[dict]:
    """Hover ``start``, press, hold HOLD_MS, then for each leg glide to the
    point in ``steps`` motion events or (for an int) pause up to that many ms
    (until :func:`dragging`'s body ends), and release at the last point."""
    x, y = start
    actions = [_move(x, y - 30), _pause(100), _move(x, y), _pause(150)]
    actions += [{"type": "pointerDown", "button": 0}, _pause(HOLD_MS)]
    pos = start
    for leg in legs:
        if isinstance(leg, int):
            actions.append(_pause(leg, interruptible=True))
            continue
        for p in inp.line(pos, leg, steps):
            actions += [_move(*p), _pause(STEP_MS)]
        pos = leg
    actions.append({"type": "pointerUp", "button": 0})
    return actions


def _end(start: tuple[int, int], legs: Sequence[tuple[int, int] | int]) -> tuple[int, int]:
    return next((leg for leg in reversed(legs) if not isinstance(leg, int)), start)


def _pointer(actions: list[dict]) -> list[dict]:
    return [{"type": "pointer", "id": "mouse", "parameters": {"pointerType": "mouse"}, "actions": actions}]


def drag(start: tuple[int, int], legs: Sequence[tuple[int, int] | int]) -> None:
    """A complete drag, in the foreground."""
    inp.run_actions(_pointer(drag_actions(start, legs)), timeout=60)
    inp.move(*_end(start, legs))  # keep krema_e2e.input's pointer bookkeeping in sync


@contextmanager
def dragging(start: tuple[int, int], legs: Sequence[tuple[int, int] | int]) -> Iterator[None]:
    """Run a drag chain in the background; the body runs while it is in
    flight (sync on kwin.cursor_pos()). The chain's pause ends when the body
    does; always waits for the release."""
    try:
        with inp.background_actions(_pointer(drag_actions(start, legs)), timeout=60):
            yield
    finally:
        inp.move(*_end(start, legs))


def wait_cursor(pos: tuple[int, int]) -> None:
    wait_until(lambda: kwin.cursor_pos() == tuple(pos), timeout=15, message=f"pointer to reach {pos}")


def assert_stays(getter: Callable[[], object], expected: object, duration: float, message: str) -> None:
    """Assert that an observed state stays unchanged for ``duration`` seconds."""
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        actual = getter()
        assert actual == expected, f"{message}: {actual!r} != {expected!r}"
        time.sleep(0.05)


# ------------------------------------------------------------ focus return


#: Title of the window opened for TW's pinned launcher: the launcher item
#: then shows the window title.
FOCUS = "Focus"


def open_active_window(apps: TestWindows, title: str) -> TestWindow:
    """An active window in the top-left corner, clear of every drag/drop
    point, whose focus a drag must hand back (the dock takes KWin's
    activation while it holds keyboard interactivity for the drag). Its app
    is TW: with TW pinned, the launcher item turns into this window's task."""
    win = apps.open(title, app_id=env.TEST_APP_ID, width=300, height=200)
    kwin.evaluate(
        f"const w = workspace.windowList().find(w => String(w.internalId) === {win.internal_id!r});"
        "w.frameGeometry = {x: 0, y: 0, width: 300, height: 200}; report(true);"
    )
    kwin.activate(win.internal_id)
    wait_until(win.is_active, message=f"{title!r} to be active before the drag")
    return win


def wait_focus_returned(win: TestWindow) -> None:
    wait_until(win.is_active, message=lambda: f"{win.title!r} active again after the drag (active: {kwin.active_window()})")


# ------------------------------------------------------------------- pixels


def _lum(rgb: np.ndarray) -> np.ndarray:
    return rgb[..., :3] @ np.array([0.299, 0.587, 0.114])


def _blue_columns(rgb: np.ndarray, top: int, x0: int, x1: int) -> set[int]:
    """Columns in [x0, x1) with a (nearly) full icon-height run of
    highlight-blue pixels (Breeze highlightColor #3daee9) starting at ``top``."""
    band = rgb[top : top + ICON, x0:x1].astype(np.int32)
    r, g, b = band[..., 0], band[..., 1], band[..., 2]
    blue = (b >= 170) & (b - r >= 90) & (g >= 100)
    return {x0 + int(i) for i in np.nonzero(blue.sum(axis=0) >= ICON - 8)[0]}


@dataclass
class Scene:
    """The dock at rest (pointer away, nothing zoomed) before a drag."""

    krema: Krema
    names: list[str]
    rects: dict[str, Rect]
    base: np.ndarray  # RGB screenshot of the resting dock

    @classmethod
    def capture(cls, krema: Krema, names: list[str]) -> "Scene":
        for n in names:
            krema.wait_for_item(n, timeout=15)
        wait_until(lambda: krema.item_names() == names, message=lambda: f"dock order {names} (have {krema.item_names()})")
        krema.move_away(close_preview=False)
        rects = wait_stable(lambda: {n: krema.screen_rect(krema.item(n)) for n in names}, duration=0.6)
        assert all(r.width == ICON for r in rects.values()), f"items not at rest: {rects}"
        scene = cls(krema, names, rects, np.zeros(0))
        scene.base = scene.stable_shot("baseline")
        return scene

    # -- screenshots
    def shot(self, name: str) -> np.ndarray:
        """RGB screenshot (flattened onto black by kwin.screenshot) of the
        rows every pixel check reads: from 60 px above the item row (the
        drag ghost sits 16 px above it) down to the screen edge."""
        top = min(r.y for r in self.rects.values()) - 60
        return np.asarray(self.krema.screenshot(name, Rect(0, top, env.SCREEN_WIDTH, env.SCREEN_HEIGHT - top)))

    def stable_shot(self, name: str) -> np.ndarray:
        """Screenshot once the dock band stopped changing (icons loaded,
        show/zoom animations over)."""
        top = min(r.y for r in self.rects.values()) - 60
        wait_stable(lambda: self.shot(name)[top:].tobytes(), duration=0.6, interval=0.2)
        return self.shot(name)

    # -- geometry
    @property
    def row_top(self) -> int:
        return self.rects[self.names[0]].y

    def center(self, name: str) -> tuple[int, int]:
        return self.rects[name].center

    def item_rects(self) -> dict[str, Rect]:
        return {n: self.krema.screen_rect(self.krema.item(n)) for n in self.names}

    # -- drop indicator (main.qml dropIndicator: 2 px wide, icon high)
    def indicator_columns(self, shot: np.ndarray, ghost_x: int | None = None) -> set[int]:
        """New highlight-blue full-height columns in the item row compared to
        the resting dock, outside the ghost icon's span."""
        x0 = min(r.x for r in self.rects.values()) - 16
        x1 = max(r.x + r.width for r in self.rects.values()) + 16
        new = _blue_columns(shot, self.row_top, x0, x1) - _blue_columns(self.base, self.row_top, x0, x1)
        if ghost_x is not None:
            new -= set(range(ghost_x - ICON // 2, ghost_x + ICON // 2))
        return new

    def indicator_expected(self, target: str, after: bool) -> set[int]:
        """Columns of the insertion line: right after ``target`` when moving
        forward, right before it when moving backward (±1 px tolerance)."""
        r = self.rects[target]
        x = r.x + ICON + 1 if after else r.x - 3  # itemX + width + spacing/2 - 1 | itemX - spacing/2 - 1
        return set(range(x - 1, x + 3))

    # -- opacity of the dimmed source and of the ghost
    def _panel_lum(self, name: str) -> float:
        r = self.rects[name]
        return float(np.median(_lum(self.base)[r.y + 4 : r.y + ICON - 4, r.x - 2]))

    def _icon(self, name: str, rows: int) -> tuple[np.ndarray, float]:
        """Unblended luminance of ``name``'s icon (first ``rows`` rows),
        recovered from the resting dock where the icon is drawn at opacity
        0.8 over the panel (DockItem.qml iconImage: inactive launcher)."""
        r, panel = self.rects[name], self._panel_lum(name)
        drawn = _lum(self.base)[r.y : r.y + rows, r.x : r.x + ICON]
        return (drawn - 0.2 * panel) / 0.8, panel

    def source_opacity(self, shot: np.ndarray, name: str) -> float:
        """Opacity of ``name``'s icon in its own slot, measured where the icon
        contrasts with the panel: alpha = (bg - observed) / (bg - icon)."""
        r = self.rects[name]
        icon, panel = self._icon(name, ICON)
        seen = _lum(shot)[r.y : r.y + ICON, r.x : r.x + ICON]
        body = np.abs(panel - icon) > 100
        assert body.sum() > 100, f"{name!r} icon has too little contrast to measure opacity"
        return float(np.median((panel - seen[body]) / (panel - icon[body])))

    def ghost(self, shot: np.ndarray, name: str, cursor: tuple[int, int]) -> tuple[float, float]:
        """(opacity, correlation with ``name``'s icon) of the drag ghost
        centred on ``cursor``, over its rows with no dock icon underneath."""
        gx, gy = cursor[0] - ICON // 2, cursor[1] - ICON // 2
        rows = min(ICON, self.row_top - gy)
        assert rows >= 24, f"ghost at {cursor} overlaps the item row"
        icon, _ = self._icon(name, rows)
        bg = _lum(self.base)[gy : gy + rows, gx : gx + ICON]
        seen = _lum(shot)[gy : gy + rows, gx : gx + ICON]
        body = np.abs(bg - icon) > 100
        assert body.sum() > 100, f"{name!r} icon has too little contrast to measure opacity"
        opacity = float(np.median((bg[body] - seen[body]) / (bg[body] - icon[body])))
        corr = float(np.corrcoef(seen.ravel(), icon.ravel())[0, 1])
        return opacity, corr


# -------------------------------------------------------------------- tests


@pytest.mark.kremarc(kremarc(KWRITE, KFIND, TW, TW2))
def test_dnd_001_drag_reorders_dock_items(krema: Krema, apps: TestWindows) -> None:
    focus = open_active_window(apps, FOCUS)
    scene = Scene.capture(krema, [KWRITE, KFIND, FOCUS, TW2])
    start = scene.center(KWRITE)
    over = (scene.center(FOCUS)[0], start[1])  # third item's slot

    with dragging(start, [over, PAUSE_MS]):
        wait_cursor(over)
        expected = scene.indicator_expected(FOCUS, after=True)
        wait_until(
            lambda: (cols := scene.indicator_columns(scene.shot("mid-drag"), ghost_x=over[0])) and cols <= expected and cols,
            timeout=5,
            message=f"drop indicator right after {FOCUS!r} (columns {sorted(expected)})",
        )
        krema.wait_keyboard_focus()  # the drag took KWin's activation

    order = [KFIND, FOCUS, KWRITE, TW2]
    wait_until(lambda: krema.item_names() == order, message=lambda: f"AT-SPI order {order} (have {krema.item_names()})")
    # The reordering drop activates no task: focus goes back.
    wait_focus_returned(focus)


@pytest.mark.kremarc(kremarc(KWRITE, KFIND, TW, TW2))
def test_dnd_002_reorder_persists_after_restart(krema: Krema) -> None:
    scene = Scene.capture(krema, [KWRITE, KFIND, TW, TW2])
    start = scene.center(TW2)
    drag(start, [(scene.center(KFIND)[0], start[1])])

    order = [KWRITE, TW2, KFIND, TW]
    wait_until(lambda: krema.item_names() == order, message=lambda: f"AT-SPI order {order} (have {krema.item_names()})")
    saved = [config.launcher(DESKTOP_IDS[n]) for n in order]
    wait_until(lambda: pinned_launchers(krema) == saved, message=lambda: f"kremarc PinnedLaunchers {saved} (have {pinned_launchers(krema)})")

    krema.restart()
    for n in order:
        krema.wait_for_item(n, timeout=15)
    wait_until(lambda: krema.item_names() == order, message=lambda: f"order after restart {order} (have {krema.item_names()})")
    assert pinned_launchers(krema) == saved


@pytest.mark.kremarc(kremarc(KWRITE, KFIND, TW, TW2))
def test_dnd_003_drag_shows_ghost_dimmed_source_and_drop_indicator(krema: Krema) -> None:
    if not kwin.can_capture():
        pytest.fail("KWin cannot capture (QPainter compositing, no /dev/dri render node); DND-003 is a visual check")
    scene = Scene.capture(krema, [KWRITE, KFIND, TW, TW2])
    # Light text-editor icon, dragged backwards onto the first slot: it
    # contrasts both with the black desktop (ghost) and the grey panel (source).
    source, target = TW2, KWRITE
    start = scene.center(source)
    # Above the icon row but inside the dock's hover input region, so the
    # ghost (centred on the pointer) has desktop, not other icons, beneath it.
    over = (scene.center(target)[0], scene.row_top - 16)
    drop = (over[0], start[1])

    with dragging(start, [over, PAUSE_MS, drop]):
        wait_cursor(over)
        expected = scene.indicator_expected(target, after=False)
        seen: dict = {}

        def feedback() -> dict | None:
            shot = scene.shot("mid-drag")
            opacity, corr = scene.ghost(shot, source, over)
            seen.update(
                indicator=sorted(scene.indicator_columns(shot, ghost_x=over[0])),
                source_opacity=round(scene.source_opacity(shot, source), 3),
                ghost_opacity=round(opacity, 3),
                ghost_corr=round(corr, 3),
            )
            ok = (
                seen["indicator"]
                and set(seen["indicator"]) <= expected
                and 0.18 <= seen["source_opacity"] <= 0.42  # DockItem.qml: isDragSource -> 0.3
                and 0.65 <= seen["ghost_opacity"] <= 0.95  # main.qml dragGhost opacity 0.8
                and seen["ghost_corr"] >= 0.85  # the ghost shows the source's icon
            )
            return dict(seen) if ok else None

        wait_until(feedback, timeout=5, message=lambda: f"drag feedback (indicator {sorted(expected)}): {seen}")
        # Zoom is off during the drag: the pointer is in the zoom zone right
        # above an item, yet every item keeps its base size.
        rects = {n: painted_rect(r, scene.rects[n]) for n, r in scene.item_rects().items()}
        assert all(r.width == ICON for r in rects.values()), f"items zoomed during drag: {rects}"

    order = [TW2, KWRITE, KFIND, TW]
    wait_until(lambda: krema.item_names() == order, message=lambda: f"AT-SPI order {order} (have {krema.item_names()})")


@pytest.mark.kremarc(kremarc(KWRITE, KFIND))
def test_dnd_004_drag_released_outside_dock_keeps_order(krema: Krema, apps: TestWindows) -> None:
    alpha = open_active_window(apps, "Alpha")  # unpinned window task
    scene = Scene.capture(krema, [KWRITE, KFIND, "Alpha"])
    rc_before = krema.config_path.read_text()
    start = scene.center("Alpha")
    over = (scene.center(KWRITE)[0], start[1])
    outside = (over[0], 450)  # well above the dock surface

    with dragging(start, [over, PAUSE_MS, outside]):
        wait_cursor(over)
        expected = scene.indicator_expected(KWRITE, after=False)
        wait_until(
            lambda: (cols := scene.indicator_columns(scene.shot("mid-drag"), ghost_x=over[0])) and cols <= expected and cols,
            timeout=5,
            message="drag in progress with a pending reorder before the first item",
        )
        krema.wait_keyboard_focus()  # the drag took KWin's activation

    # The drag ended: no indicator, the dock looks as before the drag.
    def at_rest() -> bool:
        shot = scene.shot("after-drop")
        return not scene.indicator_columns(shot) and scene.item_rects() == scene.rects

    wait_until(at_rest, timeout=5, message=lambda: f"dock back at rest (rects {scene.item_rects()} vs {scene.rects})")
    assert krema.item_names() == [KWRITE, KFIND, "Alpha"]
    assert krema.config_path.read_text() == rc_before
    wait_focus_returned(alpha)


@pytest.mark.kremarc(kremarc(KWRITE, KFIND, TW, TW2))
def test_dnd_004_escape_cancels_drag(krema: Krema, apps: TestWindows) -> None:
    focus = open_active_window(apps, FOCUS)
    scene = Scene.capture(krema, [KWRITE, KFIND, FOCUS, TW2])
    rc_before = krema.config_path.read_text()
    start = scene.center(KWRITE)
    over = (scene.center(FOCUS)[0], start[1])

    with dragging(start, [over, PAUSE_MS]):
        wait_cursor(over)
        expected = scene.indicator_expected(FOCUS, after=True)
        wait_until(
            lambda: (cols := scene.indicator_columns(scene.shot("mid-drag"), ghost_x=over[0])) and cols <= expected and cols,
            timeout=5,
            message="drag in progress with a pending reorder",
        )
        krema.wait_keyboard_focus()  # Escape goes to the dock, not to the focus window
        inp.key("Escape")
        wait_until(
            lambda: not scene.indicator_columns(scene.shot("after-escape"), ghost_x=over[0]),
            timeout=3,
            message="Escape to cancel the drag (drop indicator gone)",
        )

    assert krema.item_names() == [KWRITE, KFIND, FOCUS, TW2]
    assert krema.config_path.read_text() == rc_before
    wait_focus_returned(focus)


# --------------------------------------------------------------- DND-005/006
DRAG_CLICK_ACTION_CASES = [(single, grouped) for single in (0, 1) for grouped in (0, 1, 2)]


def _configure_drag_click_actions(krema: Krema, single: int, grouped: int) -> None:
    """Keep drag checks on the same six policy combinations as click tests."""
    krema.write_config(
        {
            **kremarc(KWRITE, KFIND, TW, TW2),
            "PreviewEnabled": False,
            "SingleWindowClickAction": single,
            "GroupedWindowClickAction": grouped,
        }
    )
    krema.restart()
    wait_until(
        lambda: krema.read_config().get("General", {}).get("SingleWindowClickAction") == str(single)
        and krema.read_config().get("General", {}).get("GroupedWindowClickAction") == str(grouped),
        message=f"drag policy pair persisted ({single}, {grouped})",
    )


@pytest.mark.parametrize(
    ("single", "grouped"),
    DRAG_CLICK_ACTION_CASES,
    ids=[f"single{single}-group{grouped}" for single, grouped in DRAG_CLICK_ACTION_CASES],
)
def test_dnd005_release_inside_outside_and_exit_reenter_preserves_window_state(
    krema: Krema, apps: TestWindows, single: int, grouped: int
) -> None:
    """Drag release never routes through a configured click action."""
    _configure_drag_click_actions(krema, single, grouped)
    alpha = apps.open("Alpha", app_id=env.TEST_APP_ID, width=300, height=200)
    beta = apps.open("Beta", app_id=env.TEST_APP_ID, width=300, height=200)
    solo = apps.open("Solo", app_id=env.TEST_APP2_ID, width=300, height=200)
    focus = apps.open(FOCUS, app_id=DESKTOP_IDS[KWRITE], width=300, height=200)
    for win in (alpha, beta, solo, focus):
        kwin.evaluate(
            f"const w = workspace.windowList().find(w => String(w.internalId) === {win.internal_id!r});"
            "w.frameGeometry = {x: 0, y: 0, width: 300, height: 200}; report(true);"
        )
    for name in (TW, "Solo", FOCUS):
        krema.wait_for_item(name)

    def states() -> dict[str, tuple[bool, bool]]:
        return {w.internal_id: (w.active, w.minimized) for w in kwin.app_windows()}

    def observation() -> tuple[dict[str, tuple[bool, bool]], bool, set[str]]:
        return (
            states(),
            krema.preview_visible(),
            {w.internal_id for w in krema.windows() if not w.normal_window},
        )

    # Active sources catch accidental minimize/cycle. Background sources catch
    # accidental activation by requiring the unrelated Focus window to survive.
    for source, active in ((TW, alpha), (TW, focus), ("Solo", solo), ("Solo", focus)):
        for release_kind in ("inside", "outside", "exit-reenter"):
            kwin.activate(active.internal_id)
            wait_until(active.is_active, message=f"{active.title!r} active before dragging {source!r}")
            scene = Scene.capture(krema, wait_stable(krema.item_names))
            krema.hover_item(source)
            start = wait_stable(
                lambda: painted_rect(krema.screen_rect(krema.item(source)), scene.rects[source]).center
            )
            current = painted_rect(krema.screen_rect(krema.item(source)), scene.rects[source])
            assert current.contains(*start), f"zoomed hit point {start} outside current {source!r} bounds {current}"
            outside = (env.SCREEN_WIDTH // 2, 20)
            inside = (start[0] + current.width, start[1])
            dock = krema.surface_rect("dock")
            assert dock is not None and dock.contains(*start) and dock.contains(*inside), (
                f"horizontal latch path {start} -> {inside} left the actual dock surface {dock}"
            )
            if release_kind == "inside":
                legs: list[tuple[int, int] | int] = [inside, start]
            elif release_kind == "outside":
                legs = [inside, outside]
            else:
                legs = [inside, outside, start]

            before = observation()
            drag(start, legs)
            # This helper only waits for KWin's active state; it does not issue
            # activation or otherwise repair a ghost click.
            wait_focus_returned(active)
            assert_stays(
                observation,
                before,
                1.0,
                f"state changed after {source!r} {release_kind} drag",
            )

            if source == TW and active is alpha and release_kind == "outside":
                # The first fresh right press after an outside drop must not
                # be swallowed by the retained drag-release click guard.
                click = krema.settled_item_center(source)
                menu = krema.open_context_menu(source)
                assert menu.pid == krema.pid and not menu.normal_window
                area = Rect(*menu.client_geometry)
                work = Rect(
                    *(
                        round(v)
                        for v in kwin.evaluate(
                            "const a = workspace.clientArea(KWin.PlacementArea, workspace.activeScreen, workspace.currentDesktop);"
                            "report([a.x, a.y, a.width, a.height]);"
                        )
                    )
                )
                anchor = (click[0], min(click[1], work.y + work.height - 1))
                grown = Rect(area.x - 2, area.y - 2, area.width + 4, area.height + 4)
                assert grown.contains(*anchor), f"fresh right-click menu {area} not anchored at {anchor}"
                assert area.x >= 0 and area.y >= 0
                assert area.x + area.width <= env.SCREEN_WIDTH and area.y + area.height <= env.SCREEN_HEIGHT

                inp.key("Escape")
                wait_until(
                    lambda: all(w.internal_id != menu.internal_id for w in kwin.windows()),
                    timeout=5,
                    message="fresh right-click native menu to close on Escape",
                )
                wait_focus_returned(active)
                assert_stays(observation, before, 1.0, "fresh right-click changed state after drag")

    # All button chains use the session's same persistent fake-input device.
    # A secondary press while Left is held is not a new click cycle, and
    # releasing either button first must not consume the drag-release guard.
    for source, active in ((TW, alpha), ("Solo", solo)):
        for release_order in (("right", "left"), ("left", "right")):
            kwin.activate(active.internal_id)
            wait_until(active.is_active, message=f"{active.title!r} active before the chord drag")
            scene = Scene.capture(krema, wait_stable(krema.item_names))
            krema.hover_item(source)
            start = wait_stable(
                lambda: painted_rect(krema.screen_rect(krema.item(source)), scene.rects[source]).center
            )
            current = painted_rect(krema.screen_rect(krema.item(source)), scene.rects[source])
            assert current.contains(*start), f"chord start {start} outside current {source!r} bounds {current}"
            outside = (env.SCREEN_WIDTH // 2, 20)
            inside = (start[0] + current.width, start[1])
            dock = krema.surface_rect("dock")
            assert dock is not None and dock.contains(*start) and dock.contains(*inside), (
                f"chord latch path {start} -> {inside} left the actual dock surface {dock}"
            )
            before = observation()
            held: list[str] = []
            try:
                inp.press("left", *start)
                held.append("left")
                inp.run_actions(_pointer([_pause(HOLD_MS)]))
                inp.move_path(inp.line(start, inside, 8), step_ms=STEP_MS)
                wait_cursor(inside)
                krema.wait_keyboard_focus()
                inp.move_path(inp.line(inside, outside, 8), step_ms=STEP_MS)
                wait_cursor(outside)
                inp.move_path(inp.line(outside, start, 8), step_ms=STEP_MS)
                wait_cursor(start)
                inp.press("right")
                held.append("right")
                for button in release_order:
                    inp.release(button)
                    held.remove(button)
            finally:
                for button in reversed(held):
                    inp.release(button)

            wait_focus_returned(active)
            assert_stays(
                observation,
                before,
                1.0,
                f"{source!r} chord release order {release_order} changed state",
            )
