// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include <catch2/catch_test_macros.hpp>

#include "utils/inputregion.h"

TEST_CASE("Dock input region computation", "[input-region]")
{
    SECTION("Hidden: only trigger strip")
    {
        krema::InputRegionParams p{};
        p.surfaceWidth = 1920;
        p.surfaceHeight = 120;
        p.panelX = 800;
        p.panelY = 80;
        p.panelWidth = 320;
        p.panelHeight = 40;
        p.zoomOverflowHeight = 29;
        p.visible = false;
        p.hovered = false;

        QRegion region = krema::computeDockInputRegion(p);

        // Should contain only the trigger strip at the bottom
        REQUIRE(region.contains(QPoint(960, 118))); // center bottom
        REQUIRE(region.contains(QPoint(0, 118))); // left bottom
        REQUIRE_FALSE(region.contains(QPoint(960, 90))); // panel area excluded
    }

    SECTION("Visible but not hovered: panel area + trigger strip")
    {
        krema::InputRegionParams p{};
        p.surfaceWidth = 1920;
        p.surfaceHeight = 120;
        p.panelX = 800;
        p.panelY = 80;
        p.panelWidth = 320;
        p.panelHeight = 40;
        p.zoomOverflowHeight = 29;
        p.visible = true;
        p.hovered = false;

        QRegion region = krema::computeDockInputRegion(p);

        // Panel area should be included (with margin)
        REQUIRE(region.contains(QPoint(960, 100))); // panel center
        REQUIRE(region.contains(QPoint(960, 118))); // trigger strip
        // Far outside panel should be excluded
        REQUIRE_FALSE(region.contains(QPoint(100, 90)));
    }

    SECTION("Visible and hovered: includes zoom overflow")
    {
        krema::InputRegionParams p{};
        p.surfaceWidth = 1920;
        p.surfaceHeight = 120;
        p.panelX = 800;
        p.panelY = 80;
        p.panelWidth = 320;
        p.panelHeight = 40;
        p.zoomOverflowHeight = 29;
        p.visible = true;
        p.hovered = true;

        QRegion region = krema::computeDockInputRegion(p);

        // Zoom overflow area above panel should be included
        REQUIRE(region.contains(QPoint(960, 55))); // above panel in zoom area
        REQUIRE(region.contains(QPoint(960, 100))); // panel center
    }

    SECTION("Panel width zero: fallback to empty (accept all input)")
    {
        krema::InputRegionParams p{};
        p.surfaceWidth = 1920;
        p.surfaceHeight = 120;
        p.panelWidth = 0;
        p.visible = true;
        p.hovered = false;

        QRegion region = krema::computeDockInputRegion(p);

        // Empty QRegion = no mask = accept all input
        REQUIRE(region.isEmpty());
    }
}

TEST_CASE("Dock screen rect computation", "[screen-rect]")
{
    SECTION("Bottom edge")
    {
        krema::DockScreenRectParams p{};
        p.screenX = 0;
        p.screenY = 0;
        p.screenWidth = 1920;
        p.screenHeight = 1080;
        p.surfaceWidth = 1920;
        p.surfaceHeight = 120;
        p.panelX = 800;
        p.panelRefY = 80;
        p.panelWidth = 320;
        p.panelHeight = 40;
        p.edge = 0; // Bottom

        QRect rect = krema::computeDockScreenRect(p);

        // Surface top-left: (0, 1080-120) = (0, 960)
        // Panel: (0+800, 960+80, 320, 40) = (800, 1040, 320, 40)
        REQUIRE(rect.x() == 800);
        REQUIRE(rect.y() == 1040);
        REQUIRE(rect.width() == 320);
        REQUIRE(rect.height() == 40);
    }

    SECTION("Top edge")
    {
        krema::DockScreenRectParams p{};
        p.screenX = 0;
        p.screenY = 0;
        p.screenWidth = 1920;
        p.screenHeight = 1080;
        p.surfaceWidth = 1920;
        p.surfaceHeight = 120;
        p.panelX = 800;
        p.panelRefY = 0;
        p.panelWidth = 320;
        p.panelHeight = 40;
        p.edge = 1; // Top

        QRect rect = krema::computeDockScreenRect(p);

        REQUIRE(rect.x() == 800);
        REQUIRE(rect.y() == 0);
    }
}
