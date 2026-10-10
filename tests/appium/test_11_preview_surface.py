# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""PREV-011: native preview surfaces contain their laid-out popup."""

from __future__ import annotations

import json
import time
from dataclasses import asdict

import pytest

from krema_e2e import config, env, kwin
from krema_e2e import input as inp
from krema_e2e import preview as pv
from krema_e2e.krema import Krema, Rect
from krema_e2e.waits import wait_stable, wait_until
from krema_e2e.windows import TestWindow, TestWindows


EDGES = [
    pytest.param(config.EDGE_BOTTOM, id="horizontal"),
    pytest.param(config.EDGE_LEFT, id="left"),
    pytest.param(config.EDGE_RIGHT, id="right"),
]
TITLES = ("Surface A", "Surface B", "Surface C")


def _record(krema: Krema, stage: str, **values) -> None:
    with env.artifact_path(f"{krema.name}/surface-proof.jsonl").open("a") as stream:
        stream.write(json.dumps({"stage": stage, **values}, sort_keys=True) + "\n")


def _contains(outer: Rect, inner: Rect) -> bool:
    return (
        inner.width > 0
        and inner.height > 0
        and outer.x <= inner.x
        and outer.y <= inner.y
        and inner.x + inner.width <= outer.x + outer.width
        and inner.y + inner.height <= outer.y + outer.height
    )


def _assert_popup_inside(krema: Krema, titles: list[str], stage: str, *, shrinking_from: int | None = None) -> tuple[Rect, Rect, str]:
    expected_titles = set(titles)

    def geometry() -> tuple[Rect, Rect, list[tuple[str, Rect]]] | None:
        displayed_titles = pv.thumb_titles(krema)
        displayed_set = set(displayed_titles)
        if len(displayed_titles) != len(titles) or len(displayed_set) != len(titles) or displayed_set != expected_titles:
            return None
        surface = krema.surface_rect("preview")
        popup = krema.preview_popup()
        if surface is None or popup is None:
            return None
        # Containment alone also accepts the larger surface from before a
        # shrink. Wait for KWin's asynchronous configure before recording it.
        if shrinking_from is not None and surface.width >= shrinking_from:
            return None
        popup_rect = pv.screen_rect(krema, popup)
        thumbs = [krema.find(pv.thumb_xpath(title)) for title in displayed_titles]
        if any(thumb is None for thumb in thumbs):
            return None
        rectangles = sorted(
            ((title, pv.screen_rect(krema, thumb)) for title, thumb in zip(displayed_titles, thumbs, strict=True)),
            key=lambda thumbnail: thumbnail[1].x,
        )
        if not _contains(surface, popup_rect) or not all(_contains(surface, rect) for _, rect in rectangles):
            return None
        return surface, popup_rect, rectangles

    timeout = 10.0
    deadline = time.monotonic() + timeout

    def settled() -> tuple[Rect, Rect, list[tuple[str, Rect]]] | None:
        # geometry() samples each element through its own AT-SPI round trip,
        # and every screen_rect re-queries KWin for the surface origin, so a
        # single pass takes tens of ms and can mix pre- and post-layout
        # states: when the group grows, the popup still reports its old
        # smaller rect while the row already shows the new thumbnail. The
        # full-width surface contains both, so the checks inside geometry()
        # pass, and the popup-containment assert below then fails on the
        # torn sample. A real layout pass is frame-paced, so a sample that
        # is unchanged for GEOMETRY_SETTLE is necessarily consistent.
        return wait_stable(
            geometry,
            duration=pv.GEOMETRY_SETTLE,
            timeout=max(pv.GEOMETRY_SETTLE, deadline - time.monotonic()),
        )

    surface, popup, thumbs = wait_until(
        settled,
        timeout=timeout,
        message=f"exact thumbnail membership and entire popup with all {len(titles)} thumbnails to fit the native preview surface"
        + (f" after its width shrinks below {shrinking_from}px" if shrinking_from is not None else ""),
    )
    assert all(_contains(popup, rect) for _, rect in thumbs)
    _record(krema, stage, native_surface=surface._asdict(), popup=popup._asdict(), thumbnails=[rect._asdict() for _, rect in thumbs], titles=[title for title, _ in thumbs])
    last_title, last_thumb = thumbs[-1]
    return surface, last_thumb, last_title


