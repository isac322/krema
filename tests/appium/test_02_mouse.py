# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""tests/e2e/scenarios/02-mouse-interaction.md (MOUSE-001..007, MOUSE-009) with
real pointer input through KWin fake-input and KWin / AT-SPI / screenshot oracles."""

from __future__ import annotations

import os
import signal
import time
from pathlib import Path
from typing import Iterator

import numpy as np
import pytest
from PIL import Image

from krema_e2e import config, env, kwin
from krema_e2e import input as inp
from krema_e2e.krema import ITEMS_XPATH, DescriptionChanges, Krema, Rect, painted_rect
from krema_e2e.waits import wait_stable, wait_until
from krema_e2e.windows import TestWindows

APP1, APP2 = env.TEST_APP_ID, env.TEST_APP2_ID
NAME1, NAME2 = env.TEST_APP_NAME, env.TEST_APP2_NAME


def _require_capture() -> None:
    if not kwin.can_capture():
        pytest.skip(f"KWin compositing is {kwin.compositing_type()}: no DRM render node, ScreenShot2 needs OpenGL")


# --------------------------------------------------------------------- helpers
def _app_windows(app_id: str) -> list[kwin.Window]:
    return [w for w in kwin.app_windows() if w.app_id == app_id]


def _launched_pids() -> list[int]:
    """PIDs of processes krema launched from .desktop files: fixture windows
    started without ``--title`` (TestWindows always passes one) and the
    slow-launch wrapper shell."""
    out = []
    for d in Path("/proc").iterdir():
        if not d.name.isdigit():
            continue
        try:
            argv = (d / "cmdline").read_bytes().split(b"\0")
        except OSError:
            continue
        exe = os.path.basename(argv[0].decode(errors="replace"))
        if exe == env.TEST_WINDOW_BINARY and b"--title" not in argv:
            out.append(int(d.name))
        elif exe == "sh" and any(SLOW_ID.encode() in a for a in argv):
            out.append(int(d.name))
    return out


def _kill_launched() -> None:
    for pid in _launched_pids():
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    wait_until(lambda: not _launched_pids(), message="krema-launched processes to exit")


@pytest.fixture(autouse=True)
def _no_leaked_launches(apps: TestWindows) -> Iterator[None]:
    """Windows krema launches are not owned by ``apps``: kill them after each
    test so they cannot join the next test's task groups."""
    yield
    _kill_launched()
    wait_until(
        lambda: not [w for w in kwin.app_windows() if w.pid not in {tw.pid for tw in apps.open_windows}],
        message="krema-launched windows to disappear",
    )


#: A launcher whose app takes 3 s to map its window, so the launch feedback
#: (DockItem ``launching`` -> bounce + "Starting" description) lasts long
#: enough to observe deterministically. Installed in krema's private
#: XDG_DATA_HOME before krema starts.
SLOW_ID = "org.kde.krema.mouseslow"
SLOW_NAME = "Krema Slow Launch"
SLOW_DELAY_S = 3


def _install_slow_launcher(krema: Krema) -> None:
    apps_dir = krema.home / "data" / "applications"
    apps_dir.mkdir(parents=True, exist_ok=True)
    (apps_dir / f"{SLOW_ID}.desktop").write_text(
        "[Desktop Entry]\n"
        "Type=Application\n"
        f"Name={SLOW_NAME}\n"
        f'Exec=sh -c "sleep {SLOW_DELAY_S}; exec {env.TEST_WINDOW_BINARY} --app-id {SLOW_ID}"\n'
        "Icon=utilities-terminal\n"
        f"StartupWMClass={SLOW_ID}\n"
        "StartupNotify=true\n"
    )


def _has_description(krema: Krema, name: str, part: str) -> bool:
    """Whether dock item ``name``'s accessible description contains ``part``.
    (XPath on @description: the webdriver's get_attribute("description")
    returns None.)"""
    return krema.find(f"{ITEMS_XPATH}[@name='{name}'][contains(@description, '{part}')]") is not None


