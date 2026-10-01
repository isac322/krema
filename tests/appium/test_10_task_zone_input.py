# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""KBD-010, PREV-010 and MOUSE-018: real task-zone input consumers."""

from __future__ import annotations

import json
from dataclasses import asdict

import pyatspi
import pytest

from krema_e2e import config, env, kwin
from krema_e2e import input as inp
from krema_e2e import preview as pv
from krema_e2e.krema import EXTENTS_IGNORE_SCALE, Krema, Rect, context_menu_entries, has_state
from krema_e2e.shortcuts import invoke_shortcut
from krema_e2e.waits import wait_stable, wait_until
from krema_e2e.windows import TestWindow, TestWindows
from test_09_task_zones import ALL_NAMES, KFIND_ID, KWRITE_ID, PINNED_A, PINNED_B, PINNED_IDS, SEPARATOR, UNPINNED_A, UNPINNED_B

pytestmark = pytest.mark.no_krema_autostart

EDGES = [
    pytest.param(config.EDGE_BOTTOM, id="bottom"),
    pytest.param(config.EDGE_TOP, id="top"),
    pytest.param(config.EDGE_LEFT, id="left"),
    pytest.param(config.EDGE_RIGHT, id="right"),
]
ORIENTATIONS = [pytest.param(config.EDGE_BOTTOM, id="horizontal"), pytest.param(config.EDGE_LEFT, id="vertical")]
SEPARATION = [pytest.param(True, id="on"), pytest.param(False, id="off")]
APP_IDS = (*PINNED_IDS, KWRITE_ID, KFIND_ID)
EXTRA_APP_ID = "org.kde.krema.taskzoneextra"
EXTRA_COLD_NAME = "Krema Extra Test Window"
EXTRA_NAME = "Newly launched"
ICON_SIZE = 48
GROUP_TITLES = ("Group 1", "Group 2", "Group 3")
F12 = 0x0100003B


def _record(krema: Krema, stage: str, **values) -> None:
    with env.artifact_path(f"{krema.name}/consumer-proof.jsonl").open("a") as stream:
        stream.write(json.dumps({"stage": stage, **values}, sort_keys=True) + "\n")


def _vertical(edge: int) -> bool:
    return edge in (config.EDGE_LEFT, config.EDGE_RIGHT)


def _extent(rect: Rect, edge: int) -> int:
    return rect.height if _vertical(edge) else rect.width


def _primary_center(rect: Rect, edge: int) -> int:
    return rect.center[1] if _vertical(edge) else rect.center[0]


def _rects(krema: Krema, names: tuple[str, ...]) -> dict[str, Rect]:
    return {name: krema.screen_rect(krema.wait_for_item(name)) for name in names}


def _json_rects(rects: dict[str, Rect]) -> dict[str, dict]:
    return {name: rect._asdict() for name, rect in rects.items()}


def _start(krema: Krema, edge: int, *, separate: bool = True, zoom: float = 1.0, previews: bool = False, pinned: tuple[str, ...] = PINNED_IDS) -> None:
    settings = {
        "PinnedLaunchers": [config.launcher(app_id) for app_id in pinned],
        "SeparateLaunchers": separate,
        "Edge": edge,
        "VisibilityMode": config.ALWAYS_VISIBLE,
        "IconSize": ICON_SIZE,
        "MaxZoomFactor": zoom,
        "ZoomStyle": 0,
        "PreviewEnabled": previews,
        "PreviewHoverDelay": 500,
    }
    if previews:
        # Keep the popup open while its 500 ms hover retarget is pending.
        settings["PreviewHideDelay"] = 1500
    krema.write_config(settings)
    krema.start()
    _record(krema, "configuration", requested=settings, actual=krema.read_config()["General"])


def _expect_order(krema: Krema, names: tuple[str, ...]) -> None:
    wait_until(lambda: krema.item_names() == list(names), message=f"native dock task order {names}")