def _open(krema: Krema, windows: list[TestWindow], stage: str) -> tuple[Rect, Rect, str]:
    item = env.TEST_APP_NAME if len(windows) > 1 else windows[0].title
    pv.open_by_hover(krema, item)
    return _assert_popup_inside(krema, [window.title for window in windows], stage)


def _select_last(krema: Krema, windows: list[TestWindow], underlying: TestWindow, stage: str) -> TestWindow:
    kwin.activate(underlying.internal_id)
    wait_until(underlying.is_active, message="wrong underlying client active before preview selection")
    krema.move_away()
    surface, last_thumb, last_title = _open(krema, windows, stage)
    point = last_thumb.center
    before = underlying.refresh()
    assert before is not None and before.active
    assert Rect(before.x, before.y, before.width, before.height).contains(*point)
    expected = next(window for window in windows if window.title == last_title)
    assert before.pid != expected.pid and before.internal_id != expected.internal_id
    assert surface.contains(*point)
    pv.glide_into(krema, point)
    wait_until(lambda: kwin.cursor_pos() == point, message="real pointer on last thumbnail")
    assert krema.preview_visible()
    inp.click(*point)
    active = wait_until(
        lambda: (window if (window := kwin.active_window()) is not None and window.internal_id == expected.internal_id else None),
        message=f"last thumbnail to activate PID {expected.pid}, internalId {expected.internal_id}",
    )
    assert active.pid == expected.pid and active.title == expected.title
    assert active.pid != underlying.pid and not underlying.is_active()
    _record(krema, f"{stage}-activation", before=asdict(before), observed_last_title=last_title, expected_pid=expected.pid, expected_internal_id=expected.internal_id, point=point, active=asdict(active))
    wait_until(lambda: not krema.preview_visible(), message="preview to close after last-thumbnail activation")
    return expected


def _transparent_passthrough(krema: Krema, windows: list[TestWindow], underlying: TestWindow, selected: TestWindow) -> None:
    surface, _, _ = _open(krema, windows, "transparent-pass-through")
    popup = pv.screen_rect(krema, krema.preview_popup())
    clients = [window for window in kwin.windows() if not window.minimized]
    preview_ids = [
        window.internal_id
        for window in clients
        if window.pid == krema.pid
        and Rect(window.client_x, window.client_y, window.client_width, window.client_height) == surface
    ]
    assert len(preview_ids) == 1, f"expected one native preview surface matching {surface}, got {preview_ids}"
    exempt_ids = {preview_ids[0], underlying.internal_id}
    client = next(window for window in clients if window.internal_id == underlying.internal_id)
    assert not client.active
    client_rect = Rect(client.x, client.y, client.width, client.height)
    other_rects = [Rect(window.x, window.y, window.width, window.height) for window in clients if window.internal_id not in exempt_ids]
    candidates = [
        (surface.center[0], surface.y + 20),
        (surface.center[0], surface.y + surface.height - 20),
        (surface.x + 20, surface.center[1]),
        (surface.x + surface.width - 20, surface.center[1]),
    ]
    point = next(
        (
            point
            for point in candidates
            if surface.contains(*point)
            and client_rect.contains(*point)
            and not popup.contains(*point)
            and not any(rect.contains(*point) for rect in other_rects)
        ),
        None,
    )
    assert point is not None, f"no transparent point reaches underlying client: candidates={candidates}, frames={[asdict(window) for window in clients]}, popup={popup}"
    inp.move(*point)
    cursor = wait_until(lambda: (position if (position := kwin.cursor_pos()) == point else None), message="native pointer at transparent pass-through point")
    before = kwin.active_window()
    assert before is not None and before.pid == selected.pid and before.internal_id == selected.internal_id
    assert before.pid != underlying.pid and before.internal_id != underlying.internal_id
    clients = [window for window in kwin.windows() if not window.minimized]
    client = next(window for window in clients if window.internal_id == underlying.internal_id)
    assert not client.active
    assert Rect(client.x, client.y, client.width, client.height).contains(*cursor)
    assert all(not Rect(window.x, window.y, window.width, window.height).contains(*cursor) for window in clients if window.internal_id not in exempt_ids)
    assert surface.contains(*cursor) and not popup.contains(*cursor) and krema.preview_visible()
    _record(krema, "transparent-before-click", native_cursor=cursor, native_surface=surface._asdict(), popup=popup._asdict(), clients=[asdict(window) for window in clients], active=asdict(before), expected_pid=underlying.pid, expected_internal_id=underlying.internal_id, preview_internal_id=preview_ids[0])
    inp.click(*cursor)
    active = wait_until(
        lambda: (window if (window := kwin.active_window()) is not None and window.internal_id == underlying.internal_id else None),
        message="transparent part of preview surface passes real input to underlying client",
    )
    assert active.pid == underlying.pid
    _record(krema, "transparent-activation", point=point, native_surface=surface._asdict(), popup=popup._asdict(), active=asdict(active))
    wait_until(lambda: not krema.preview_visible(), message="preview closes after pointer leaves visible content")


