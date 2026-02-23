// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "utils/zoomcalculator.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("Parabolic zoom factor", "[zoom]")
{
    SECTION("Distance 0 gives maximum zoom")
    {
        double f = krema::parabolicZoomFactor(0.0, 48.0, 1.6);
        REQUIRE_THAT(f, WithinAbs(1.6, 0.001));
    }

    SECTION("Large distance gives zoom near 1.0")
    {
        double f = krema::parabolicZoomFactor(500.0, 48.0, 1.6);
        REQUIRE_THAT(f, WithinAbs(1.0, 0.01));
    }

    SECTION("maxZoomFactor 1.0 always returns 1.0")
    {
        double f = krema::parabolicZoomFactor(0.0, 48.0, 1.0);
        REQUIRE_THAT(f, WithinAbs(1.0, 0.001));
    }

    SECTION("maxZoomFactor below 1.0 returns 1.0")
    {
        double f = krema::parabolicZoomFactor(0.0, 48.0, 0.5);
        REQUIRE_THAT(f, WithinAbs(1.0, 0.001));
    }

    SECTION("Zoom decreases with distance")
    {
        double f10 = krema::parabolicZoomFactor(10.0, 48.0, 1.6);
        double f30 = krema::parabolicZoomFactor(30.0, 48.0, 1.6);
        double f60 = krema::parabolicZoomFactor(60.0, 48.0, 1.6);
        REQUIRE(f10 > f30);
        REQUIRE(f30 > f60);
    }

    SECTION("Symmetry: positive and negative distance give same result")
    {
        double fPos = krema::parabolicZoomFactor(25.0, 48.0, 1.6);
        double fNeg = krema::parabolicZoomFactor(-25.0, 48.0, 1.6);
        REQUIRE_THAT(fPos, WithinAbs(fNeg, 0.0001));
    }

    SECTION("Different icon sizes scale the curve")
    {
        // Larger icon → wider Gaussian → less falloff at same distance
        double fSmall = krema::parabolicZoomFactor(30.0, 32.0, 1.6);
        double fLarge = krema::parabolicZoomFactor(30.0, 64.0, 1.6);
        REQUIRE(fLarge > fSmall);
    }
}
