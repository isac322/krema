# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Pure geometry regression tests; may run with pytest --noconftest."""

import pytest

from krema_e2e import config
from krema_e2e.krema import Rect, dock_padding_target


@pytest.mark.parametrize("zoom", [1.0, 1.6, 3.0])
@pytest.mark.parametrize("edge,expected", [
    (config.EDGE_TOP, (456, 32)),
    (config.EDGE_BOTTOM, (456, 736)),
    (config.EDGE_LEFT, (32, 328)),
    (config.EDGE_RIGHT, (992, 328)),
])
def test_padding_stays_inside_resting_panel_as_zoom_retracts(zoom, edge, expected):
    # Two 48px slots with 4px spacing: rest content is 100px, centred.
    # The slot's hit area includes half the spacing; the target must be
    # outside it, but within even an 8px resting panel padding.
    area = Rect(0, 0, 1024, 768)
    settings = {"Edge": edge, "MaxZoomFactor": zoom}
    assert dock_padding_target(area, 2, settings) == expected


def test_default_edge_and_empty_dock():
    assert dock_padding_target(Rect(0, 668, 1024, 100), 0, {}) == (512, 736)


def test_second_output_and_separator_use_resting_extent():
    area = Rect(1024, 668, 1024, 100)
    assert dock_padding_target(area, 2, {}, separator=True) == (1478, 736)


def test_nonfloating_dock_ignores_stored_floating_margin():
    settings = {"Floating": "false", "FloatingMargin": 128}
    assert dock_padding_target(Rect(0, 668, 1024, 100), 2, settings) == (456, 744)