@pytest.mark.no_krema_autostart
@pytest.mark.parametrize("edge", EDGES)
def test_prev011_grouped_preview_surface_tracks_two_three_and_single_layouts(
    krema: Krema, apps: TestWindows, edge: int
) -> None:
    krema.write_config(
        {
            "PinnedLaunchers": [],
            "Edge": edge,
            "VisibilityMode": config.ALWAYS_VISIBLE,
            "MaxZoomFactor": 1.0,
            "PreviewEnabled": True,
            "PreviewHoverDelay": 500,
            "PreviewHideDelay": 1500,
        }
    )
    krema.start()
    # Preview thumbnail dimensions are untouched: exercise their defaults.
    windows = [apps.open(title, app_id=env.TEST_APP_ID) for title in TITLES[:2]]
    underlying = apps.open("Underlying client", app_id=env.TEST_APP2_ID)
    kwin.evaluate(
        f"const w = workspace.windowList().find(w => w.pid === {underlying.pid} && w.normalWindow);"
        "w.setMaximize(true, true); report(true);"
    )
    krema.wait_for_item(env.TEST_APP_NAME)
    two_surface, _, _ = _open(krema, windows, "two-initial")
    _select_last(krema, windows, underlying, "two-last")
    _open(krema, windows, "two-before-growth")

    # Grow while open: size must follow the content callback, not only doShow.
    windows.append(apps.open(TITLES[2], app_id=env.TEST_APP_ID))
    three_surface, _, _ = _assert_popup_inside(krema, list(TITLES), "three-grown")
    if edge in (config.EDGE_LEFT, config.EDGE_RIGHT):
        assert three_surface.width > two_surface.width
    _select_last(krema, windows, underlying, "three-last")

    # Reuse the popup through group->single and regrow without restarting Krema.
    _open(krema, windows, "three-before-shrink")
    vertical = edge in (config.EDGE_LEFT, config.EDGE_RIGHT)
    apps.close(windows.pop())
    shrunk_two, _, _ = _assert_popup_inside(
        krema, list(TITLES[:2]), "two-shrunk", shrinking_from=three_surface.width if vertical else None
    )
    apps.close(windows.pop())
    single_surface, _, _ = _assert_popup_inside(
        krema, [TITLES[0]], "single-shrunk", shrinking_from=shrunk_two.width if vertical else None
    )
    if edge in (config.EDGE_LEFT, config.EDGE_RIGHT):
        assert single_surface.width < shrunk_two.width < three_surface.width
    windows.extend(apps.open(title, app_id=env.TEST_APP_ID) for title in TITLES[1:])
    regrown_surface, _, _ = _assert_popup_inside(krema, list(TITLES), "three-regrown")
    assert regrown_surface.width == three_surface.width
    selected = _select_last(krema, windows, underlying, "three-regrown-last")
    _transparent_passthrough(krema, windows, underlying, selected)