def _icon_lift(rest: np.ndarray, shot: np.ndarray, item: Rect, max_dy: int = 12) -> int:
    """Vertical displacement (px, positive = up) of the icon pixels in
    ``shot`` relative to ``rest``: the shift that best aligns the icon area."""
    cx = item.x + item.width // 2
    xs = slice(cx - item.width // 3, cx + item.width // 3)
    y0, y1 = item.y + max_dy, item.y + item.height - 8
    ref = rest[y0:y1, xs]
    errs = [np.abs(shot[y0 - dy : y1 - dy, xs] - ref).mean() for dy in range(max_dy + 1)]
    return int(np.argmin(errs))


def _max_lift_while_launching(krema: Krema, ref: np.ndarray, item: Rect, app_id: str, windows_before: int, tag: str) -> int:
    """Screenshot the dock right after a launch click until the new window is
    mapped plus 1 s; return the largest icon lift seen (the launch bounce
    moves the icon up to 8 px, scaled by zoom, away from the dock edge)."""
    lift, i, mapped_at = 0, 0, None
    deadline = time.monotonic() + SLOW_DELAY_S + 8
    while time.monotonic() < deadline and (mapped_at is None or time.monotonic() < mapped_at + 1):
        lift = max(lift, _icon_lift(ref, _pixels(krema.screenshot(f"{tag}-{i}", item)), item))
        i += 1
        if mapped_at is None and len(_app_windows(app_id)) > windows_before:
            mapped_at = time.monotonic()
    return lift


def _pixels(image: Image.Image) -> np.ndarray:
    return np.asarray(image.convert("RGB"), dtype=np.int16)


def _dock_area(krema: Krema) -> Rect:
    """Screen rect of the dock surface: where its items and their indicator
    dots are drawn (the part of the screen the dot oracles read)."""
    return wait_until(lambda: krema.surface_rect("dock"), message="dock surface geometry")


def _changed(a: np.ndarray, b: np.ndarray, tolerance: int = 24) -> np.ndarray:
    """Boolean mask of pixels whose max channel difference exceeds ``tolerance``."""
    return np.abs(a - b).max(axis=2) > tolerance


def _bbox(mask: np.ndarray) -> Rect | None:
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None
    return Rect(int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1))


def _indicator_strip(item: Rect) -> tuple[slice, slice]:
    """Screen region where DockItem.qml draws its indicator dots on a bottom
    dock: the ``_indicatorSpace`` band under the icon (last ~8 px of the item
    cell), horizontally centred, wide enough for 3 dots (3x4 + 2 spacing)."""
    cx = item.x + item.width // 2
    bottom = item.y + item.height
    return slice(bottom - 7, bottom), slice(cx - 14, cx + 15)


def _dot_pixels(krema: Krema, name: str, shot: np.ndarray, baseline: np.ndarray) -> int:
    ys, xs = _indicator_strip(krema.screen_rect(krema.item(name)))
    return int(_changed(shot[ys, xs], baseline[ys, xs]).sum())


def _item_rects(krema: Krema) -> list[Rect]:
    return wait_stable(lambda: [Rect.of(e) for e in krema.items()])


def _painted_rects(krema: Krema, rest: list[Rect]) -> list[Rect]:
    """Where each dock item is drawn (surface-local), given its rest rects."""
    return [painted_rect(r, r0) for r, r0 in zip([Rect.of(e) for e in krema.items()], rest, strict=True)]


def _settled_painted_rects(krema: Krema, rest: list[Rect]) -> list[Rect]:
    return wait_stable(lambda: _painted_rects(krema, rest))


def _background_span(krema: Krema, y: int, tag: str) -> tuple[int, int]:
    """Screen x extent [left, right) of the dock background on screen row
    ``y`` (an item's centre row: below the panel's rounded corners, with
    only the black desktop around it; icons lie inside the background)."""
    row = _pixels(krema.screenshot(tag, Rect(0, y, env.SCREEN_WIDTH, 1)))[y]
    xs = np.nonzero(row.sum(axis=1) > 30)[0]
    assert len(xs), f"no dock background on screen row {y}"
    return int(xs.min()), int(xs.max()) + 1


def _monotonic(values: list[int], tolerance: int = 2) -> bool:
    """True if ``values`` never reverses direction by more than ``tolerance``."""
    lo = hi = values[0]
    up = down = False
    for v in values[1:]:
        up = up or v > lo + tolerance
        down = down or v < hi - tolerance
        lo, hi = min(lo, v), max(hi, v)
    return not (up and down)


