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
        p.edge = 1; // Bottom

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
        p.edge = 0; // Top

        QRect rect = krema::computeDockScreenRect(p);

        REQUIRE(rect.x() == 800);
        REQUIRE(rect.y() == 0);
    }
}

TEST_CASE("Preview input region matches the visible popup", "[input-region][preview]")
{
    // 400 px deep preview surface; 300x180 popup.
    krema::PreviewInputRegionParams p{};
    p.contentWidth = 300;
    p.contentHeight = 180;

    SECTION("Top edge: popup flush with the top, nothing below it")
    {
        p.surfaceWidth = 1920;
        p.surfaceHeight = 400;
        p.contentX = 800;
        p.edge = 0;

        const QRegion region = krema::computePreviewInputRegion(p);

        REQUIRE(region == QRegion(800, 0, 300, 180));
        REQUIRE_FALSE(region.contains(QPoint(950, 180))); // just below the popup
        REQUIRE_FALSE(region.contains(QPoint(950, 399))); // far side of the surface
    }

    SECTION("Bottom edge: popup flush with the bottom, nothing above it")
    {
        p.surfaceWidth = 1920;
        p.surfaceHeight = 400;
        p.contentX = 800;
        p.edge = 1;

        REQUIRE(krema::computePreviewInputRegion(p) == QRegion(800, 220, 300, 180));
    }

    SECTION("Left edge: popup flush with the left, nothing to its right")
    {
        p.surfaceWidth = 400;
        p.surfaceHeight = 1080;
        p.contentY = 450;
        p.edge = 2;

        REQUIRE(krema::computePreviewInputRegion(p) == QRegion(0, 450, 300, 180));
    }

    SECTION("Right edge: popup flush with the right, nothing to its left")
    {
        p.surfaceWidth = 400;
        p.surfaceHeight = 1080;
        p.contentY = 450;
        p.edge = 3;

        REQUIRE(krema::computePreviewInputRegion(p) == QRegion(100, 450, 300, 180));
    }

    SECTION("Fractional popup geometry is fully covered")
    {
        p.surfaceWidth = 1920;
        p.surfaceHeight = 400;
        p.contentX = 800.5;
        p.contentWidth = 300.5;
        p.contentHeight = 180.5;
        p.edge = 0;

        REQUIRE(krema::computePreviewInputRegion(p) == QRegion(800, 0, 301, 181));
    }

    SECTION("No popup size yet: 1x1 region, never an empty (accept-all) mask")
    {
        p.surfaceWidth = 1920;
        p.surfaceHeight = 400;
        p.contentX = 0;
        p.contentWidth = 0;
        p.contentHeight = 0;
        p.edge = 1;

        const QRegion region = krema::computePreviewInputRegion(p);

        REQUIRE_FALSE(region.isEmpty());
        REQUIRE(region == QRegion(0, 0, 1, 1));
    }
}
