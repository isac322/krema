// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include <catch2/catch_test_macros.hpp>

#include "utils/surfacegeometry.h"

TEST_CASE("Surface geometry calculations", "[geometry]")
{
    SECTION("Zoom overflow height")
    {
        REQUIRE(krema::zoomOverflowHeight(48, 1.6) == 29); // ceil(48 * 0.6)
        REQUIRE(krema::zoomOverflowHeight(48, 1.0) == 0);
        REQUIRE(krema::zoomOverflowHeight(48, 2.0) == 48);
        REQUIRE(krema::zoomOverflowHeight(64, 1.5) == 32); // ceil(64 * 0.5)
    }

    SECTION("Surface height includes all components")
    {
        // iconSize=48, padding=8, zoom=1.6, tooltipReserve=36, floatingPad=8
        int h = krema::surfaceHeight(48, 8, 1.6, 36, 8);
        // dockHeight = 48 + 16 = 64
        // overflow = max(29, 36) = 36
        // total = 64 + 36 + 8 = 108
        REQUIRE(h == 108);
    }

    SECTION("Tooltip reserve wins when zoom overflow is small")
    {
        int h = krema::surfaceHeight(48, 8, 1.2, 36, 0);
        // overflow = ceil(48 * 0.2) = 10
        // max(10, 36) = 36
        // 64 + 36 + 0 = 100
        REQUIRE(h == 100);
    }

    SECTION("Zoom overflow wins when large")
    {
        int h = krema::surfaceHeight(48, 8, 2.0, 36, 0);
        // overflow = ceil(48 * 1.0) = 48
        // max(48, 36) = 48
        // 64 + 48 + 0 = 112
        REQUIRE(h == 112);
    }

    SECTION("Non-floating has zero floating padding")
    {
        int h = krema::surfaceHeight(48, 8, 1.6, 36, 0);
        REQUIRE(h == 100); // 64 + 36 + 0
    }

    SECTION("Panel bar height")
    {
        REQUIRE(krema::panelBarHeight(48, 8, 8) == 72); // 48+16+8
        REQUIRE(krema::panelBarHeight(48, 8, 0) == 64); // non-floating
        REQUIRE(krema::panelBarHeight(64, 8, 0) == 80); // larger icon
    }
}