# ------------------------------------------------------------------ MOUSE-001
def test_mouse001_left_click_activates_and_unminimizes_running_app(krema: Krema, apps: TestWindows) -> None:
    first = apps.open("First", app_id=APP1)
    second = apps.open("Second", app_id=APP2)
    krema.wait_for_item("First")
    krema.wait_for_item("Second")
    kwin.set_minimized(first.internal_id, True)
    wait_until(lambda: (w := first.refresh()) is not None and w.minimized, message="First to be minimized")
    wait_until(second.is_active, message="Second to be active while First is minimized")

    krema.click_item("First")

    active = wait_until(
        lambda: (w := first.refresh()) is not None and w.active and not w.minimized and w,
        message=lambda: f"First to be un-minimized and active (First: {first.refresh()}, active: {kwin.active_window()})",
    )
    assert kwin.active_window().pid == first.pid
    assert active.title == "First"


def test_mouse001_click_without_motion_on_an_item_that_appeared_under_the_pointer(krema: Krema, apps: TestWindows) -> None:
    """A user whose pointer rests on the dock where an app's icon then
    appears clicks without moving: the click must go to that icon."""
    probe = apps.open("Probe", app_id=APP1)
    spot = wait_stable(lambda: krema.item_center("Probe"), duration=0.5)
    apps.close(probe)
    krema.wait_for_no_item("Probe")
    # The dock is empty: whatever the pointer enters here, no item is under it.
    inp.move(*spot)

    first = apps.open("First", app_id=APP1)
    krema.wait_for_item("First")
    assert wait_stable(lambda: krema.item_center("First"), duration=0.5) == spot, "item reappears under the pointer"
    kwin.set_minimized(first.internal_id, True)
    wait_until(lambda: (w := first.refresh()) is not None and w.minimized, message="First to be minimized")
    assert kwin.cursor_pos() == spot

    inp.click()  # at the current position: no motion event

    wait_until(
        lambda: (w := first.refresh()) is not None and w.active and not w.minimized,
        message=lambda: f"First to be un-minimized and active (First: {first.refresh()})",
    )


# ------------------------------------------------------------------ MOUSE-002
#: No tooltip/preview while the pointer rests on the item (keeps the icon
#: area of the screenshots comparable to the pre-click reference).
QUIET_HOVER = {"PreviewEnabled": False, "PreviewHoverDelay": 600000}


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher(SLOW_ID)], **QUIET_HOVER})
def test_mouse002_left_click_launches_pinned_app(krema: Krema) -> None:
    _require_capture()
    _install_slow_launcher(krema)
    krema.start()
    krema.wait_for_item(SLOW_NAME)
    assert _app_windows(SLOW_ID) == []
    krema.move_away(close_preview=False)
    dock = _dock_area(krema)
    idle = _pixels(krema.screenshot("pinned-only", dock))

    krema.click_item(SLOW_NAME)

    launched = wait_until(lambda: _app_windows(SLOW_ID), timeout=SLOW_DELAY_S + 10, message="pinned app window in KWin")
    assert len(launched) == 1, f"exactly one new window of the pinned app: {launched}"
    # The launcher merged into the running task (hideActivatedLaunchers):
    # still one item, now the pinned running app, named by its window title.
    wait_until(lambda: krema.item_names() == [launched[0].title], message="launcher to become the running task")
    assert _has_description(krema, launched[0].title, "Pinned")
    krema.move_away(close_preview=False)
    wait_until(
        lambda: _dot_pixels(krema, launched[0].title, _pixels(krema.screenshot("running", dock)), idle) >= 4,
        timeout=5,
        message="indicator dot to appear under the launched app",
    )


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher(SLOW_ID)], **QUIET_HOVER})
def test_mouse002_pinned_launch_bounces(krema: Krema) -> None:
    _require_capture()
    _install_slow_launcher(krema)
    krema.start()
    rest = wait_stable(lambda: krema.screen_rect(krema.item(SLOW_NAME)))
    krema.hover_item(SLOW_NAME)
    # The hovered icon is zoomed: where it is drawn comes from painted_rect
    # (Qt < 6.9 reports zoomed extents with the unscaled size).
    item = wait_stable(lambda: painted_rect(krema.screen_rect(krema.item(SLOW_NAME)), rest), duration=0.3)
    ref = _pixels(krema.screenshot("hovered", item))

    # Click where the pointer already rests: a click at a centre recomputed
    # from the zoomed extents would move the pointer and the zoom layout.
    inp.click()

    lift = _max_lift_while_launching(krema, ref, item, SLOW_ID, 0, "launch")
    assert len(_app_windows(SLOW_ID)) == 1, "the app launched"
    assert lift >= 3, f"icon bounced while the app was starting (max lift {lift}px)"


