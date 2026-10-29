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


@pytest.mark.parametrize("edge", [config.EDGE_TOP, config.EDGE_BOTTOM, config.EDGE_LEFT, config.EDGE_RIGHT])
def test_neutral_point_is_outside_entire_surface(edge):
    area = Rect(0, 0, 1024, 768)
    assert not area.contains(*dock_menu_neutral_point(area, edge))


def test_empty_dock_and_default_edge():
    assert dock_padding_target(Rect(0, 668, 1024, 100), {}) == (512, 736)


def test_second_output_uses_observed_rest_geometry():
    assert dock_padding_target(Rect(1024, 668, 1024, 100), {}, Rect(1486, 704, 48, 56)) == (1482, 700)