def _park(krema: Krema) -> tuple[int, int]:
    dock = krema.surface_rect("dock")
    preview = krema.surface_rect("preview")
    popup = pv.screen_rect(krema, krema.preview_popup()) if krema.preview_visible() else None
    assert dock is not None
    occupied = [rect for rect in (dock, preview, popup) if rect is not None]
    candidates = [
        (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2),
        (env.SCREEN_WIDTH - 20, env.SCREEN_HEIGHT // 2),
        (20, env.SCREEN_HEIGHT // 2),
        (env.SCREEN_WIDTH // 2, 20),
        (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT - 20),
        (20, 20),
        (env.SCREEN_WIDTH - 20, 20),
        (20, env.SCREEN_HEIGHT - 20),
        (env.SCREEN_WIDTH - 20, env.SCREEN_HEIGHT - 20),
    ]
    point = next((p for p in candidates if not any(rect.contains(*p) for rect in occupied)), None)
    assert point is not None, f"no neutral pointer position outside surfaces {occupied}"
    inp.move(*point)
    wait_until(lambda: kwin.cursor_pos() == point, message="native pointer parked outside dock/preview")
    wait_until(lambda: not krema.preview_visible(), message="preview hidden before measuring rest")
    _record(krema, "pointer-park", native_cursor=kwin.cursor_pos(), occupied=[rect._asdict() for rect in occupied])
    return point


def _rest(krema: Krema, names: tuple[str, ...], edge: int) -> dict[str, Rect]:
    _park(krema)
    wait_until(lambda: all(_extent(rect, edge) == ICON_SIZE for rect in _rects(krema, names).values()), message="all task primary extents at rest")
    return wait_stable(lambda: _rects(krema, names))


def _divider(krema: Krema, present: bool) -> None:
    if not present:
        wait_until(lambda: krema.find(SEPARATOR) is None, message="accessible divider removed after repartition")
        return
    separator = krema.wait_for(SEPARATOR)
    states = {state: has_state(separator, state) for state in ("focusable", "focused", "selectable", "selected")}
    assert not any(states.values()), states


def _matrix(apps: TestWindows, krema: Krema, order: tuple[str, ...] = ALL_NAMES) -> dict[str, TestWindow]:
    windows = {name: apps.open(name, app_id=app_id, accessible=True) for name, app_id in zip(ALL_NAMES, APP_IDS, strict=True)}
    _expect_order(krema, order)
    return windows


def _active(krema: Krema, window: TestWindow, stage: str) -> None:
    current = wait_until(
        lambda: (w if (w := kwin.active_window()) is not None and w.pid == window.pid and w.internal_id == window.internal_id else None),
        message=f"exact native active window {window.title}, PID {window.pid}",
    )
    assert current.title == window.title
    _record(krema, stage, expected_pid=window.pid, expected_internal_id=window.internal_id, active=asdict(current))


def _focused_names(krema: Krema, names: tuple[str, ...]) -> list[str]:
    focused = []
    for name in names:
        node = krema.item_accessible(name)
        if node is not None:
            node.clear_cache()
            if node.getState().contains(pyatspi.STATE_FOCUSED):
                focused.append(node.name)
    return focused


def _focus(krema: Krema, names: tuple[str, ...], expected: str, stage: str) -> None:
    wait_until(lambda: _focused_names(krema, names) == [expected], message=f"only native task {expected!r} focused")
    _divider(krema, True)
    _record(krema, stage, expected=expected, native_focused=_focused_names(krema, names), accessible_process_pid=krema.pid)


def _client_states(window: TestWindow) -> dict | None:
    app = next((a for a in pyatspi.Registry.getDesktop(0) if a is not None and a.get_process_id() == window.pid), None)
    if app is None:
        return None
    frame = pyatspi.findDescendant(app, lambda n: n.getRoleName() == "frame" and n.name == window.title)
    if frame is None:
        return None
    frame.clear_cache()
    focusable, focused = [], []
    for node in pyatspi.findAllDescendants(frame, lambda n: True):
        node.clear_cache()
        state = node.getState()
        identity = {"name": node.name, "role": node.getRoleName()}
        if state.contains(pyatspi.STATE_FOCUSABLE):
            focusable.append(identity)
        if state.contains(pyatspi.STATE_FOCUSED):
            focused.append(identity)
    return {"pid": app.get_process_id(), "title": frame.name, "active": frame.getState().contains(pyatspi.STATE_ACTIVE), "focusable_children": focusable, "focused_children": focused}


def _client_activation(krema: Krema, expected: TestWindow, windows: list[TestWindow], stage: str) -> None:
    def observed():
        states = [_client_states(window) for window in windows]
        if any(state is None for state in states):
            return None
        target = next(state for state in states if state["pid"] == expected.pid)
        if not target["active"] or any(state["active"] for state in states if state["pid"] != expected.pid):
            return None
        if target["focusable_children"] and not target["focused_children"]:
            return None
        return states

    states = wait_until(observed, message=f"own native client PID {expected.pid} active with focused child")
    assert next(state for state in states if state["pid"] == expected.pid)["title"] == expected.title
    _record(krema, stage, expected_pid=expected.pid, own_native_frames=states)


def _traverse(krema: Krema, apps: TestWindows, windows: dict[str, TestWindow], names: tuple[str, ...], edge: int, stage: str) -> None:
    _expect_order(krema, names)
    _park(krema)
    krema.click_item(names[-1])
    _active(krema, windows[names[-1]], f"{stage}-before-navigation")
    _park(krema)
    invoke_shortcut("focus-dock")
    initial = wait_until(lambda: (f if len(f := _focused_names(krema, names)) == 1 else None), message="one native focused controlled task")[0]
    _focus(krema, names, initial, f"{stage}-initial")
    previous, following = ("Up", "Down") if _vertical(edge) else ("Left", "Right")
    for index in range(names.index(initial) - 1, -1, -1):
        inp.key(previous)
        _focus(krema, names, names[index], f"{stage}-canonicalize")
    _focus(krema, names, names[0], f"{stage}-first")
    visits = [names[0]]
    for name in names[1:]:
        inp.key(following)
        _focus(krema, names, name, f"{stage}-forward")
        visits.append(name)
    for name in reversed(names[:-1]):
        inp.key(previous)
        _focus(krema, names, name, f"{stage}-reverse")
        visits.append(name)
    assert visits == [*names, *reversed(names[:-1])]
    _record(krema, stage, visits=visits, forward=following, backward=previous)
    inp.key("Return")
    target = windows[names[0]]
    _client_activation(krema, target, list(windows.values()), f"{stage}-return")
    before = len(apps.key_presses())
    inp.key("F12")
    wait_until(lambda: (target.title, F12, 0) in apps.key_presses()[before:], message="new F12 delivered to the exact native client")
    delivered = apps.key_presses()[before:]
    assert all(title == target.title for title, key, _ in delivered if key == F12)
    _client_activation(krema, target, list(windows.values()), f"{stage}-probe-active")
    _record(krema, f"{stage}-delivered", expected_pid=target.pid, delivered=delivered)


def _install_extra_launcher(krema: Krema) -> None:
    # Use the fixture's private XDG_DATA_HOME, as the native mouse tests do.
    applications = krema.home / "data" / "applications"
    applications.mkdir(parents=True, exist_ok=True)
    (applications / f"{EXTRA_APP_ID}.desktop").write_text(
        "[Desktop Entry]\n"
        "Type=Application\n"
        f"Name={EXTRA_COLD_NAME}\n"
        f"Exec={env.TEST_WINDOW_BINARY} --app-id {EXTRA_APP_ID}\n"
        "Icon=utilities-terminal\n"
        f"StartupWMClass={EXTRA_APP_ID}\n",
        encoding="utf-8",
    )


@pytest.mark.parametrize("edge", EDGES)
def test_kbd010_task_navigation_survives_native_launch_and_close(krema: Krema, apps: TestWindows, edge: int) -> None:
    _install_extra_launcher(krema)
    _start(krema, edge, pinned=(*PINNED_IDS, EXTRA_APP_ID))
    cold_names = wait_until(
        lambda: (names if len(names := krema.item_names()) == 3 else None),
        message="three actual cold pinned launchers",
    )
    cold_name = cold_names[2]
    # The existing harness documents desktop-id names as unresolved launchers.
    assert cold_name not in (EXTRA_APP_ID, f"{EXTRA_APP_ID}.desktop", config.launcher(EXTRA_APP_ID)), f"extra desktop fixture did not resolve: {cold_names}"
    assert cold_name == EXTRA_COLD_NAME, f"cold launcher name must resolve from the private fixture entry: {cold_names}"
    _record(krema, "cold-pinned-fixture", app_id=EXTRA_APP_ID, native_cold_names=cold_names, extra_cold_name=cold_name)
    initial = (PINNED_A, PINNED_B, cold_name, UNPINNED_A, UNPINNED_B)
    windows = _matrix(apps, krema, initial)
    _traverse(krema, apps, windows, initial, edge, "initial-cold-pinned")
    extra = apps.open(EXTRA_NAME, app_id=EXTRA_APP_ID, accessible=True)
    windows[EXTRA_NAME] = extra
    launched = (PINNED_A, PINNED_B, EXTRA_NAME, UNPINNED_A, UNPINNED_B)
    _traverse(krema, apps, windows, launched, edge, "after-pinned-launch")
    apps.close(windows.pop(UNPINNED_B))
    krema.wait_for_no_item(UNPINNED_B)
    closed = (PINNED_A, PINNED_B, EXTRA_NAME, UNPINNED_A)
    _traverse(krema, apps, windows, closed, edge, "after-unpinned-close")


def _entry(krema: Krema, edge: int, target: Rect) -> list[tuple[int, int]]:
    dock = krema.surface_rect("dock")
    assert dock is not None
    x, y = target.center
    if edge == config.EDGE_LEFT:
        start, near = (env.SCREEN_WIDTH - 20, y - 4), (x, y - 4)
    elif edge == config.EDGE_RIGHT:
        start, near = (20, y - 4), (x, y - 4)
    elif edge == config.EDGE_BOTTOM:
        start, near = (x - 4, 20), (x - 4, y)
    else:
        start, near = (x - 4, env.SCREEN_HEIGHT - 20), (x - 4, y)
    assert not dock.contains(*start)
    preview = krema.surface_rect("preview")
    assert preview is None or not preview.contains(*start)
    assert target.contains(*near)
    return [start, *inp.line(start, near, 5), *inp.line(near, target.center, 2)]


def _physical(krema: Krema, names: tuple[str, ...], name: str, stage: str, path: list[tuple[int, int]]) -> dict[str, Rect]:
    bounds = wait_stable(lambda: _rects(krema, names))
    cursor = kwin.cursor_pos()
    dock = krema.surface_rect("dock")
    preview = krema.surface_rect("preview")
    popup = pv.screen_rect(krema, krema.preview_popup()) if krema.preview_visible() else None
    _record(krema, stage, target=name, native_cursor=cursor, planned_path=path, all_task_bounds=_json_rects(bounds), native_dock=dock._asdict() if dock else None, native_preview=preview._asdict() if preview else None, interactive_popup=popup._asdict() if popup else None)
    assert [item for item, rect in bounds.items() if rect.contains(*cursor)] == [name]
    assert popup is None or not popup.contains(*cursor)
    return bounds


def _vertical_preview_hover(krema: Krema, names: tuple[str, ...], item: str, initial: bool) -> None:
    before = _rest(krema, names, config.EDGE_LEFT) if initial else wait_stable(lambda: _rects(krema, names))
    assert all(rect.height == ICON_SIZE for rect in before.values())
    target = before[item]
    cursor = kwin.cursor_pos()
    if initial:
        path = _entry(krema, config.EDGE_LEFT, target)
        inp.move_path(path, step_ms=40)
    else:
        assert krema.preview_visible(), "retarget must begin with the real popup open"
        assert cursor[0] == target.center[0]
        path = inp.line(cursor, target.center, 5)
        for point in path:
            inp.move_path([point], step_ms=40)
            assert krema.preview_visible(), "popup must stay open throughout primary-axis retarget"
    after = _physical(krema, names, item, "vertical-after-single-action", path)
    assert kwin.cursor_pos() == target.center
    assert after == before
    wait_until(krema.preview_visible, message=f"popup after single measured vertical hover on {item}")
    assert _physical(krema, names, item, "vertical-popup-physical-target", path) == before


def _group_identity(krema: Krema, group: list[TestWindow], stage: str) -> None:
    native = [w for w in kwin.app_windows() if w.pid in {window.pid for window in group}]
    assert {(w.pid, w.internal_id, w.title) for w in native} == {(w.pid, w.internal_id, w.title) for w in group}
    _record(krema, stage, native_windows=[asdict(window) for window in native])


def _preview(krema: Krema, edge: int, names: tuple[str, ...], item: str, header: str, windows: list[TestWindow], stage: str, *, initial: bool = False) -> None:
    if edge == config.EDGE_LEFT:
        _vertical_preview_hover(krema, names, item, initial)
    else:
        pv.open_by_hover(krema, item)
    popup = krema.preview_popup()
    assert popup is not None and has_state(popup, "showing") and has_state(popup, "visible")
    wait_until(lambda: krema.find(pv.HEADER_XPATH).get_attribute("name") == header, message=f"application header {header!r}")
    titles = [window.title for window in windows]
    wait_until(lambda: pv.thumb_titles(krema) == titles, message=f"exact thumbnail titles/order {titles}")
    assert len(krema.thumbnails()) == len(windows)
    _group_identity(krema, windows, stage)
    _record(krema, stage, popup_name=popup.get_attribute("name"), expected_header=header, thumbnail_titles=pv.thumb_titles(krema))


def _last_thumbnail(krema: Krema, group: list[TestWindow], expected_titles: tuple[str, ...], stage: str) -> None:
    expected = group[-1]
    assert pv.thumb_titles(krema) == list(expected_titles)
    surface, popup, thumbnail = wait_stable(
        lambda: (krema.surface_rect("preview"), pv.screen_rect(krema, krema.preview_popup()), pv.screen_rect(krema, krema.wait_for(pv.thumb_xpath(expected.title))))
    )
    point = thumbnail.center
    _record(krema, f"{stage}-target", expected_title=expected.title, expected_pid=expected.pid, native_preview=surface._asdict() if surface else None, popup=popup._asdict(), thumbnail=thumbnail._asdict(), point=point)
    assert surface is not None and surface.contains(*point), f"last thumbnail is outside native input surface: {surface}, {thumbnail}"
    assert popup.contains(*point)
    before = kwin.active_window()
    assert before is not None and before.pid != expected.pid, "last-thumbnail selection must activate a different native client"
    pv.glide_into(krema, point)
    wait_until(lambda: kwin.cursor_pos() == point, message="native pointer on last thumbnail")
    inp.click(*point)
    _active(krema, expected, stage)
    wait_until(lambda: not krema.preview_visible(), message="last-thumbnail activation closes preview")


def _toggle_pin(krema: Krema, name: str, app_id: str, *, pinned: bool, order: tuple[str, ...]) -> None:
    _park(krema)
    before_names = krema.item_names()
    before_launchers = config.as_list(krema.read_config()["General"]["PinnedLaunchers"])
    krema.open_context_menu(name)
    action = "Unpin from Dock" if pinned else "Pin to Dock"
    krema.choose_context_menu_entry(action, context_menu_entries(pinned=pinned, is_window=True))
    launcher = config.launcher(app_id)
    wait_until(lambda: (launcher in config.as_list(krema.read_config()["General"].get("PinnedLaunchers", ""))) is not pinned, message=f"real {action} membership change")
    _expect_order(krema, order)
    _park(krema)
    _record(krema, "native-pin-transition", action=action, task=name, before_names=before_names, before_launcher_urls=before_launchers, after_names=krema.item_names(), after_launcher_urls=config.as_list(krema.read_config()["General"]["PinnedLaunchers"]))


@pytest.mark.parametrize("edge", ORIENTATIONS)
def test_prev010_last_thumbnail_and_other_app_pin_transitions(krema: Krema, apps: TestWindows, edge: int) -> None:
    _start(krema, edge, previews=True)
    # The existing vertical preview surface is 400 px deep. Keep its feature
    # coverage strict by exercising thumbnail index 1 vertically; the separate
    # preview-surface fix owns whole-rect reachability for wider grouped rows.
    group_titles = GROUP_TITLES if edge == config.EDGE_BOTTOM else GROUP_TITLES[:2]
    group = [apps.open(title, app_id=env.TEST_APP_ID, accessible=True) for title in group_titles]
    apps.open(PINNED_B, app_id=env.TEST_APP2_ID, accessible=True)
    unpinned = apps.open(UNPINNED_A, app_id=KWRITE_ID, accessible=True)
    name = env.TEST_APP_NAME
    original = (name, PINNED_B, UNPINNED_A)
    reordered = (PINNED_B, name, UNPINNED_A)
    _expect_order(krema, original)
    _toggle_pin(krema, name, env.TEST_APP_ID, pinned=True, order=reordered)
    _toggle_pin(krema, name, env.TEST_APP_ID, pinned=False, order=reordered)
    assert config.as_list(krema.read_config()["General"]["PinnedLaunchers"]) == [config.launcher(env.TEST_APP2_ID), config.launcher(env.TEST_APP_ID)]
    _divider(krema, True)
    _preview(krema, edge, reordered, name, name, group, "initial-group", initial=True)
    _preview(krema, edge, reordered, UNPINNED_A, "KWrite", [unpinned], "initial-across-division")
    _preview(krema, edge, reordered, name, name, group, "initial-return-group")
    _last_thumbnail(krema, group, group_titles, "last-thumbnail-first-selection")

    shifted = (name, PINNED_B, UNPINNED_A)
    for currently_pinned, label in ((True, "other-app-unpinned"), (False, "other-app-repinned")):
        before_names = krema.item_names()
        _toggle_pin(krema, PINNED_B, env.TEST_APP2_ID, pinned=currently_pinned, order=shifted)
        after_names = krema.item_names()
        assert after_names.index(name) == 0
        if currently_pinned:
            assert before_names.index(name) == 1
            assert after_names.index(name) != before_names.index(name)
        _record(krema, f"{label}-group-slot", before_names=before_names, after_names=after_names, before_slot=before_names.index(name), after_slot=after_names.index(name))
        _group_identity(krema, group, f"{label}-group-preserved")
        _divider(krema, True)
        krema.click_item(UNPINNED_A)
        _active(krema, unpinned, f"{label}-before-last-selection")
        _preview(krema, edge, shifted, name, name, group, f"{label}-group", initial=True)
        _preview(krema, edge, shifted, UNPINNED_A, "KWrite", [unpinned], f"{label}-other-app")
        _preview(krema, edge, shifted, name, name, group, f"{label}-return-group")
        _last_thumbnail(krema, group, group_titles, f"{label}-last-thumbnail-reselection")


def _expanded_point(base: Rect, drawn: Rect, edge: int) -> tuple[int, int]:
    x, y = drawn.center
    if edge == config.EDGE_BOTTOM:
        point = (x, base.y - 4)
    elif edge == config.EDGE_TOP:
        point = (x, base.y + base.height + 4)
    elif edge == config.EDGE_LEFT:
        point = (base.x + base.width + 4, y)
    else:
        point = (base.x - 4, y)
    assert drawn.contains(*point) and not base.contains(*point), f"no expanded-only hit at {point}: baseline={base}, magnified={drawn}"
    return point


def _magnified_click(krema: Krema, names: tuple[str, ...], windows: dict[str, TestWindow], target: str, edge: int, separate: bool) -> None:
    baseline = _rest(krema, names, edge)
    path = _entry(krema, edge, baseline[target])
    inp.move_path(path, step_ms=40)
    wait_until(lambda: _extent(_rects(krema, names)[target], edge) >= ICON_SIZE * 1.5, message=f"{target} actually magnifies near configured 1.6x")
    drawn = wait_stable(lambda: _rects(krema, names))
    index = names.index(target)
    for neighbor in (index - 1, index + 1):
        name = names[neighbor]
        shift = _primary_center(drawn[name], edge) - _primary_center(baseline[name], edge)
        assert shift * (neighbor - index) > 2, f"{name} did not reflow outward: {baseline}, {drawn}"
    point = _expanded_point(baseline[target], drawn[target], edge)
    assert [name for name, rect in drawn.items() if rect.contains(*point)] == [target]
    inp.move_path(inp.line(kwin.cursor_pos(), point, 3), step_ms=40)
    final = wait_stable(lambda: _rects(krema, names))
    cursor = kwin.cursor_pos()
    _record(krema, "magnified-expanded-hit", target=target, edge=edge, separate=separate, baseline=_json_rects(baseline), magnified=_json_rects(final), point=point, native_cursor=cursor)
    assert cursor == point
    assert final[target].contains(*point) and not baseline[target].contains(*point)
    assert [name for name, rect in final.items() if rect.contains(*point)] == [target]
    assert _extent(final[target], edge) >= ICON_SIZE * 1.5
    surface = krema.surface_rect("dock")
    assert surface is not None and surface.contains(*point)
    _divider(krema, separate)
    inp.click(*point)
    _active(krema, windows[target], f"magnified-click-{target}")


@pytest.mark.skipif(EXTENTS_IGNORE_SCALE, reason="expanded native AT-SPI hit rectangles require Qt >= 6.9")
@pytest.mark.parametrize("edge", EDGES)
@pytest.mark.parametrize("separate", SEPARATION)
def test_mouse018_separation_modes_and_magnified_boundary_hits(krema: Krema, apps: TestWindows, edge: int, separate: bool) -> None:
    _start(krema, edge, separate=separate, zoom=1.6)
    windows = _matrix(apps, krema)
    _divider(krema, separate)
    for name in (PINNED_B, UNPINNED_A):
        rest = _rest(krema, ALL_NAMES, edge)
        point = rest[name].center
        inp.click(*point)
        assert kwin.cursor_pos() == point
        _active(krema, windows[name], f"rest-boundary-click-{name}")
    for name in (PINNED_B, UNPINNED_A):
        _magnified_click(krema, ALL_NAMES, windows, name, edge, separate)
