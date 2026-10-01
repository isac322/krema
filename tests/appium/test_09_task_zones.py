# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""TZONE-001..TZONE-007: real pinned/running task-zone behavior.

These scenarios exercise KWin-mapped fixture windows and the dock's AT-SPI
surface. The image-free geometry checks use actual accessible bounds; there is
no screenshot or DRM dependency here.
"""

from __future__ import annotations

import json
from typing import Iterable

import pytest

from krema_e2e import config, env, kwin
from krema_e2e import input as inp
from krema_e2e.krema import Krema, context_menu_entries, has_state
from krema_e2e.preview import Announcements
from krema_e2e.waits import wait_stable, wait_until
from krema_e2e.windows import TestWindows

# Reuse the established settings-page navigation rather than adding a second
# scrolling convention to the Appium suite.
from test_06_settings import close_settings, open_page, scroll_into_view
from test_05_drag import drag
from test_04_context_menu import description


KWRITE_ID, KFIND_ID = "org.kde.kwrite", "org.kde.kfind"
PINNED_A, PINNED_B = "Pinned A", "Pinned B"
UNPINNED_A, UNPINNED_B = "Unpinned A", "Unpinned B"
PINNED_IDS = (env.TEST_APP_ID, env.TEST_APP2_ID)
ALL_NAMES = (PINNED_A, PINNED_B, UNPINNED_A, UNPINNED_B)
SEPARATE_SWITCH = "//frame[@name='Settings']//check_box[@name='Separate pinned and running apps']"
SEPARATOR = "//separator[@name='Pinned and running apps separator']"


@pytest.fixture
def task_zone_names() -> tuple[str, ...]:
    """Stable names exposed by the real fixture window titles."""
    return ALL_NAMES


def _launcher_ids(krema: Krema) -> list[str]:
    return config.as_list(krema.read_config().get("General", {}).get("PinnedLaunchers", ""))


def _configure(krema: Krema, *, edge: int, separate: bool, pinned: Iterable[str] = PINNED_IDS) -> None:
    krema.write_config(
        {
            "PinnedLaunchers": [config.launcher(app_id) for app_id in pinned],
            "SeparateLaunchers": separate,
            "Edge": edge,
            "VisibilityMode": config.ALWAYS_VISIBLE,
            "PreviewHoverDelay": 60000,
        }
    )
    krema.restart()


def _open_matrix(apps: TestWindows, krema: Krema) -> None:
    apps.open("Pinned A", app_id=env.TEST_APP_ID)
    apps.open("Pinned B", app_id=env.TEST_APP2_ID)
    apps.open("Unpinned A", app_id=KWRITE_ID)
    apps.open("Unpinned B", app_id=KFIND_ID)
    for name in ALL_NAMES:
        krema.wait_for_item(name)
    wait_until(lambda: len(krema.item_names()) == len(ALL_NAMES), message="one visible task per fixture app")
    krema.move_away()


def _assert_partition(krema: Krema, expected: Iterable[str] = ALL_NAMES) -> None:
    names = krema.item_names()
    fixture_names = [name for name in names if name in ALL_NAMES]
    assert names[:2] == [PINNED_A, PINNED_B], f"pinned zone order: {names}"
    assert fixture_names == list(expected), f"fixture task order/duplicates: {names}"

def _separator(krema: Krema):
    return krema.find(SEPARATOR)


def _wait_separator(krema: Krema):
    return wait_until(lambda: _separator(krema), message="pinned/running accessible separator")


def _wait_no_separator(krema: Krema) -> None:
    wait_until(lambda: _separator(krema) is None, message="separator to be absent")


def _open_behavior_switch(krema: Krema, *, pinned: bool = True):
    # Use the real button currently exposed by the dock. Running fixture
    # tasks are named by their window titles in the source-built QA image,
    # while launcher-only items may use the desktop Name.
    anchor = wait_until(
        lambda: next((name for name in krema.item_names() if ("Pinned" in description(krema, name)) is pinned), None),
        message="a real dock item matching the Settings context-menu membership",
    )
    krema.open_settings(anchor, entries=context_menu_entries(pinned=pinned, is_window=True))
    open_page(krema, "Behavior")
    return scroll_into_view(krema, SEPARATE_SWITCH)


def _toggle_separation(krema: Krema, expected: bool) -> None:
    switch = _open_behavior_switch(krema)
    assert has_state(switch, "checked") is not expected
    inp.click(*krema.screen_rect(switch, "settings").center)
    wait_until(
        lambda: has_state(krema.wait_for(SEPARATE_SWITCH), "checked") == expected,
        message=f"separation switch to become {expected}",
    )
    wait_until(
        lambda: config.as_bool(krema.read_config().get("General", {}).get("SeparateLaunchers", "false")) == expected,
        message=f"SeparateLaunchers autosaved as {expected}",
    )


@pytest.mark.parametrize(
    "edge",
    [config.EDGE_TOP, config.EDGE_BOTTOM, config.EDGE_LEFT, config.EDGE_RIGHT],
    ids=["top", "bottom", "left", "right"],
)
def test_tzone001_live_toggle_orders_visible_tasks_and_persists(
    krema: Krema, apps: TestWindows, edge: int, task_zone_names: tuple[str, ...]
) -> None:
    """The live switch partitions real tasks and survives full restarts."""
    _configure(krema, edge=edge, separate=False)
    _open_matrix(apps, krema)
    assert krema.item_names() == list(task_zone_names)
    original_membership = _launcher_ids(krema)
    _wait_no_separator(krema)

    # Prove ON-mode migration from a genuinely cross-zone OFF-mode order. The
    # relative order within each resulting zone must remain stable.
    active = kwin.active_window()
    assert active is not None
    _drag_from_rest(krema, UNPINNED_B, PINNED_A, "off-mode-cross-zone-before-toggle")
    off_order = [UNPINNED_B, PINNED_A, PINNED_B, UNPINNED_A]
    wait_until(lambda: krema.item_names() == off_order, message="OFF mode permits cross-zone order before enabling separation")
    assert _launcher_ids(krema) == original_membership
    # Match the established post-drag focus contract before opening a fresh
    # menu. This observes KWin state without activating or repairing a client.
    wait_until(
        lambda: (current := kwin.active_window()) is not None and current.internal_id == active.internal_id,
        message="pre-drag window active again after the drop",
    )
    park = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)
    dock = krema.surface_rect("dock")
    assert dock is not None and not dock.contains(*park)
    inp.move(*park)

    _toggle_separation(krema, True)
    separated_order = [PINNED_A, PINNED_B, UNPINNED_B, UNPINNED_A]
    # Settings is itself an unpinned native task while its window is open.
    wait_until(
        lambda: [name for name in krema.item_names() if name in ALL_NAMES] == separated_order,
        message="ON mode preserves fixture relative order while partitioning zones",
    )
    _assert_partition(krema, separated_order)
    _wait_separator(krema)
    assert _launcher_ids(krema) == original_membership
    close_settings(krema)
    wait_until(lambda: krema.item_names() == separated_order, message="exact separated order once Settings closes")

    krema.restart()
    for name in task_zone_names:
        krema.wait_for_item(name)
    # Only launcher order is persisted. Running-task enumeration may change
    # after a process restart, but ON mode must restore the pinned section.
    wait_until(
        lambda: len(names := krema.item_names()) == 4 and names[:2] == [PINNED_A, PINNED_B] and set(names[2:]) == {UNPINNED_A, UNPINNED_B},
        message="ON restart restores pinned order and exactly one of each running fixture",
    )
    restarted_order = wait_stable(krema.item_names)
    assert len(restarted_order) == 4
    assert restarted_order[:2] == [PINNED_A, PINNED_B]
    assert set(restarted_order[2:]) == {UNPINNED_A, UNPINNED_B}
    _wait_separator(krema)
    assert _launcher_ids(krema) == original_membership
    assert config.as_bool(krema.read_config()["General"]["SeparateLaunchers"])

    _toggle_separation(krema, False)
    close_settings(krema)
    _wait_no_separator(krema)
    wait_until(lambda: krema.item_names() == restarted_order, message="live OFF mode retains the settled pre-toggle order")
    _assert_partition(krema, restarted_order)
    assert _launcher_ids(krema) == original_membership
    assert config.as_bool(krema.read_config().get("General", {}).get("SeparateLaunchers", "false")) is False

    krema.restart()
    for name in task_zone_names:
        krema.wait_for_item(name)
    wait_until(
        lambda: len(names := krema.item_names()) == 4 and set(names) == set(ALL_NAMES),
        message="OFF restart restores exactly one of each fixture without imposing task ranks",
    )
    off_restart_order = wait_stable(krema.item_names)
    assert len(off_restart_order) == 4 and set(off_restart_order) == set(ALL_NAMES)
    switch = _open_behavior_switch(krema)
    wait_until(
        lambda: [name for name in krema.item_names() if name in ALL_NAMES] == off_restart_order,
        message="opening Settings preserves existing OFF-mode fixture order",
    )
    _wait_no_separator(krema)
    assert _launcher_ids(krema) == original_membership
    assert not has_state(switch, "checked"), "OFF separation preference must persist across restart"
    assert not config.as_bool(krema.read_config().get("General", {}).get("SeparateLaunchers", "false"))
    close_settings(krema)
    wait_until(lambda: krema.item_names() == off_restart_order, message="closing Settings preserves exact OFF-mode fixture order")


@pytest.mark.parametrize(
    "edge",
    [config.EDGE_TOP, config.EDGE_BOTTOM, config.EDGE_LEFT, config.EDGE_RIGHT],
    ids=["top", "bottom", "left", "right"],
)
def test_tzone002_separator_accessibility_geometry_and_single_zone_visibility(
    krema: Krema, apps: TestWindows, edge: int
) -> None:
    """The separator is an accessible, orientation-aware overlay, not a task."""
    _configure(krema, edge=edge, separate=True)
    _open_matrix(apps, krema)
    # The stock helper parks at (screenWidth/2, 20), which is still inside a
    # top-edge dock. Establish actual rest bounds before checking its gap.
    park = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)
    dock = krema.surface_rect("dock")
    assert dock is not None and not dock.contains(*park)
    inp.move(*park)
    vertical = edge in (config.EDGE_LEFT, config.EDGE_RIGHT)
    wait_until(
        lambda: all(
            (krema.screen_rect(item).height if vertical else krema.screen_rect(item).width) == 48
            for item in krema.items()
        ),
        message="all dock items to return to their unzoomed primary extent",
    )
    wait_stable(
        lambda: tuple(krema.screen_rect(item) for item in krema.items()),
        duration=0.6,
    )

    separator = _wait_separator(krema)
    assert has_state(separator, "showing")
    assert has_state(separator, "visible")
    rect = krema.screen_rect(separator)
    assert rect.width > 0 and rect.height > 0, f"separator must have positive extents: {rect}"
    before = krema.screen_rect(krema.wait_for_item(PINNED_B))
    after = krema.screen_rect(krema.wait_for_item(UNPINNED_A))
    if edge in (config.EDGE_TOP, config.EDGE_BOTTOM):
        assert rect.height > rect.width
        assert before.x + before.width <= rect.center[0] <= after.x
    else:
        assert rect.width > rect.height
        assert before.y + before.height <= rect.center[1] <= after.y

    # At maximum zoom the two adjacent delegates remain real hit targets; the
    # overlay must not steal pointer input or disappear while either is hovered.
    for name in (PINNED_B, UNPINNED_A):
        krema.hover_item(name)
        wait_until(lambda: krema.screen_rect(krema.wait_for_item(name)).width >= 48, message=f"{name} hover target")
        assert _separator(krema) is not None
        assert kwin_cursor_is_on_item(krema, name)
    krema.move_away()

    # Removing the unpinned zone leaves no boundary; removing every task does
    # the same. Both observations are from AT-SPI, not QML/source inspection.
    for window in list(apps.open_windows):
        if window.app_id in (KWRITE_ID, KFIND_ID):
            apps.close(window)
    wait_until(
        lambda: krema.item(UNPINNED_A) is None and krema.item(UNPINNED_B) is None,
        message="all unpinned tasks to close",
    )
    _wait_no_separator(krema)
    apps.close_all()
    _configure(krema, edge=edge, separate=True, pinned=())
    wait_until(lambda: krema.item_names() == [], message="empty task model to have no dock items")
    _wait_no_separator(krema)


def kwin_cursor_is_on_item(krema: Krema, name: str) -> bool:
    """Check the real pointer hit point after a zoomed hover."""
    from krema_e2e import kwin

    return krema.screen_rect(krema.wait_for_item(name)).contains(*kwin.cursor_pos())


def test_tzone003_grouped_instances_keep_one_pinned_slot_and_close_differently(
    krema: Krema, apps: TestWindows
) -> None:
    """A pinned app stays in its slot; an unpinned task vanishes on close."""
    _configure(krema, edge=config.EDGE_BOTTOM, separate=True, pinned=(env.TEST_APP_ID,))
    wait_until(lambda: krema.item_names() == [env.TEST_APP_NAME], message="cold pinned launcher slot")

    first = apps.open("Pinned first", app_id=env.TEST_APP_ID)
    wait_until(lambda: krema.item_names() == [first.title], message="single pinned window title in its slot")
    second = apps.open("Pinned second", app_id=env.TEST_APP_ID)
    wait_until(
        lambda: krema.item_names() == [env.TEST_APP_NAME]
        and "2 windows" in description(krema, env.TEST_APP_NAME),
        message="two same-app windows grouped as one pinned desktop-name task",
    )
    assert {w.pid for w in apps.open_windows if w.refresh()} == {first.pid, second.pid}

    apps.close(second)
    wait_until(lambda: krema.item_names() == [first.title], message="group collapses to the remaining pinned window title")
    apps.close(first)
    wait_until(lambda: krema.item_names() == [env.TEST_APP_NAME], message="cold pinned launcher slot survives all instances closing")

    apps.open(UNPINNED_A, app_id=KWRITE_ID)
    krema.wait_for_item(UNPINNED_A)
    menu = krema.open_context_menu(UNPINNED_A)
    assert menu.pid == krema.pid
    krema.choose_context_menu_entry("Close", context_menu_entries(pinned=False, is_window=True))
    wait_until(lambda: not [w for w in apps.open_windows if w.refresh()], message="unpinned fixture window to close")
    krema.wait_for_no_item(UNPINNED_A)
    assert krema.item_names() == [env.TEST_APP_NAME]


def test_tzone004_pin_and_unpin_use_real_context_menu_and_preserve_membership(
    krema: Krema, apps: TestWindows
) -> None:
    _configure(krema, edge=config.EDGE_BOTTOM, separate=True, pinned=(env.TEST_APP_ID,))
    apps.open(PINNED_A, app_id=env.TEST_APP_ID)
    apps.open(UNPINNED_A, app_id=KWRITE_ID)
    krema.wait_for_item(PINNED_A)
    krema.wait_for_item(UNPINNED_A)
    original = _launcher_ids(krema)

    krema.open_context_menu(UNPINNED_A)
    krema.choose_context_menu_entry("Pin to Dock", context_menu_entries(pinned=False, is_window=True))
    wait_until(lambda: config.launcher(KWRITE_ID) in _launcher_ids(krema), message="real Pin to Dock action")
    wait_until(lambda: krema.item_names()[:2] == [PINNED_A, UNPINNED_A], message="pinned item to move into pinned zone")

    krema.open_context_menu(UNPINNED_A)
    krema.choose_context_menu_entry("Unpin from Dock", context_menu_entries(pinned=True, is_window=True))
    wait_until(lambda: config.launcher(KWRITE_ID) not in _launcher_ids(krema), message="real Unpin from Dock action")
    assert config.launcher(env.TEST_APP_ID) in _launcher_ids(krema)
    assert original == [config.launcher(env.TEST_APP_ID)]
    wait_until(lambda: krema.item_names() == [PINNED_A, UNPINNED_A], message="unpinned running task to remain visible after unpin")


def _drag_from_rest(
    krema: Krema, source: str, target: str, tag: str, announcements: Announcements | None = None
) -> None:
    """Compute both physical drag endpoints from one settled rest layout."""
    park = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)
    dock = krema.surface_rect("dock")
    assert dock is not None and not dock.contains(*park)
    inp.move(*park)
    settings = krema.read_config().get("General", {})
    icon_size = int(settings.get("IconSize", "48"))
    edge = int(settings.get("Edge", config.EDGE_BOTTOM))
    vertical = edge in (config.EDGE_LEFT, config.EDGE_RIGHT)
    wait_until(
        lambda: all(
            (krema.screen_rect(item).height if vertical else krema.screen_rect(item).width) == icon_size
            for item in krema.items()
        ),
        message="dock to return to unzoomed drag geometry",
    )
    bounds = wait_stable(
        lambda: {name: krema.screen_rect(krema.wait_for_item(name)) for name in (source, target)},
        duration=0.6,
    )
    dock = krema.surface_rect("dock")
    assert dock is not None
    assert all(dock.contains(*rect.center) for rect in bounds.values()), f"drag endpoints outside dock: {bounds}, {dock}"
    artifact = env.artifact_path(f"{krema.name}/drag-{tag}.json")
    metadata = {"source": source, "target": target, "bounds": {name: list(rect) for name, rect in bounds.items()}, "dock": list(dock)}
    if announcements is not None:
        metadata["announcements_before"] = announcements.messages("krema")
    artifact.write_text(json.dumps(metadata), encoding="utf-8")
    try:
        drag(bounds[source].center, [bounds[target].center])
    finally:
        if announcements is not None:
            metadata["announcements_after"] = announcements.messages("krema")
            artifact.write_text(json.dumps(metadata), encoding="utf-8")



def test_tzone005_on_reorders_inside_both_zones_and_clamps_cross_boundary(
    krema: Krema, apps: TestWindows
) -> None:
    _configure(krema, edge=config.EDGE_BOTTOM, separate=True)
    _open_matrix(apps, krema)
    original_membership = set(_launcher_ids(krema))

    # Real pointer drags inside each zone retain the zone's independent order.
    _drag_from_rest(krema, PINNED_B, PINNED_A, "pinned-reorder")
    wait_until(lambda: krema.item_names()[:2] == [PINNED_B, PINNED_A], message="pinned-zone reorder")
    _drag_from_rest(krema, UNPINNED_B, UNPINNED_A, "running-reorder")
    wait_until(lambda: krema.item_names()[2:] == [UNPINNED_B, UNPINNED_A], message="running-zone reorder")

    announcements = Announcements()
    try:
        # Drop the first pinned app past the running zone: it must move to the
        # nearest valid position (last pinned), not merely stay in its zone.
        _drag_from_rest(krema, PINNED_B, UNPINNED_A, "pinned-to-running-clamped", announcements)
        wait_until(
            lambda: krema.item_names() == [PINNED_A, PINNED_B, UNPINNED_B, UNPINNED_A],
            message="pinned-to-running drop clamped to the last pinned position",
        )
        assert set(_launcher_ids(krema)) == original_membership

        # The last running app dropped before the pinned zone must move to the
        # first running position, without becoming pinned.
        _drag_from_rest(krema, UNPINNED_A, PINNED_A, "running-to-pinned-clamped", announcements)
        wait_until(
            lambda: krema.item_names() == [PINNED_A, PINNED_B, UNPINNED_A, UNPINNED_B],
            message="running-to-pinned drop clamped to the first running position",
        )
        assert set(_launcher_ids(krema)) == original_membership
        stable_order = wait_stable(krema.item_names, duration=0.6)
        assert stable_order == [PINNED_A, PINNED_B, UNPINNED_A, UNPINNED_B], "queued partition must not roll back the clamped order"
        env.artifact_path(f"{krema.name}/native-clamp-announcements.json").write_text(
            json.dumps({"order": krema.item_names(), "messages": announcements.messages("krema")}),
            encoding="utf-8",
        )
    finally:
        announcements.close()


def test_tzone006_off_allows_real_cross_zone_reorder_without_auto_pin(
    krema: Krema, apps: TestWindows
) -> None:
    _configure(krema, edge=config.EDGE_BOTTOM, separate=False)
    _open_matrix(apps, krema)
    original_membership = set(_launcher_ids(krema))

    _drag_from_rest(krema, UNPINNED_B, PINNED_A, "running-to-pinned-free")
    wait_until(
        lambda: krema.item_names().index(UNPINNED_B) < krema.item_names().index(PINNED_A),
        message="OFF mode cross-zone drag to reorder freely",
    )
    assert set(_launcher_ids(krema)) == original_membership
    _drag_from_rest(krema, PINNED_A, UNPINNED_B, "pinned-to-running-free")
    wait_until(
        lambda: krema.item_names().index(PINNED_A) < krema.item_names().index(UNPINNED_B),
        message="OFF mode reverse cross-zone drag",
    )
    assert set(_launcher_ids(krema)) == original_membership


def test_tzone007_fresh_default_separation_is_off(krema: Krema, apps: TestWindows) -> None:
    """A fresh config keeps the new preference at its documented false default."""
    apps.open("Default", app_id=env.TEST_APP_ID)
    krema.wait_for_item("Default")
    switch = _open_behavior_switch(krema, pinned=False)
    assert not has_state(switch, "checked")
    assert not config.as_bool(krema.read_config().get("General", {}).get("SeparateLaunchers", "false"))
    close_settings(krema)
