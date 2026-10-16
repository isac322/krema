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

    SECTION("Animation overflow includes the indicator cell and bounce")
    {
        REQUIRE(krema::animationOverflowHeight(48, 8, 1.0) == 6);
        REQUIRE(krema::animationOverflowHeight(48, 8, 1.6) == 48);
        REQUIRE(krema::animationOverflowHeight(48, 8, 2.0) == 76);
    }

    SECTION("Surface height includes all components")
    {
        // iconSize=48, padding=8, zoom=1.6, tooltipReserve=36, floatingPad=8
        int h = krema::surfaceHeight(48, 8, 1.6, 36, 8);
        // dockHeight = 48 + 16 = 64
        // animation overflow = ceil(56 * 0.6 + 14 * 1.6 - 8) = 48
        // (56 includes the fixed 8 px indicator cell)
        // total = 64 + 48 + 8 = 120
        REQUIRE(h == 120);
    }

    SECTION("Tooltip reserve wins when zoom overflow is small")
    {
        int h = krema::surfaceHeight(48, 8, 1.2, 36, 0);
        // animation overflow = ceil(56 * 0.2 + 14 * 1.2 - 8) = 20
        // max(20, 36) = 36
        // 64 + 36 + 0 = 100
        REQUIRE(h == 100);
    }

    SECTION("Zoom overflow wins when large")
    {
        int h = krema::surfaceHeight(48, 8, 2.0, 36, 0);
        // animation overflow = ceil(56 * 1.0 + 14 * 2.0 - 8) = 76
        // 64 + 76 + 0 = 140
        REQUIRE(h == 140);
    }

    SECTION("Non-floating has zero floating padding")
    {
        int h = krema::surfaceHeight(48, 8, 1.6, 36, 0);
        REQUIRE(h == 112); // 64 + 48 + 0
    }

    SECTION("Panel bar height")
    {
        REQUIRE(krema::panelBarHeight(48, 8, 8) == 72); // 48+16+8
        REQUIRE(krema::panelBarHeight(48, 8, 0) == 64); // non-floating
        REQUIRE(krema::panelBarHeight(64, 8, 0) == 80); // larger icon
    }

    SECTION("Non-default floating margin is kept apart from padding")
    {
        // padding and the default margin are both 8; a custom margin must
        // only change the floating term.
        REQUIRE(krema::panelBarHeight(48, 8, 20) == 84); // 48+16+20
        REQUIRE(krema::surfaceHeight(48, 8, 1.6, 36, 20) == 132); // 64+48+20
        REQUIRE(krema::panelBarHeight(48, 8, 1) == 65); // minimum margin
        REQUIRE(krema::panelBarHeight(48, 8, 32) == 96); // maximum margin
    }
}