# ------------------------------------------------------ MOUSE-003 / MOUSE-009
ZOOM_LAUNCHERS = [APP1, APP2, "org.kde.kwrite", "org.kde.kfind", "qt6-designer", "qt6-linguist", "org.kde.kiod6"]
ZOOM_KREMARC = {"PinnedLaunchers": [config.launcher(i) for i in ZOOM_LAUNCHERS], "MaxZoomFactor": 1.6}


def _zoom_rest_layout(krema: Krema) -> tuple[list[Rect], int, Rect]:
    """Rest rects of the 7 pinned items, their common size and the dock
    surface's screen rect."""
    wait_until(lambda: len(krema.items()) == len(ZOOM_LAUNCHERS), message="7 pinned items")
    krema.move_away(close_preview=False)
    rest = _item_rects(krema)
    widths = [r.width for r in rest]
    assert len(set(widths)) == 1, f"all items at base size before hover: {widths}"
    dock = wait_until(lambda: krema.surface_rect("dock"), message="dock surface geometry")
    return rest, widths[0], dock


def _assert_parabolic_falloff(w: list[int], base: int, mid: int) -> None:
    # Hovered item is zoomed close to MaxZoomFactor (pointer at its centre).
    assert w[mid] >= base * 1.5, f"hovered item zoomed to ~1.6x: {w}"
    # Zoom decreases with distance, symmetrically on both sides.
    assert w[mid] > w[mid - 1] > w[mid - 2] >= base, f"left falloff: {w}"
    assert w[mid] > w[mid + 1] > w[mid + 2] >= base, f"right falloff: {w}"
    assert w[mid - 1] > base + 4, f"neighbours at intermediate zoom: {w}"
    for d in (1, 2, 3):
        assert abs(w[mid - d] - w[mid + d]) <= 1, f"falloff not symmetric at distance {d}: {w}"
    # Items three icons away stay at base size.
    assert abs(w[0] - base) <= 1 and abs(w[-1] - base) <= 1, f"far items at base size: {w}"


@pytest.mark.kremarc(ZOOM_KREMARC)
def test_mouse003_parabolic_zoom_on_hover(krema: Krema) -> None:
    """Default zoom style: magnified icons push their neighbours aside with
    constant gaps, the background grows to contain them, a sweep across the
    middle keeps the background edges and far icons still, and toward an end
    each background edge moves one way only."""
    _require_capture()
    rest, base, dock = _zoom_rest_layout(krema)
    rest_gap = rest[1].x - (rest[0].x + base)
    row_y = dock.y + rest[0].center[1]
    rest_bg = _background_span(krema, row_y, "rest")
    mid = len(rest) // 2

    krema.hover_item(krema.item_names()[mid])
    p = _settled_painted_rects(krema, rest)
    w = [r.width for r in p]
    _assert_parabolic_falloff(w, base, mid)

    # The hovered icon stays under the pointer; every other icon moves outward.
    pointer_x = inp.pointer_position()[0] - dock.x
    assert abs(p[mid].center[0] - pointer_x) <= 2, f"hovered icon centre {p[mid].center[0]} left the pointer at {pointer_x}"
    for i, (r, r0) in enumerate(zip(p, rest)):
        if i != mid:
            shift = r.center[0] - r0.center[0]
            assert shift * (i - mid) > 2, f"item {i} not pushed aside (centre shift {shift}): {p}"
    # Gaps between the drawn icons stay at the rest spacing: no overlap.
    gaps = [b.x - (a.x + a.width) for a, b in zip(p, p[1:])]
    assert all(abs(g - rest_gap) <= 2 for g in gaps), f"gaps {gaps}, rest spacing {rest_gap}"
    # The background grows by the row's growth, half on each side for a middle icon.
    growth = sum(x - base for x in w)
    bg = _background_span(krema, row_y, "zoomed")
    assert abs((rest_bg[0] - bg[0]) - growth / 2) <= 3, f"left edge {rest_bg[0]} -> {bg[0]}, row growth {growth}"
    assert abs((bg[1] - rest_bg[1]) - growth / 2) <= 3, f"right edge {rest_bg[1]} -> {bg[1]}, row growth {growth}"

    # Sweep across the middle icons: background edges and far icons stay still.
    x_left, x_right, x_end = (dock.x + rest[i].center[0] for i in (mid - 1, mid + 1, len(rest) - 1))
    edges, far = [], []
    for i, x in enumerate(range(x_left, x_right + 1, 8)):
        inp.move(x, row_y)
        q = _settled_painted_rects(krema, rest)
        far.append((q[0].x, q[-1].x + q[-1].width))
        edges.append(_background_span(krema, row_y, f"sweep-{i}"))
    for side, name in ((0, "left"), (1, "right")):
        for label, seq in (("background edge", [e[side] for e in edges]), ("far icon edge", [f[side] for f in far])):
            assert max(seq) - min(seq) <= 2, f"{name} {label} moved during the middle sweep: {seq}"

    # Toward the right end: each background edge moves smoothly, one way only.
    trail = []
    for i, x in enumerate(range(x_right, x_end + 1, 8)):
        inp.move(x, row_y)
        _settled_painted_rects(krema, rest)
        trail.append(_background_span(krema, row_y, f"end-{i}"))
    for side, name in ((0, "left"), (1, "right")):
        seq = [t[side] for t in trail]
        assert _monotonic(seq), f"{name} background edge went back and forth toward the end: {seq}"
        assert max(abs(b - a) for a, b in zip(seq, seq[1:])) <= 12, f"{name} background edge jumped: {seq}"

    krema.move_away(close_preview=False)
    wait_until(lambda: _painted_rects(krema, rest) == rest, message="icons to return to the rest layout")
    wait_until(lambda: _background_span(krema, row_y, "after") == rest_bg, message="background to return to its rest extent")


