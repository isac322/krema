# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Pure geometry checks; live routing is covered by CTX-012."""

import pytest

from krema_e2e import config
from krema_e2e.krema import Rect, dock_menu_neutral_point, dock_padding_target


@pytest.mark.parametrize("spacing", [0, 4, 32, 64])
@pytest.mark.parametrize("edge", [config.EDGE_TOP, config.EDGE_BOTTOM, config.EDGE_LEFT, config.EDGE_RIGHT])
def test_target_is_inside_panel_but_outside_unzoomed_hit_span(edge, spacing):
    # Two rest delegates, 8px panel padding, and an 8px indicator strip.
    # Large spacing can extend the primary hit slot across leading padding;
    # the target must therefore be outside the secondary hit span as well.
    primary = ((768 if edge >= config.EDGE_LEFT else 1024) - (96 + spacing)) // 2
    if edge == config.EDGE_TOP:
        first, panel = Rect(primary, 8, 48, 56), Rect(primary - 8, 8, 112 + spacing, 64)
    elif edge == config.EDGE_BOTTOM:
        first, panel = Rect(primary, 704, 48, 56), Rect(primary - 8, 696, 112 + spacing, 64)
    elif edge == config.EDGE_LEFT:
        first, panel = Rect(8, primary, 56, 48), Rect(8, primary - 8, 64, 112 + spacing)
    else:
        first, panel = Rect(960, primary, 56, 48), Rect(952, primary - 8, 64, 112 + spacing)
    target = dock_padding_target(Rect(0, 0, 1024, 768), {"Edge": edge, "IconSpacing": spacing}, first)
    assert panel.contains(*target)
    assert not first.contains(*target)
    if edge <= config.EDGE_BOTTOM:
        assert target[1] < first.y or target[1] >= first.y + first.height
    else:
        assert target[0] < first.x or target[0] >= first.x + first.width


@pytest.mark.parametrize(
    "edge, area, preview",
    [
        (config.EDGE_TOP, Rect(0, 0, 1024, 120), None),
        (config.EDGE_TOP, Rect(0, 0, 1024, 120), Rect(0, 120, 1024, 400)),
        (config.EDGE_BOTTOM, Rect(0, 648, 1024, 120), None),
        (config.EDGE_BOTTOM, Rect(0, 648, 1024, 120), Rect(0, 248, 1024, 400)),
        (config.EDGE_LEFT, Rect(0, 0, 120, 768), None),
        (config.EDGE_LEFT, Rect(0, 0, 120, 768), Rect(120, 0, 400, 768)),
        (config.EDGE_RIGHT, Rect(904, 0, 120, 768), None),
        (config.EDGE_RIGHT, Rect(904, 0, 120, 768), Rect(504, 0, 400, 768)),
    ],
)
def test_neutral_point_stays_inside_output_and_avoids_surfaces(edge, area, preview):
    output = Rect(0, 0, 1024, 768)
    target = dock_menu_neutral_point(area, edge, preview, output)

    assert output.contains(*target)
    assert not area.contains(*target)
    if preview is not None:
        assert not preview.contains(*target)


def test_set023_bottom_preview_neutral_point():
    area = Rect(0, 648, 1024, 120)
    preview = Rect(0, 248, 1024, 400)
    output = Rect(0, 0, 1024, 768)

    assert dock_menu_neutral_point(area, config.EDGE_BOTTOM, preview, output) == (512, 216)


@pytest.mark.parametrize(
    "first_preview, second_preview",
    [(None, None), (Rect(0, 248, 1024, 400), Rect(1024, 248, 1024, 400))],
)
def test_neutral_point_translates_with_second_output(first_preview, second_preview):
    first_area = Rect(0, 648, 1024, 120)
    first_output = Rect(0, 0, 1024, 768)
    second_area = Rect(1024, 648, 1024, 120)
    second_output = Rect(1024, 0, 1024, 768)

    first_target = dock_menu_neutral_point(first_area, config.EDGE_BOTTOM, first_preview, first_output)
    second_target = dock_menu_neutral_point(second_area, config.EDGE_BOTTOM, second_preview, second_output)

    assert second_output.contains(*second_target)
    assert not first_area.contains(*first_target)
    assert not second_area.contains(*second_target)
    if first_preview is not None:
        assert not first_preview.contains(*first_target)
    if second_preview is not None:
        assert not second_preview.contains(*second_target)


@pytest.mark.parametrize(
    "edge, area, preview",
    [
        (config.EDGE_TOP, Rect(0, 0, 1024, 120), Rect(0, 120, 1024, 616)),
        (config.EDGE_BOTTOM, Rect(0, 648, 1024, 120), Rect(0, 31, 1024, 617)),
        (config.EDGE_LEFT, Rect(0, 0, 120, 768), Rect(120, 0, 872, 768)),
        (config.EDGE_RIGHT, Rect(904, 0, 120, 768), Rect(31, 0, 873, 768)),
    ],
)
def test_neutral_point_rejects_when_no_on_screen_escape_exists(edge, area, preview):
    output = Rect(0, 0, 1024, 768)

    with pytest.raises(AssertionError, match="output="):
        dock_menu_neutral_point(area, edge, preview, output)


def test_empty_dock_and_default_edge():
    assert dock_padding_target(Rect(0, 668, 1024, 100), {}) == (512, 736)


def test_second_output_uses_observed_rest_geometry():
    assert dock_padding_target(Rect(1024, 668, 1024, 100), {}, Rect(1486, 704, 48, 56)) == (1482, 700)


@pytest.mark.parametrize(
    "area, edge, hidden_first, visible_first",
    [
        (
            Rect(0, 648, 1024, 120),
            config.EDGE_BOTTOM,
            Rect(488, 784, 48, 56),
            Rect(488, 704, 48, 56),
        ),
        (
            Rect(0, 0, 350, 768),
            config.EDGE_LEFT,
            Rect(-72, 360, 56, 48),
            Rect(8, 360, 56, 48),
        ),
    ],
)
def test_padding_target_rejects_hidden_first_but_accepts_visible_control(
    area, edge, hidden_first, visible_first
):
    with pytest.raises(AssertionError, match="dock item|dock surface"):
        dock_padding_target(area, {"Edge": edge}, hidden_first)

    target = dock_padding_target(area, {"Edge": edge}, visible_first)
    assert area.contains(*target)
    assert not visible_first.contains(*target)
