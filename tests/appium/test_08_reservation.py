# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""VIS-009: actual KWin work areas and already-maximized consumer frames.

No screenshot/DRM prerequisite. Observations pair KWin's MaximizeArea and
FullScreenArea with real fixture frameGeometry and unzoomed AT-SPI dock item
bounds, rather than echoing a Krema exclusive-zone property or pixel constant.
"""

from __future__ import annotations

import json

import pytest

from krema_e2e import config, env, kwin
from krema_e2e import input as inp
from krema_e2e import preview as pv
from krema_e2e.krema import Krema, Rect, has_state
from krema_e2e.waits import WaitTimeout, wait_stable, wait_until
from krema_e2e.windows import TestWindow, TestWindows
from test_06_settings import (
    ICON_SIZE_SLIDER,
    RESERVE_SWITCH,
    SETTINGS,
    choose,
    click_el,
    close_settings,
    config_value,
    current_choice,
    focus_slider,
    open_page,
    pick,
    scroll_into_view,
    slider_value,
)
from test_07_visibility import maximize

EDGES = (
    ("top", config.EDGE_TOP),
    ("bottom", config.EDGE_BOTTOM),
    ("left", config.EDGE_LEFT),
    ("right", config.EDGE_RIGHT),
)


def _edge_cases(reserved: bool):
    return [
        pytest.param(
            edge,
            marks=pytest.mark.kremarc(
                {
                    "PinnedLaunchers": [],
                    "ReserveScreenSpace": reserved,
                    "Edge": edge,
                    "Floating": False,
                    "ShadowEnabled": False,
                }
            ),
            id=name,
        )
        for name, edge in EDGES
    ]


def _park() -> None:
    inp.move(env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)


def _observe(krema: Krema, win: TestWindow, with_icon: bool) -> dict:
    observation = kwin.evaluate(
        f"const w = workspace.windowList().find(w => w.pid === {win.pid} && w.normalWindow && !w.skipTaskbar);"
        "if (!w) throw new Error('reservation fixture window not found');"
        "function rect(r) { return [Math.round(r.x), Math.round(r.y), Math.round(r.width), Math.round(r.height)]; }"
        "report({window: String(w.internalId), frame: rect(w.frameGeometry),"
        " workarea: rect(workspace.clientArea(KWin.MaximizeArea, w)),"
        " screen: rect(workspace.clientArea(KWin.FullScreenArea, w)),"
        " output: w.output.name});"
    )
    if with_icon:
        icon = krema.screen_rect(krema.wait_for_item(win.title))
        observation["icon"] = list(icon)
        observation["icon_center"] = list(icon.center)
        surface = krema.surface_rect("dock")
        observation["surface"] = list(surface) if surface is not None else None
    return observation


def _matches(observation: dict, edge: int, reserved: bool) -> bool:
    frame = Rect(*observation["frame"])
    work = Rect(*observation["workarea"])
    screen = Rect(*observation["screen"])
    if frame != work:
        return False
    if not reserved:
        return work == screen
    # Exactly one screen edge is removed from the consumer work area. Its
    # amount is measured by KWin, never computed from Krema's source defaults.
    if edge == config.EDGE_TOP:
        return work.x == screen.x and work.width == screen.width and work.y > screen.y and work.y + work.height == screen.y + screen.height
    if edge == config.EDGE_BOTTOM:
        return work.x == screen.x and work.width == screen.width and work.y == screen.y and work.y + work.height < screen.y + screen.height
    if edge == config.EDGE_LEFT:
        return work.y == screen.y and work.height == screen.height and work.x > screen.x and work.x + work.width == screen.x + screen.width
    return work.y == screen.y and work.height == screen.height and work.x == screen.x and work.x + work.width < screen.x + screen.width


def _record(krema: Krema, stage: str, observation: dict) -> None:
    with env.artifact_path(f"{krema.name}/reservation-geometry.jsonl").open("a", encoding="utf-8") as artifact:
        artifact.write(json.dumps({"stage": stage, **observation}) + "\n")


def _geometry(krema: Krema, win: TestWindow, edge: int, reserved: bool, stage: str) -> dict:
    last = {}

    def observe():
        nonlocal last
        last = _observe(krema, win, with_icon=reserved)
        return last

    def ready(observation):
        if not _matches(observation, edge, reserved):
            return False
        if not reserved:
            return True
        icon_data = observation.get("icon")
        surface_data = observation.get("surface")
        if icon_data is None or surface_data is None:
            return False
        frame, icon = Rect(*observation["frame"]), Rect(*icon_data)
        surface = Rect(*surface_data)
        inside = (
            surface.x <= icon.x and icon.x + icon.width <= surface.x + surface.width
            and surface.y <= icon.y and icon.y + icon.height <= surface.y + surface.height
        )
        clear = {
            config.EDGE_TOP: frame.y >= icon.y + icon.height,
            config.EDGE_BOTTOM: frame.y + frame.height <= icon.y,
            config.EDGE_LEFT: frame.x >= icon.x + icon.width,
            config.EDGE_RIGHT: frame.x + frame.width <= icon.x,
        }[edge]
        return inside and clear

    try:
        # KWin's consumer frame, work area, surface and the slower AT-SPI item
        # query must agree in every stable sample. A second unconstrained
        # observation can otherwise land in the panel-probe handoff.
        observation = wait_stable(observe, duration=0.3, timeout=8, accept=ready)
    except WaitTimeout:
        _record(krema, stage + "-failed", last)
        raise
    assert _matches(observation, edge, reserved), f"{stage}: geometry changed before settling: {observation}"
    # Only ON needs an item comparison. In particular the OFF baseline fails
    # on actual consumer geometry, not because old code lacks the new UI row.
    if reserved:
        _record(krema, stage, observation)
        assert _matches(observation, edge, True), observation
        frame, icon = Rect(*observation["frame"]), Rect(*observation["icon"])
        if edge == config.EDGE_TOP:
            assert frame.y >= icon.y + icon.height, observation
        elif edge == config.EDGE_BOTTOM:
            assert frame.y + frame.height <= icon.y, observation
        elif edge == config.EDGE_LEFT:
            assert frame.x >= icon.x + icon.width, observation
        else:
            assert frame.x + frame.width <= icon.x, observation
    else:
        _record(krema, stage, observation)
    return observation


def _behavior(krema: Krema, name: str = "Alpha", edge: int = config.EDGE_BOTTOM) -> None:
    krema.wait_for_item(name)
    if edge != config.EDGE_BOTTOM:
        assert krema.item_names() == [name], "the native Settings frontdoor must target the sole fixture task"
    settings = krema.open_settings()
    # Park the real pointer beyond the selected edge to cancel keyboard/hover
    # state and close any preview without leaving it over the dock surface.
    park = {
        config.EDGE_TOP: (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT - 1),
        config.EDGE_BOTTOM: (env.SCREEN_WIDTH // 2, 0),
        config.EDGE_LEFT: (env.SCREEN_WIDTH - 1, env.SCREEN_HEIGHT // 2),
        config.EDGE_RIGHT: (0, env.SCREEN_HEIGHT // 2),
    }[edge]
    inp.move(*park)
    wait_until(lambda: kwin.cursor_pos() == park, message="pointer parked outside dock/preview")
    wait_until(lambda: not krema.preview_visible(), timeout=8, message="preview closed after moving away")
    if edge != config.EDGE_BOTTOM:
        _wait_shown_at_edge(krema, name)
        surface, rest = wait_stable(
            lambda: (krema.surface_rect("dock"), krema.screen_rect(krema.wait_for_item(name)))
        )
        assert surface is not None and surface.contains(*rest.center)
        _record(krema, "edge-settings-frontdoor", {
            "edge": edge, "input": "keyboard", "surface": list(surface),
            "rest_icon": list(rest), "settings": list(settings.client_geometry),
        })
    open_page(krema, "Behavior")


def _wait_shown_at_edge(krema: Krema, name: str) -> None:
    def shown():
        item = krema.wait_for_item(name)
        if not has_state(item, "showing"):
            return None
        icon = krema.screen_rect(item)
        surface = krema.surface_rect("dock")
        if surface is None:
            return None
        inside = (
            surface.x <= icon.x and icon.x + icon.width <= surface.x + surface.width
            and surface.y <= icon.y and icon.y + icon.height <= surface.y + surface.height
        )
        return (icon, surface) if inside else None

    wait_until(shown, timeout=8, message="shown dock item inside its actual KWin surface")


def _wait_hidden_at_edge(krema: Krema, name: str, edge: int, why: str) -> None:
    def hidden():
        item = krema.wait_for_item(name)
        if has_state(item, "showing"):
            return None
        icon = krema.screen_rect(item)
        surface = krema.surface_rect("dock")
        if surface is None:
            return None
        outside = {
            config.EDGE_TOP: icon.y + icon.height <= surface.y,
            config.EDGE_BOTTOM: icon.y >= surface.y + surface.height,
            config.EDGE_LEFT: icon.x + icon.width <= surface.x,
            config.EDGE_RIGHT: icon.x >= surface.x + surface.width,
        }[edge]
        return (icon, surface) if outside else None

    icon, surface = wait_until(hidden, timeout=8, message=f"dock hidden beyond selected edge {why}")
    _record(krema, f"hidden-{why}", {"edge": edge, "icon": list(icon), "surface": list(surface)})


def _reserve(krema: Krema, reserved: bool) -> None:
    switch = scroll_into_view(krema, RESERVE_SWITCH)
    if has_state(switch, "checked") != reserved:
        click_el(krema, switch)
    wait_until(lambda: has_state(krema.wait_for(RESERVE_SWITCH), "checked") == reserved, message=f"reservation switch={reserved}")
    wait_until(
        lambda: config.as_bool(config_value(krema, "ReserveScreenSpace") or "true") == reserved,
        message=f"reservation autosaved={reserved}",
    )
    _park()


@pytest.mark.parametrize("edge", _edge_cases(False))
def test_vis009_reservation_off_maximized_window_uses_full_output(krema: Krema, apps: TestWindows, edge: int) -> None:
    """Negative control: pre-feature AlwaysVisible reserves despite OFF."""
    win = apps.open("Alpha", width=400, height=300)
    krema.wait_for_item("Alpha")
    _park()
    maximize(win)
    _geometry(krema, win, edge, False, "startup-off")


@pytest.mark.parametrize("edge", _edge_cases(True))
def test_vis009_already_maximized_window_reflows_on_live_reservation_toggle(
    krema: Krema, apps: TestWindows, edge: int
) -> None:
    win = apps.open("Alpha", width=400, height=300)
    krema.wait_for_item("Alpha")
    _park()
    maximize(win)  # The only maximize request in the entire test.
    initial = _geometry(krema, win, edge, True, "startup-on")
    pid = krema.pid
    _behavior(krema, edge=edge)
    _reserve(krema, False)
    off = _geometry(krema, win, edge, False, "live-off-settings-open")
    assert off["window"] == initial["window"] and off["frame"] != initial["frame"]
    _reserve(krema, True)
    on = _geometry(krema, win, edge, True, "live-on-settings-open")
    assert on["window"] == initial["window"] and on["workarea"] == initial["workarea"]
    assert krema.pid == pid, "live reservation must not restart the dock"
    close_settings(krema)
    _park()
    _geometry(krema, win, edge, True, "live-on-settings-closed")


@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.ALWAYS_VISIBLE, "ReserveScreenSpace": True, "Floating": False})
def test_vis009_reservation_off_and_on_persist_and_reflow_existing_window_after_restart(
    krema: Krema, apps: TestWindows
) -> None:
    win = apps.open("Alpha", width=400, height=300)
    krema.wait_for_item("Alpha")
    _park()
    maximize(win)  # Remains maximized across both dock restarts.
    initial = _geometry(krema, win, config.EDGE_BOTTOM, True, "before-restarts")
    for reserved in (False, True):
        _behavior(krema)
        _reserve(krema, reserved)
        close_settings(krema)
        _park()
        _geometry(krema, win, config.EDGE_BOTTOM, reserved, f"saved-{reserved}")
        pid = krema.pid
        krema.restart()
        assert krema.pid != pid
        krema.wait_for_item("Alpha")
        _park()
        restored = _geometry(krema, win, config.EDGE_BOTTOM, reserved, f"restarted-{reserved}")
        assert restored["window"] == initial["window"], "consumer window must survive dock restart unchanged"
        _behavior(krema)
        assert has_state(scroll_into_view(krema, RESERVE_SWITCH), "checked") == reserved
        assert config.as_bool(config_value(krema, "ReserveScreenSpace") or "true") == reserved
        close_settings(krema)
        _park()


@pytest.mark.parametrize("edge", _edge_cases(True))
def test_vis009_live_icon_size_and_floating_update_maximized_consumer(krema: Krema, apps: TestWindows, edge: int) -> None:
    win = apps.open("Alpha", width=400, height=300)
    krema.wait_for_item("Alpha")
    _park()
    maximize(win)
    initial = _geometry(krema, win, edge, True, "initial-size-flat")
    pid = krema.pid
    _behavior(krema, edge=edge)
    open_page(krema, "Icons")
    slider = focus_slider(krema, ICON_SIZE_SLIDER, 24, 96)
    before_size = slider_value(slider)
    inp.key("right")
    wait_until(lambda: slider_value(slider) > before_size, message="icon size increased through native control")
    _park()
    resized = _geometry(krema, win, edge, True, "larger-icons")
    assert resized["workarea"][2] * resized["workarea"][3] < initial["workarea"][2] * initial["workarea"][3], (initial, resized)
    assert resized["icon"][2] > initial["icon"][2], (initial, resized)
    open_page(krema, "Layout & Position")
    floating_xpath = f"{SETTINGS}//check_box[@name='Floating']"
    for floating in (True, False):
        switch = scroll_into_view(krema, floating_xpath)
        if has_state(switch, "checked") != floating:
            click_el(krema, switch)
        wait_until(lambda: has_state(krema.wait_for(floating_xpath), "checked") == floating, message=f"floating={floating}")
        _park()
        changed = _geometry(krema, win, edge, True, f"floating-{floating}")
        if floating:
            assert changed["workarea"][2] * changed["workarea"][3] < resized["workarea"][2] * resized["workarea"][3], (resized, changed)
        else:
            assert changed["workarea"] == resized["workarea"], (resized, changed)
        assert changed["window"] == initial["window"]
    open_page(krema, "Behavior")
    _reserve(krema, False)
    _geometry(krema, win, edge, False, "resized-off")
    assert krema.pid == pid, "size, floating and reservation changes must all apply live"


FLOATING_SWITCH = f"{SETTINGS}//check_box[@name='Floating']"
SHADOW_SWITCH = f"{SETTINGS}//check_box[@name='Enable shadow']"
FLOATING_MARGIN_SLIDER = f"{SETTINGS}//slider[@name='Floating margin']"
FLOATING_MARGIN_MAX = 128
#: FormComboBoxDelegate row choosing what the floating gap is measured from
#: (kcfg FloatingMarginReference: 0 Screen edge, 1 Panels).
REFERENCE_ROW = "Measure gap from"
REFERENCE_ROW_XPATH = f"{SETTINGS}//list_item[@name='{REFERENCE_ROW}']"
SCREEN_EDGE_REFERENCE = "Screen edge"
PANELS_REFERENCE = "Panels"


def _floating_margin_cases():
    # No FloatingMargin key: the slider must start from the 8 px default.
    return [
        pytest.param(
            edge,
            marks=pytest.mark.kremarc(
                {
                    "PinnedLaunchers": [],
                    "ReserveScreenSpace": True,
                    "Edge": edge,
                    "Floating": True,
                    "ShadowEnabled": False,
                }
            ),
            id=name,
        )
        for name, edge in EDGES
    ]


def _reserved_depth(observation: dict, edge: int) -> int:
    """Thickness KWin removed from the consumer work area at the dock edge."""
    work, screen = Rect(*observation["workarea"]), Rect(*observation["screen"])
    if edge in (config.EDGE_TOP, config.EDGE_BOTTOM):
        return screen.height - work.height
    return screen.width - work.width


def _visible_gap(observation: dict, edge: int) -> int:
    """The screen-to-item gap, a stable proxy for the visible panel rest edge."""
    icon, screen = Rect(*observation["icon"]), Rect(*observation["screen"])
    if edge == config.EDGE_TOP:
        return icon.y - screen.y
    if edge == config.EDGE_BOTTOM:
        return screen.y + screen.height - (icon.y + icon.height)
    if edge == config.EDGE_LEFT:
        return icon.x - screen.x
    return screen.x + screen.width - (icon.x + icon.width)


def _surface_depth(observation: dict, edge: int) -> int:
    """Surface extent inward from the selected screen edge."""
    surface, screen = Rect(*observation["surface"]), Rect(*observation["screen"])
    if edge == config.EDGE_TOP:
        return surface.y + surface.height - screen.y
    if edge == config.EDGE_BOTTOM:
        return screen.y + screen.height - surface.y
    if edge == config.EDGE_LEFT:
        return surface.x + surface.width - screen.x
    return screen.x + screen.width - surface.x


def _set_floating(krema: Krema, floating: bool) -> None:
    switch = scroll_into_view(krema, FLOATING_SWITCH)
    if has_state(switch, "checked") != floating:
        click_el(krema, switch)
    wait_until(lambda: has_state(krema.wait_for(FLOATING_SWITCH), "checked") == floating, message=f"floating={floating}")
    _park()


def _set_shadow(krema: Krema, enabled: bool) -> None:
    open_page(krema, "Shadow")
    switch = scroll_into_view(krema, SHADOW_SWITCH)
    if has_state(switch, "checked") != enabled:
        click_el(krema, switch)
    wait_until(lambda: has_state(krema.wait_for(SHADOW_SWITCH), "checked") == enabled, message=f"shadow={enabled}")
    wait_until(
        lambda: config.as_bool(config_value(krema, "ShadowEnabled") or "false") == enabled,
        message=f"shadow autosaved={enabled}",
    )
    _park()


@pytest.mark.parametrize("edge", _floating_margin_cases())
def test_vis009_live_floating_margin_update_maximized_consumer(krema: Krema, apps: TestWindows, edge: int) -> None:
    """The Floating margin slider moves an already-maximized consumer by
    exactly the margin delta, live, only while Floating is on, and persists."""
    win = apps.open("Alpha", width=400, height=300)
    krema.wait_for_item("Alpha")
    _park()
    maximize(win)
    default = _geometry(krema, win, edge, True, "margin-default")
    pid = krema.pid
    _behavior(krema, edge=edge)
    open_page(krema, "Layout & Position")
    slider = focus_slider(krema, FLOATING_MARGIN_SLIDER, 1, FLOATING_MARGIN_MAX)
    assert slider_value(slider) == 8, "a fresh configuration uses the 8 px default margin"
    assert has_state(slider, "enabled"), "the margin is adjustable while the dock floats"
    inp.key("pageup")  # ten 1 px steps, as a user would press it
    wait_until(lambda: slider_value(krema.wait_for(FLOATING_MARGIN_SLIDER)) == 18, message="floating margin raised to 18 px")
    wait_until(lambda: config_value(krema, "FloatingMargin") == "18", message="floating margin autosaved")
    _park()
    wider = _geometry(krema, win, edge, True, "margin-18")
    assert _reserved_depth(wider, edge) - _reserved_depth(default, edge) == 10, (default, wider)
    assert _visible_gap(wider, edge) - _visible_gap(default, edge) == 10, (default, wider)
    assert wider["window"] == default["window"]

    # Floating off removes the gap entirely, whatever the saved margin.
    _set_floating(krema, False)
    wait_until(
        lambda: not has_state(krema.wait_for(FLOATING_MARGIN_SLIDER), "enabled"),
        message="floating margin disabled while the dock is not floating",
    )
    flat = _geometry(krema, win, edge, True, "margin-18-floating-off")
    assert _reserved_depth(default, edge) - _reserved_depth(flat, edge) == 8, (default, flat)
    assert _visible_gap(default, edge) - _visible_gap(flat, edge) == 8, (default, flat)
    assert config_value(krema, "FloatingMargin") == "18", "turning Floating off keeps the saved margin"

    _set_floating(krema, True)
    restored = _geometry(krema, win, edge, True, "margin-18-floating-on")
    assert _reserved_depth(restored, edge) == _reserved_depth(wider, edge), (wider, restored)
    assert _visible_gap(restored, edge) == _visible_gap(wider, edge), (wider, restored)
    assert restored["window"] == default["window"]

    # Unit/QML tests cover inward containment; Appium observes the
    # user-visible no-dead-band behavior.
    # Shadow containment may enlarge the KWin surface, but must not move the
    # visible panel or its maximized consumer away from the requested margin.
    _set_shadow(krema, True)
    shadowed = _geometry(krema, win, edge, True, "margin-18-shadow-on")
    assert _visible_gap(shadowed, edge) == _visible_gap(wider, edge), (wider, shadowed)
    assert shadowed["frame"] == wider["frame"], (wider, shadowed)
    assert shadowed["workarea"] == wider["workarea"], (wider, shadowed)
    assert _reserved_depth(shadowed, edge) == _reserved_depth(wider, edge), (wider, shadowed)
    assert shadowed["window"] == wider["window"] == default["window"], (wider, shadowed)
    assert _surface_depth(shadowed, edge) >= _surface_depth(wider, edge), (wider, shadowed)

    # Floating off removes the visible gap even while shadow is on.
    open_page(krema, "Layout & Position")
    _set_floating(krema, False)
    shadowed_flat = _geometry(krema, win, edge, True, "margin-18-shadow-floating-off")
    assert _visible_gap(flat, edge) == 0, flat
    assert _visible_gap(shadowed_flat, edge) == _visible_gap(flat, edge), (flat, shadowed_flat)
    assert shadowed_flat["frame"] == flat["frame"], (flat, shadowed_flat)
    assert shadowed_flat["workarea"] == flat["workarea"], (flat, shadowed_flat)
    assert _reserved_depth(shadowed_flat, edge) == _reserved_depth(flat, edge), (flat, shadowed_flat)
    assert shadowed_flat["window"] == flat["window"] == default["window"], (flat, shadowed_flat)

    _set_floating(krema, True)
    restored_shadow = _geometry(krema, win, edge, True, "margin-18-shadow-restored")
    assert _visible_gap(restored_shadow, edge) == _visible_gap(shadowed, edge), (shadowed, restored_shadow)
    assert restored_shadow["frame"] == shadowed["frame"], (shadowed, restored_shadow)
    assert restored_shadow["workarea"] == shadowed["workarea"], (shadowed, restored_shadow)
    assert _reserved_depth(restored_shadow, edge) == _reserved_depth(shadowed, edge), (shadowed, restored_shadow)
    assert restored_shadow["window"] == shadowed["window"], (shadowed, restored_shadow)
    _set_shadow(krema, False)

    close_settings(krema)
    _park()
    krema.restart()
    assert krema.pid != pid
    krema.wait_for_item("Alpha")
    _park()
    restarted = _geometry(krema, win, edge, True, "margin-18-restarted")
    assert _reserved_depth(restarted, edge) == _reserved_depth(wider, edge), (wider, restarted)
    assert restarted["window"] == default["window"], "consumer window must survive dock restart unchanged"


@pytest.mark.kremarc(
    {
        "PinnedLaunchers": [],
        "VisibilityMode": config.ALWAYS_VISIBLE,
        "ReserveScreenSpace": True,
        "Edge": config.EDGE_BOTTOM,
        "Floating": True,
        "ShadowEnabled": False,
    }
)
def test_set021_floating_margin_reference_choice_autosaves_live_and_persists(krema: Krema, apps: TestWindows) -> None:
    """The "Measure gap from" row is an accessible combo row that defaults to
    Panels, autosaves live without a restart, stays usable with Floating off,
    and persists; the Floating margin reaches its 128 px maximum. The session
    has no Plasma panel, so Panels must place the dock exactly like Screen edge."""
    edge = config.EDGE_BOTTOM
    win = apps.open("Alpha", width=400, height=300)
    krema.wait_for_item("Alpha")
    _park()
    maximize(win)
    panels = _geometry(krema, win, edge, True, "reference-panels")
    pid = krema.pid
    _behavior(krema, edge=edge)
    open_page(krema, "Layout & Position")

    row = scroll_into_view(krema, REFERENCE_ROW_XPATH)
    assert has_state(row, "enabled"), "the gap reference is adjustable"
    assert current_choice(krema, REFERENCE_ROW) == PANELS_REFERENCE, "a fresh configuration measures from panels"
    assert config_value(krema, "FloatingMarginReference") is None

    choose(krema, REFERENCE_ROW, SCREEN_EDGE_REFERENCE)
    wait_until(lambda: config_value(krema, "FloatingMarginReference") == "0", message="gap reference autosaved as Screen edge")
    _park()
    screen_edge = _geometry(krema, win, edge, True, "reference-screen-edge")
    assert _reserved_depth(screen_edge, edge) == _reserved_depth(panels, edge), "with no panel, Panels equals Screen edge"
    assert _visible_gap(screen_edge, edge) == _visible_gap(panels, edge), "reference choice must not change the visible gap without panels"
    assert screen_edge["window"] == panels["window"]

    # Floating off: the dock is flush with the reference, so the reference
    # stays adjustable while the margin slider is disabled.
    _set_floating(krema, False)
    wait_until(
        lambda: not has_state(krema.wait_for(FLOATING_MARGIN_SLIDER), "enabled"),
        message="floating margin disabled while the dock is not floating",
    )
    assert has_state(scroll_into_view(krema, REFERENCE_ROW_XPATH), "enabled"), "the gap reference stays enabled with Floating off"
    flat = _geometry(krema, win, edge, True, "reference-floating-off")
    assert _visible_gap(panels, edge) - _visible_gap(flat, edge) == 8, (panels, flat)
    choose(krema, REFERENCE_ROW, PANELS_REFERENCE)
    wait_until(
        lambda: config_value(krema, "FloatingMarginReference") in ("1", None),
        message="gap reference autosaved back to the Panels default",
    )
    _set_floating(krema, True)
    restored = _geometry(krema, win, edge, True, "reference-floating-on")
    assert _visible_gap(restored, edge) == _visible_gap(panels, edge), (panels, restored)

    focus_slider(krema, FLOATING_MARGIN_SLIDER, 1, FLOATING_MARGIN_MAX)
    inp.key("end")
    wait_until(
        lambda: slider_value(krema.wait_for(FLOATING_MARGIN_SLIDER)) == FLOATING_MARGIN_MAX,
        message="floating margin raised to its maximum",
    )
    wait_until(lambda: config_value(krema, "FloatingMargin") == str(FLOATING_MARGIN_MAX), message="maximum margin autosaved")
    _park()
    widest = _geometry(krema, win, edge, True, "margin-max")
    assert _reserved_depth(widest, edge) - _reserved_depth(panels, edge) == FLOATING_MARGIN_MAX - 8, (panels, widest)
    assert _visible_gap(widest, edge) - _visible_gap(panels, edge) == FLOATING_MARGIN_MAX - 8, (panels, widest)
    assert widest["window"] == panels["window"]

    choose(krema, REFERENCE_ROW, SCREEN_EDGE_REFERENCE)
    wait_until(lambda: config_value(krema, "FloatingMarginReference") == "0", message="gap reference autosaved as Screen edge")
    assert krema.pid == pid, "gap reference and margin changes must apply live"

    close_settings(krema)
    _park()
    krema.restart()
    assert krema.pid != pid
    krema.wait_for_item("Alpha")
    _park()
    restarted = _geometry(krema, win, edge, True, "reference-screen-edge-restarted")
    assert _reserved_depth(restarted, edge) == _reserved_depth(widest, edge), (widest, restarted)
    assert restarted["window"] == panels["window"], "consumer window must survive dock restart unchanged"
    _behavior(krema, edge=edge)
    open_page(krema, "Layout & Position")
    scroll_into_view(krema, REFERENCE_ROW_XPATH)
    assert current_choice(krema, REFERENCE_ROW) == SCREEN_EDGE_REFERENCE, "the gap reference survives restart"
    close_settings(krema)


@pytest.mark.parametrize(
    "edge,reserved",
    [
        pytest.param(
            edge,
            reserved,
            marks=pytest.mark.kremarc(
                {
                    "PinnedLaunchers": [],
                    "VisibilityMode": config.ALWAYS_VISIBLE,
                    "ReserveScreenSpace": reserved,
                    "Edge": edge,
                }
            ),
            id=f"{name}-{str(reserved).lower()}",
        )
        for name, edge in EDGES
        for reserved in (False, True)
    ],
)
def test_vis009_visibility_policy_ignores_and_retains_reservation_preference(
    krema: Krema, apps: TestWindows, edge: int, reserved: bool
) -> None:
    win = apps.open("Alpha", width=400, height=300)
    krema.wait_for_item("Alpha")
    _park()
    maximize(win)
    _geometry(krema, win, edge, reserved, "always-before-modes")
    pid = krema.pid
    for label, mode in (("Auto hide", config.AUTO_HIDE), ("Dodge windows", config.DODGE_WINDOWS)):
        _behavior(krema, edge=edge)
        pick(krema, "Visibility mode", label)
        wait_until(lambda: config_value(krema, "VisibilityMode") == str(mode), message=f"visibility policy {label} saved")
        _geometry(krema, win, edge, False, f"{label}-settings-open")
        close_settings(krema)
        kwin.activate(win.internal_id)
        _park()
        _wait_hidden_at_edge(krema, "Alpha", edge, why=f"{label}-maximized-consumer")
        _geometry(krema, win, edge, False, f"{label}-hidden")
        assert config.as_bool(config_value(krema, "ReserveScreenSpace") or "true") == reserved
    _behavior(krema, edge=edge)
    pick(krema, "Visibility mode", "Always visible")
    assert has_state(scroll_into_view(krema, RESERVE_SWITCH), "checked") == reserved
    close_settings(krema)
    _park()
    _wait_shown_at_edge(krema, "Alpha")
    _geometry(krema, win, edge, reserved, "always-restored")
    assert krema.pid == pid


@pytest.mark.parametrize(
    "edge,reserved",
    [
        pytest.param(
            edge,
            reserved,
            marks=pytest.mark.kremarc(
                {
                    "PinnedLaunchers": [],
                    "VisibilityMode": config.ALWAYS_VISIBLE,
                    "ReserveScreenSpace": reserved,
                    "Edge": edge,
                    "Floating": True,
                    "PreviewEnabled": True,
                }
            ),
            id=f"{name}-{'on' if reserved else 'off'}",
        )
        for name, edge in EDGES
        for reserved in (False, True)
    ],
)
def test_prev_reservation_hover_popup_stays_inward_of_resting_dock_item(
    krema: Krema, apps: TestWindows, edge: int, reserved: bool
) -> None:
    """A real hover popup never uses a reserved origin when reservation is OFF."""
    win = apps.open("Alpha", width=400, height=300)
    krema.wait_for_item("Alpha")
    _park()
    rest = wait_stable(lambda: _observe(krema, win, with_icon=True), duration=0.3)
    icon = Rect(*rest["icon"])
    popup = pv.open_by_hover(krema, "Alpha")
    wait_until(
        lambda: pv.thumb_titles(krema) == ["Alpha"],
        message="real fixture window represented by its preview thumbnail",
    )

    def visible_geometry():
        current = krema.preview_popup()
        if current is None or not has_state(current, "showing") or not has_state(current, "visible"):
            return None
        return pv._popup_geometry(krema, current)

    wait_until(visible_geometry, timeout=8, message="visible hover popup within its real KWin preview surface")
    geometry = wait_stable(visible_geometry, duration=pv.GEOMETRY_SETTLE)
    assert geometry is not None, "hover preview disappeared instead of presenting valid geometry"
    surface, popup_rect = geometry
    assert has_state(popup, "showing") and has_state(popup, "visible")
    observation = {
        **rest,
        "reserved": reserved,
        "edge": edge,
        "preview_surface": list(surface),
        "popup": list(popup_rect),
        "thumbnail_titles": pv.thumb_titles(krema),
        "pixel_oracle": "not exercised; popup geometry does not require DRM or screencast pixels",
    }
    _record(krema, "hover-preview", observation)
    screen = Rect(*rest["screen"])
    assert screen.x <= popup_rect.x and popup_rect.x + popup_rect.width <= screen.x + screen.width, observation
    assert screen.y <= popup_rect.y and popup_rect.y + popup_rect.height <= screen.y + screen.height, observation
    # Both the actual popup and its KWin toplevel must sit on the content
    # side of the unzoomed icon measured before hover. No margin, exclusive
    # zone getter, thumbnail pixels, or Krema panel-size constants are used.
    if edge == config.EDGE_TOP:
        assert surface.y >= icon.y + icon.height and popup_rect.y >= icon.y + icon.height, observation
        assert popup_rect.x <= icon.center[0] <= popup_rect.x + popup_rect.width, observation
    elif edge == config.EDGE_BOTTOM:
        assert surface.y + surface.height <= icon.y and popup_rect.y + popup_rect.height <= icon.y, observation
        assert popup_rect.x <= icon.center[0] <= popup_rect.x + popup_rect.width, observation
    elif edge == config.EDGE_LEFT:
        assert surface.x >= icon.x + icon.width and popup_rect.x >= icon.x + icon.width, observation
        assert popup_rect.y <= icon.center[1] <= popup_rect.y + popup_rect.height, observation
    else:
        assert surface.x + surface.width <= icon.x and popup_rect.x + popup_rect.width <= icon.x, observation
        assert popup_rect.y <= icon.center[1] <= popup_rect.y + popup_rect.height, observation


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({"PinnedLaunchers": [], "VisibilityMode": config.ALWAYS_VISIBLE, "Edge": config.EDGE_BOTTOM})
def test_vis009_fresh_config_default_reserves_screen_space_for_maximized_consumer(
    krema: Krema, apps: TestWindows
) -> None:
    """An absent reservation key must produce the actual reserving behavior."""
    # The normal fixture creates a private fresh XDG home. Delayed autostart
    # lets this consumer case prove the key was absent before Krema read it.
    assert config_value(krema, "ReserveScreenSpace") is None
    env.artifact_path(f"{krema.name}/fresh-default-before-start.kremarc").write_text(
        krema.config_path.read_text(), encoding="utf-8"
    )
    win = apps.open("Alpha", width=400, height=300)
    krema.start()
    krema.wait_for_item("Alpha")
    _park()
    maximize(win)
    _geometry(krema, win, config.EDGE_BOTTOM, True, "fresh-default-maximized")

    _behavior(krema)
    switch = scroll_into_view(krema, RESERVE_SWITCH)
    assert has_state(switch, "showing") and has_state(switch, "checked"), (
        "fresh AlwaysVisible configuration must expose an enabled reservation preference"
    )
    # Capture native semantic-help exposure for manual inspection, rather
    # than asserting incidental wording or claiming that text was painted.
    from xml.etree import ElementTree

    settings_xml = krema.page_source()
    control_name = switch.get_attribute("name")
    native_control = next(
        (
            node.attrib
            for node in ElementTree.fromstring(settings_xml).iter("check_box")
            if node.attrib.get("name") == control_name
        ),
        None,
    )
    env.artifact_path(f"{krema.name}/fresh-default-settings-atspi.xml").write_text(
        settings_xml, encoding="utf-8"
    )
    env.artifact_path(f"{krema.name}/fresh-default-reservation-control.json").write_text(
        json.dumps(
            {
                "name": control_name,
                "description": native_control.get("description") if native_control is not None else None,
                "states": native_control.get("states") if native_control is not None else None,
                "native_control": native_control,
                "description_transport": "native AT-SPI page_source XML; WebDriver description getter unsupported",
                "collector_match_found": native_control is not None,
                "stored_reservation_key": config_value(krema, "ReserveScreenSpace"),
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    for mode in ("Auto hide", "Dodge windows"):
        pick(krema, "Visibility mode", mode)
        assert not any(has_state(row, "showing") for row in krema.find_all(RESERVE_SWITCH)), (
            f"reservation preference must not be available in {mode}"
        )
    pick(krema, "Visibility mode", "Always visible")
    restored = scroll_into_view(krema, RESERVE_SWITCH)
    assert has_state(restored, "showing") and has_state(restored, "checked")
    _park()
    _geometry(krema, win, config.EDGE_BOTTOM, True, "fresh-default-restored")