@pytest.mark.kremarc({**ZOOM_KREMARC, "ZoomStyle": 1})
def test_mouse009_in_place_zoom_scales_icons_without_moving_them(krema: Krema) -> None:
    _require_capture()
    rest, base, dock = _zoom_rest_layout(krema)
    row_y = dock.y + rest[0].center[1]
    rest_bg = _background_span(krema, row_y, "rest")
    mid = len(rest) // 2

    krema.hover_item(krema.item_names()[mid])
    p = _settled_painted_rects(krema, rest)
    _assert_parabolic_falloff([r.width for r in p], base, mid)
    for i, (r, r0) in enumerate(zip(p, rest)):
        assert abs(r.center[0] - r0.center[0]) <= 2, f"item {i} moved: centre {r0.center[0]} -> {r.center[0]}"
    assert p[mid - 1].x + p[mid - 1].width > p[mid].x, f"left neighbour does not overlap the magnified icon: {p}"
    assert p[mid + 1].x < p[mid].x + p[mid].width, f"right neighbour does not overlap the magnified icon: {p}"
    bg = _background_span(krema, row_y, "zoomed")
    assert abs(bg[0] - rest_bg[0]) <= 1 and abs(bg[1] - rest_bg[1]) <= 1, f"background {rest_bg} -> {bg}"

    krema.move_away(close_preview=False)
    wait_until(lambda: _painted_rects(krema, rest) == rest, message="icons to return to base size")


