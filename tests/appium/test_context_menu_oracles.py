# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Isolated regression checks for CTX-009's post-New Instance oracle."""

from types import SimpleNamespace

import pytest
from PIL import Image

import test_04_context_menu as ctx


@pytest.mark.parametrize("still_zoomed", [False, True], ids=["restored", "zoom-regression"])
def test_new_instance_tracks_group_and_cleans_up(monkeypatch, still_zoomed):
    rest = {ctx.HOLD_TARGET: ctx.Rect(100, 100, 48, 56), "Other": ctx.Rect(160, 100, 48, 56)}
    launched = False
    cleaned = []
    moved_away = []

    def windows(app_id=ctx.APP):
        # The unrelated default app has no windows. Using it must not pass.
        assert app_id == ctx.HOLD_TARGET_APP
        return [SimpleNamespace(pid=1)] + ([SimpleNamespace(pid=2)] if launched else [])

    def click():
        nonlocal launched
        launched = True

    def wait(predicate, **kwargs):
        value = predicate()
        assert value, kwargs.get("message")
        return value

    def item(name, rect):
        return SimpleNamespace(get_attribute=lambda attribute: name, rect=dict(zip(("x", "y", "width", "height"), rect)))

    def items():
        target = ctx.Rect(90, 70, 77, 90) if still_zoomed else rest[ctx.HOLD_TARGET]
        return [item(ctx.HOLD_TARGET_LAUNCHER, target), item("Other", rest["Other"])]

    def grouped(name):
        assert launched and name == ctx.HOLD_TARGET_LAUNCHER

    dock = SimpleNamespace(
        items=items,
        wait_for_item=grouped,
        preview_visible=lambda: False,
        move_away=lambda **kwargs: moved_away.append(True),
    )
    monkeypatch.setattr(ctx, "app_windows", windows)
    monkeypatch.setattr(ctx, "hover_zoomed_with_preview", lambda *args: (rest, 48, ctx.Rect(100, 70, 77, 90), ctx.Rect(0, 500, 1024, 268)))
    monkeypatch.setattr(ctx, "right_click_in_place", lambda *args: SimpleNamespace(client_geometry=(0, 0, 100, 100)))
    monkeypatch.setattr(ctx, "wait_menu_closed", lambda *args: None)
    monkeypatch.setattr(ctx, "wait_until", wait)
    monkeypatch.setattr(ctx, "wait_stable", lambda getter, **kwargs: getter())
    monkeypatch.setattr(ctx, "settled_capture", lambda *args: Image.new("RGB", (100, 100), "black"))
    monkeypatch.setattr(ctx, "capture", lambda *args: Image.new("RGB", (100, 100), "white"))
    monkeypatch.setattr(ctx, "description", lambda *args: "Pinned, 2 windows")
    monkeypatch.setattr(ctx, "kill_pid", cleaned.append)
    monkeypatch.setattr(ctx, "painted_rect", lambda rect, baseline: rect)
    monkeypatch.setattr(ctx.kwin, "cursor_pos", lambda: (0, 0))
    monkeypatch.setattr(ctx.inp, "key", lambda *args: None)
    monkeypatch.setattr(ctx.inp, "move_path", lambda *args: None)
    monkeypatch.setattr(ctx.inp, "click", click)

    if still_zoomed:
        with pytest.raises(AssertionError):
            ctx.test_ctx009_zoom_releases_when_menu_closes_away_from_the_dock(dock, None)
    else:
        ctx.test_ctx009_zoom_releases_when_menu_closes_away_from_the_dock(dock, None)
    assert launched
    assert cleaned == [2], "only the newly launched process should be cleaned up, even on failure"
    assert moved_away == [True]