# ------------------------------------------------------------------ MOUSE-004
@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher(APP1), config.launcher(APP2)], "MaxZoomFactor": 1.0})
def test_mouse004_tooltip_shows_app_name_on_hover(krema: Krema) -> None:
    """The tooltip is ``Accessible.ignored`` (not in AT-SPI), so the oracle is
    the screenshot. Zoom is disabled (MaxZoomFactor 1.0) so the only pixels a
    hover changes are the tooltip's. It must appear just above the hovered
    item, centred on it, sized like a one-line label, containing text-like
    contrasting glyph pixels, and its width must scale with the length of the
    hovered app's name (OCR-free check that it shows *that* name)."""
    _require_capture()
    krema.wait_for_item(NAME1)
    krema.wait_for_item(NAME2)
    krema.move_away(close_preview=False)
    baseline = _pixels(krema.screenshot("no-tooltip"))

    tips = {}
    for name in (NAME1, NAME2):
        krema.hover_item(name)
        item = krema.screen_rect(krema.item(name))
        tip = wait_until(
            lambda: _bbox(_changed(_pixels(krema.screenshot(f"tooltip-{name}")), baseline)),
            timeout=5,
            message=f"tooltip for {name!r} to be painted",
        )
        # Settle: tooltip drawn fully (identical bbox in two consecutive shots).
        tip = wait_stable(lambda: _bbox(_changed(_pixels(krema.screenshot(f"tooltip-{name}")), baseline)), duration=0.3)
        shot = _pixels(Image.open(env.artifact_path(f"{krema.name}/tooltip-{name}.png")))
        assert tip is not None
        assert tip.y + tip.height <= item.y, f"tooltip {tip} above item {item}"
        assert item.y - (tip.y + tip.height) <= 40, f"tooltip {tip} close to item {item}"
        assert abs(tip.center[0] - item.center[0]) <= 3, f"tooltip {tip} centred on item {item}"
        assert 14 <= tip.height <= 48, f"one text line high: {tip}"
        region = shot[tip.y : tip.y + tip.height, tip.x : tip.x + tip.width]
        lum = region.mean(axis=2)
        bg = np.median(lum)
        glyph = np.abs(lum - bg) > 80
        cols = np.nonzero(glyph.any(axis=0))[0]
        assert glyph.sum() >= 40, f"tooltip contains text glyphs ({glyph.sum()} contrasting px)"
        text_span = int(cols.max() - cols.min() + 1)
        assert text_span >= tip.width * 0.6, f"glyphs span the label width ({text_span}/{tip.width})"
        tips[name] = (tip, text_span)
        krema.move_away(close_preview=False)
        wait_until(
            lambda: _bbox(_changed(_pixels(krema.screenshot("no-tooltip-again")), baseline)) is None,
            timeout=5,
            message="tooltip to disappear after leaving",
        )

    # "Krema Second Test Window" (24 chars) vs "Krema Test Window" (17 chars).
    ratio = tips[NAME2][1] / tips[NAME1][1]
    expected = len(NAME2) / len(NAME1)
    assert abs(ratio - expected) <= 0.2 * expected, f"text width ratio {ratio:.2f} ~ name length ratio {expected:.2f}"


# ------------------------------------------------------------------ MOUSE-005
@pytest.mark.kremarc({"PinnedLaunchers": [], "PreviewEnabled": False})
def test_mouse005_scroll_wheel_cycles_grouped_windows(krema: Krema, apps: TestWindows) -> None:
    titles = ["Alpha", "Beta", "Gamma"]
    wins = {t: apps.open(t, app_id=APP1) for t in titles}
    krema.wait_for_item(NAME1)
    # The first read after the item appears can catch the tool bar while it
    # is still regrouping the windows (seen as an empty item list): assert
    # on the settled list.
    names = wait_stable(krema.item_names)
    assert names.count(NAME1) == 1, f"grouped windows not one dock item: {names}"

    def active_title() -> str | None:
        a = kwin.active_window()
        return next((t for t, tw in wins.items() if a is not None and a.pid == tw.pid), None)

    start = wait_until(active_title, message="one of the grouped windows active")
    krema.hover_item(NAME1)
    seen = [start]
    for _ in range(len(titles)):
        x, y = krema.item_center(NAME1)
        inp.scroll(x, y, dy=15)  # one notch down -> next window
        prev = seen[-1]
        seen.append(wait_until(lambda: (t := active_title()) != prev and t, message=f"scroll to switch away from {prev!r}"))

    assert set(seen[:3]) == set(titles), f"each scroll activates a different window of the group: {seen}"
    assert seen[3] == seen[0], f"cycling wraps around to the first window: {seen}"

    # Scrolling up walks the same cycle backwards.
    x, y = krema.item_center(NAME1)
    inp.scroll(x, y, dy=-15)
    back = wait_until(lambda: (t := active_title()) != seen[3] and t, message="scroll up to switch window")
    assert back == seen[2], f"scroll up goes to the previous window: {seen} -> {back}"


# ------------------------------------------------------------------ MOUSE-006
# The launch bounce runs while DockItem.launching is true, which is exactly
# while the item's description carries "Starting". A window item never gets a
# startup task (TasksModel filters those of apps with a window), so its feedback
# is observed as AT-SPI description events recorded from before the click,
# which needs no screenshot capture.
def _starting_seen(changes: DescriptionChanges) -> bool:
    return any("Starting" in t for t in changes.texts("krema"))


def _starting_ended(changes: DescriptionChanges) -> bool:
    """Whether a description with "Starting" was followed by one without."""
    texts = changes.texts("krema")
    first = next((i for i, t in enumerate(texts) if "Starting" in t), None)
    return first is not None and any("Starting" not in t for t in texts[first + 1 :])


def _open_slow_original(krema: Krema, apps: TestWindows):
    _install_slow_launcher(krema)
    krema.start()
    original = apps.open("Original", app_id=SLOW_ID)
    krema.wait_for_item("Original")
    assert [w.pid for w in _app_windows(SLOW_ID)] == [original.pid]
    return original


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [], **QUIET_HOVER})
def test_mouse006_middle_click_launches_new_instance(krema: Krema, apps: TestWindows) -> None:
    original = _open_slow_original(krema, apps)
    changes = DescriptionChanges()
    try:
        krema.click_item("Original", button="middle")

        wait_until(
            lambda: _starting_seen(changes),
            timeout=5,
            message=lambda: f"dock item to start the launch feedback (bounce) (descriptions seen: {changes.texts('krema')})",
        )
        wins = wait_until(
            lambda: len(_app_windows(SLOW_ID)) == 2 and _app_windows(SLOW_ID),
            timeout=SLOW_DELAY_S + 10,
            message="the new instance's window in KWin",
        )
    finally:
        changes.close()
    assert len({w.pid for w in wins}) == 2, f"one more window, in a separate process: {wins}"
    assert original.refresh() is not None, "the original window is kept"
    krema.wait_for_item(SLOW_NAME)  # the two windows are grouped under the .desktop Name
    assert krema.item_names() == [SLOW_NAME]


class LaunchFeedbackEndedEarly(AssertionError):
    """The launch feedback ended while the new instance had no window yet."""


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [], **QUIET_HOVER})
def test_mouse006_launch_bounce_lasts_until_the_new_window_maps(krema: Krema, apps: TestWindows) -> None:
    _open_slow_original(krema, apps)
    changes = DescriptionChanges()
    try:
        krema.click_item("Original", button="middle")

        wait_until(
            lambda: _starting_ended(changes),
            timeout=SLOW_DELAY_S + 10,
            message=lambda: f"launch feedback (bounce) to start and end (descriptions seen: {changes.texts('krema')})",
        )
        # krema ends the feedback when the task gains the window, which KWin
        # has mapped by then: queried after the end event, it must be there.
        wins = _app_windows(SLOW_ID)
        if len(wins) != 2:
            raise LaunchFeedbackEndedEarly(f"launch feedback ended before the new instance's window mapped: {wins}")
    finally:
        changes.close()


# ------------------------------------------------------------------ MOUSE-007
@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher(APP1), config.launcher(APP2)], "PreviewEnabled": False})
def test_mouse007_indicator_dots_reflect_running_state(krema: Krema, apps: TestWindows) -> None:
    _require_capture()
    krema.wait_for_item(NAME1)
    krema.wait_for_item(NAME2)
    krema.move_away(close_preview=False)
    pinned = wait_stable(lambda: tuple(krema.screen_rect(e) for e in krema.items()))
    dock = _dock_area(krema)
    baseline = _pixels(krema.screenshot("pinned-only", dock))

    def settle_shot(name: str) -> np.ndarray:
        krema.move_away(close_preview=False)
        wait_stable(lambda: tuple(krema.screen_rect(e) for e in krema.items()))
        return _pixels(krema.screenshot(name, dock))

    one = apps.open(NAME1, app_id=APP1)
    wait_until(
        lambda: krema.item_names() == [NAME1, NAME2] and _has_description(krema, NAME1, "Active"),
        message="pinned launcher to become the running task in place",
    )
    (running,) = wait_until(
        lambda: (s := settle_shot("running")) is not None and _dot_pixels(krema, NAME1, s, baseline) >= 4 and [s],
        timeout=3,
        message="indicator dot under the running app within 1s",
    )
    single = _dot_pixels(krema, NAME1, running, baseline)
    assert _dot_pixels(krema, NAME2, running, baseline) == 0, "pinned-only app has no dot"

    two = apps.open("Second instance", app_id=APP1)
    kwin.activate(one.internal_id)
    (shot,) = wait_until(
        lambda: (s := settle_shot("two-windows")) is not None and _dot_pixels(krema, NAME1, s, baseline) >= 1.6 * single and [s],
        timeout=3,
        message="a second dot for the second window",
    )
    assert _dot_pixels(krema, NAME2, shot, baseline) == 0

    apps.close(two)
    apps.close(one)
    wait_until(lambda: tuple(krema.screen_rect(e) for e in krema.items()) == pinned, message="dock back to pinned-only layout")
    wait_until(
        lambda: _dot_pixels(krema, NAME1, settle_shot("closed"), baseline) == 0,
        timeout=3,
        message="indicator dot removed within 1s of closing",
    )
