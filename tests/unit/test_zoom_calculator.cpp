// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "utils/zoomcalculator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <vector>

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

    SECTION("Curve at one sigma matches the dock's exp(-(d/sigma)^2) Gaussian")
    {
        // sigma = 48 * 1.2 = 57.6 → factor at d = sigma is 1 + 0.6 / e
        double f = krema::parabolicZoomFactor(57.6, 48.0, 1.6);
        REQUIRE_THAT(f, WithinAbs(1.0 + 0.6 * std::exp(-1.0), 1e-9));
    }

    SECTION("Wider sigma factor spreads the zoom to farther icons")
    {
        double fTight = krema::parabolicZoomFactor(52.0, 48.0, 1.6, 0.8);
        double fWide = krema::parabolicZoomFactor(52.0, 48.0, 1.6, 1.8);
        REQUIRE(fWide > fTight);
    }
}

namespace
{

// Test row: 12 icons of 48px with 4px spacing resting at [10, restEnd),
// background 8px wider on each side.
constexpr int kCount = 12;
constexpr double kRestStart = 10.0;
constexpr double kIconSize = 48.0;
constexpr double kSpacing = 4.0;
constexpr double kPitch = kIconSize + kSpacing;
constexpr double kRestEnd = kRestStart + kCount * kPitch - kSpacing;
constexpr double kBackgroundMargin = 8.0;
constexpr double kBgStart = kRestStart - kBackgroundMargin;
constexpr double kBgEnd = kRestEnd + kBackgroundMargin;
constexpr double kRowCentre = (kBgStart + kBgEnd) / 2.0; // background is symmetric about the row
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kZoomLevels[] = {1.4, 1.6, 2.0};
constexpr krema::ZoomStyle kStyles[] = {krema::ZoomStyle::Parabolic, krema::ZoomStyle::InPlace};

krema::DockZoomLayout
zoomAt(double cursor, double maxZoom, krema::ZoomStyle style = krema::ZoomStyle::Parabolic, bool active = true, double minEdge = -kInf, double maxEdge = kInf)
{
    return krema::computeDockZoom(kCount, kRestStart, kIconSize, kSpacing, -1, 0.0, kBgStart, kBgEnd, maxZoom, style, active, cursor, minEdge, maxEdge);
}

double restCentre(int i)
{
    return kRestStart + i * kPitch + kIconSize / 2.0;
}

double zoomedCentre(const krema::DockZoomLayout &layout, int i)
{
    return restCentre(i) + layout.offsets[static_cast<std::size_t>(i)];
}

double zoomedWidth(const krema::DockZoomLayout &layout, int i)
{
    return kIconSize * layout.scales[static_cast<std::size_t>(i)];
}

double zoomedLeading(const krema::DockZoomLayout &layout, int i)
{
    return zoomedCentre(layout, i) - zoomedWidth(layout, i) / 2.0;
}

double zoomedTrailing(const krema::DockZoomLayout &layout, int i)
{
    return zoomedCentre(layout, i) + zoomedWidth(layout, i) / 2.0;
}

double totalGrowth(const krema::DockZoomLayout &layout)
{
    double growth = 0.0;
    for (double scale : layout.scales) {
        growth += kIconSize * (scale - 1.0);
    }
    return growth;
}

// Cursor positions in 0.25px steps over [first, last].
std::vector<double> positions(double first, double last)
{
    std::vector<double> xs;
    for (double x = first; x <= last + 1e-9; x += 0.25) {
        xs.push_back(x);
    }
    return xs;
}

// From two pitches before the background to two pitches after it.
std::vector<double> sweepPositions()
{
    return positions(kBgStart - 2.0 * kPitch, kBgEnd + 2.0 * kPitch);
}

// Direction reversals of a sampled value, ignoring wiggles up to @p tolerance:
// a reversal counts once the value has moved back by more than the tolerance
// from its latest extreme.
int reversals(const std::vector<double> &values, double tolerance)
{
    int count = 0;
    int direction = 0; // 0 = not moved beyond the tolerance yet
    double high = values.front();
    double low = values.front();
    for (double v : values) {
        high = std::max(high, v);
        low = std::min(low, v);
        if (direction == 0) {
            if (high - v > tolerance) {
                direction = -1;
                low = v;
            } else if (v - low > tolerance) {
                direction = 1;
                high = v;
            }
        } else if (direction > 0 && high - v > tolerance) {
            ++count;
            direction = -1;
            low = v;
        } else if (direction < 0 && v - low > tolerance) {
            ++count;
            direction = 1;
            high = v;
        }
    }
    return count;
}

void requireRestLayout(const krema::DockZoomLayout &layout)
{
    REQUIRE(layout.scales.size() == static_cast<std::size_t>(kCount));
    REQUIRE(layout.offsets.size() == static_cast<std::size_t>(kCount));
    for (std::size_t i = 0; i < static_cast<std::size_t>(kCount); ++i) {
        REQUIRE(layout.scales[i] == 1.0);
        REQUIRE(layout.offsets[i] == 0.0);
    }
    REQUIRE(layout.leadingGrowth == 0.0);
    REQUIRE(layout.trailingGrowth == 0.0);
}

} // namespace

TEST_CASE("Dock zoom rest state", "[zoom][layout]")
{
    SECTION("Inactive layout is the rest layout for every style")
    {
        for (krema::ZoomStyle style : kStyles) {
            for (double cursor : {-200.0, kBgStart, restCentre(3), restCentre(3) + 13.0, kBgEnd + 500.0}) {
                requireRestLayout(zoomAt(cursor, 1.6, style, /*active=*/false));
            }
        }
    }

    SECTION("maxZoomFactor at or below 1 keeps every item at rest")
    {
        for (krema::ZoomStyle style : kStyles) {
            for (double maxZoom : {1.0, 0.5}) {
                requireRestLayout(zoomAt(restCentre(3), maxZoom, style));
            }
        }
    }

    SECTION("Empty row yields no items")
    {
        for (int count : {0, -3}) {
            const auto layout = krema::computeDockZoom(count,
                                                       kRestStart,
                                                       kIconSize,
                                                       kSpacing,
                                                       -1,
                                                       0.0,
                                                       kBgStart,
                                                       kBgEnd,
                                                       1.6,
                                                       krema::ZoomStyle::Parabolic,
                                                       true,
                                                       restCentre(0),
                                                       -kInf,
                                                       kInf);
            REQUIRE(layout.scales.empty());
            REQUIRE(layout.offsets.empty());
            REQUIRE(layout.leadingGrowth == 0.0);
            REQUIRE(layout.trailingGrowth == 0.0);
        }
    }
}

TEST_CASE("Separated parabolic zoom keeps its fixed boundary gap and bounded motion", "[zoom][layout][separator]")
{
    constexpr int count = 6;
    constexpr int boundary = 3;
    constexpr double extra = kSpacing + 1.0;
    constexpr double restEnd = kRestStart + count * kPitch - kSpacing + extra;
    constexpr double backgroundEnd = restEnd + kBackgroundMargin;
    constexpr double rowCentre = (kBgStart + backgroundEnd) / 2.0;
    const auto centre = [](int i) {
        return kRestStart + i * kPitch + kIconSize / 2.0 + (i >= boundary ? extra : 0.0);
    };
    const auto calculate = [](double cursor, double minEdge, double maxEdge, bool active, krema::ZoomStyle style) {
        return krema::
            computeDockZoom(count, kRestStart, kIconSize, kSpacing, boundary, extra, kBgStart, backgroundEnd, 1.6, style, active, cursor, minEdge, maxEdge);
    };

    SECTION("Rest and InPlace preserve actual rest centres without translations")
    {
        for (const auto style : kStyles) {
            const auto rest = calculate(centre(boundary), -kInf, kInf, false, style);
            REQUIRE(rest.leadingGrowth == 0.0);
            REQUIRE(rest.trailingGrowth == 0.0);
            for (int i = 0; i < count; ++i) {
                REQUIRE(rest.scales[static_cast<std::size_t>(i)] == 1.0);
                REQUIRE(rest.offsets[static_cast<std::size_t>(i)] == 0.0);
            }
        }
        const auto inPlace = calculate(centre(boundary), kBgStart, backgroundEnd, true, krema::ZoomStyle::InPlace);
        for (int i = 0; i < count; ++i) {
            REQUIRE(inPlace.offsets[static_cast<std::size_t>(i)] == 0.0);
            REQUIRE_THAT(inPlace.scales[static_cast<std::size_t>(i)],
                         WithinAbs(krema::parabolicZoomFactor(centre(i) - centre(boundary), kIconSize, 1.6), 1e-12));
        }
    }

    SECTION("Full positional room leaves normal gaps and a twice-spaced stroke slot")
    {
        for (double cursor : {centre(boundary - 1), rowCentre, centre(boundary)}) {
            const auto layout = calculate(cursor, -kInf, kInf, true, krema::ZoomStyle::Parabolic);
            const auto mirrored = calculate(2.0 * rowCentre - cursor, -kInf, kInf, true, krema::ZoomStyle::Parabolic);
            REQUIRE_THAT(layout.leadingGrowth, WithinAbs(mirrored.trailingGrowth, 1e-9));
            for (int i = 1; i < count; ++i) {
                const auto previous = static_cast<std::size_t>(i - 1);
                const auto current = static_cast<std::size_t>(i);
                const double trailing = centre(i - 1) + layout.offsets[previous] + kIconSize * layout.scales[previous] / 2.0;
                const double leading = centre(i) + layout.offsets[current] - kIconSize * layout.scales[current] / 2.0;
                REQUIRE_THAT(leading - trailing, WithinAbs(kSpacing + (i == boundary ? extra : 0.0), 1e-9));
            }
        }
    }

    SECTION("Tight surface bounds preserve full scales and continuous contained positional slots")
    {
        constexpr double roomL = 10.0;
        constexpr double roomR = 15.0;
        const auto cursors = positions(centre(boundary - 2), centre(boundary + 1));
        auto previous = calculate(cursors.front(), kBgStart - roomL, backgroundEnd + roomR, true, krema::ZoomStyle::Parabolic);
        for (double cursor : cursors) {
            const auto free = calculate(cursor, -kInf, kInf, true, krema::ZoomStyle::Parabolic);
            const auto layout = calculate(cursor, kBgStart - roomL, backgroundEnd + roomR, true, krema::ZoomStyle::Parabolic);
            REQUIRE(layout.scales == free.scales);
            REQUIRE(layout.leadingGrowth <= roomL + 1e-9);
            REQUIRE(layout.trailingGrowth <= roomR + 1e-9);
            const double share = (layout.leadingGrowth + layout.trailingGrowth) / (free.leadingGrowth + free.trailingGrowth);
            const auto positionalWidth = [&layout, share](int i) {
                return kIconSize * (1.0 + (layout.scales[static_cast<std::size_t>(i)] - 1.0) * share);
            };
            const double first = centre(0) + layout.offsets.front() - positionalWidth(0) / 2.0;
            const double last = centre(count - 1) + layout.offsets.back() + positionalWidth(count - 1) / 2.0;
            REQUIRE(first >= kRestStart - roomL - 1e-9);
            REQUIRE(last <= restEnd + roomR + 1e-9);
            const double before = centre(boundary - 1) + layout.offsets[boundary - 1] + positionalWidth(boundary - 1) / 2.0;
            const double after = centre(boundary) + layout.offsets[boundary] - positionalWidth(boundary) / 2.0;
            REQUIRE_THAT(after - before, WithinAbs(kSpacing + extra, 1e-9));
            for (int i = 0; i < count; ++i) {
                const auto index = static_cast<std::size_t>(i);
                REQUIRE(std::abs(layout.offsets[index] - previous.offsets[index]) < 0.5);
                REQUIRE(std::abs(layout.scales[index] - previous.scales[index]) < 0.02);
            }
            previous = layout;
        }
    }
}

TEST_CASE("Absent separator zones retain the ordinary zoom layout", "[zoom][layout][separator]")
{
    for (const auto style : kStyles) {
        for (int boundary : {-1, kCount}) {
            for (double cursor : {restCentre(0), kRowCentre, restCentre(kCount - 1)}) {
                const auto ordinary = zoomAt(cursor, 1.6, style);
                const auto absent = krema::
                    computeDockZoom(kCount, kRestStart, kIconSize, kSpacing, boundary, kSpacing + 1.0, kBgStart, kBgEnd, 1.6, style, true, cursor, -kInf, kInf);
                REQUIRE(absent.scales == ordinary.scales);
                REQUIRE(absent.offsets == ordinary.offsets);
                REQUIRE(absent.leadingGrowth == ordinary.leadingGrowth);
                REQUIRE(absent.trailingGrowth == ordinary.trailingGrowth);
            }
        }
    }
}

TEST_CASE("Parabolic zoom scales along the Gaussian curve", "[zoom][layout]")
{
    for (double maxZoom : kZoomLevels) {
        for (double cursor : sweepPositions()) {
            const auto layout = zoomAt(cursor, maxZoom);
            for (int i = 0; i < kCount; ++i) {
                const double expected = krema::parabolicZoomFactor(restCentre(i) - cursor, kIconSize, maxZoom);
                REQUIRE_THAT(layout.scales[static_cast<std::size_t>(i)], WithinAbs(expected, 1e-12));
            }
        }
    }
}

TEST_CASE("Parabolic zoom keeps the dock still while the cursor crosses its middle", "[zoom][layout]")
{
    // The defect this replaces: pinning the icon under the cursor to the
    // cursor made both background edges and every icon slide back and forth
    // once per icon while the pointer moved along the dock.
    for (double maxZoom : kZoomLevels) {
        const auto reference = zoomAt(kRowCentre, maxZoom);
        for (double cursor : positions(restCentre(3), restCentre(kCount - 4))) {
            const auto layout = zoomAt(cursor, maxZoom);
            REQUIRE_THAT(layout.leadingGrowth, WithinAbs(reference.leadingGrowth, 0.01));
            REQUIRE_THAT(layout.trailingGrowth, WithinAbs(reference.trailingGrowth, 0.01));
            // The outermost icons are beyond the bump: they must not track the cursor.
            REQUIRE_THAT(zoomedCentre(layout, 0), WithinAbs(zoomedCentre(reference, 0), 0.05));
            REQUIRE_THAT(zoomedCentre(layout, kCount - 1), WithinAbs(zoomedCentre(reference, kCount - 1), 0.05));
        }
        // The growth is shared evenly while the bump lies fully over the row.
        REQUIRE_THAT(reference.leadingGrowth, WithinAbs(reference.trailingGrowth, 1e-6));
        REQUIRE(reference.leadingGrowth > 0.0);
    }
}

TEST_CASE("Parabolic zoom moves each background edge back at most once per sweep", "[zoom][layout]")
{
    // Each edge grows as the bump arrives and shrinks as it leaves; it must
    // never oscillate while the cursor moves in one direction.
    for (double maxZoom : kZoomLevels) {
        std::vector<double> leadingEdge;
        std::vector<double> trailingEdge;
        for (double cursor : sweepPositions()) {
            const auto layout = zoomAt(cursor, maxZoom);
            leadingEdge.push_back(kBgStart - layout.leadingGrowth);
            trailingEdge.push_back(kBgEnd + layout.trailingGrowth);
        }
        REQUIRE(reversals(leadingEdge, 0.01) <= 1);
        REQUIRE(reversals(trailingEdge, 0.01) <= 1);
    }
}

TEST_CASE("Parabolic zoom pushes neighbours aside with the unscaled gap", "[zoom][layout]")
{
    for (double maxZoom : kZoomLevels) {
        for (double cursor : sweepPositions()) {
            const auto layout = zoomAt(cursor, maxZoom);
            for (int i = 0; i + 1 < kCount; ++i) {
                REQUIRE_THAT(zoomedLeading(layout, i + 1) - zoomedTrailing(layout, i), WithinAbs(kSpacing, 1e-9));
            }
            // The background grows by exactly the row's growth and still
            // wraps the zoomed row with its rest margin.
            REQUIRE_THAT(layout.leadingGrowth + layout.trailingGrowth, WithinAbs(totalGrowth(layout), 1e-9));
            REQUIRE_THAT(zoomedLeading(layout, 0), WithinAbs(kRestStart - layout.leadingGrowth, 1e-9));
            REQUIRE_THAT(zoomedTrailing(layout, kCount - 1), WithinAbs(kRestEnd + layout.trailingGrowth, 1e-9));
            REQUIRE(layout.leadingGrowth >= 0.0);
            REQUIRE(layout.trailingGrowth >= 0.0);
        }
    }
}

TEST_CASE("Parabolic zoom keeps the hovered icon under the cursor", "[zoom][layout]")
{
    for (double maxZoom : kZoomLevels) {
        // On an icon centre the icon does not move.
        for (int i = 3; i < kCount - 3; ++i) {
            const auto layout = zoomAt(restCentre(i), maxZoom);
            REQUIRE_THAT(layout.offsets[static_cast<std::size_t>(i)], WithinAbs(0.0, 0.01));
            REQUIRE_THAT(layout.scales[static_cast<std::size_t>(i)], WithinAbs(maxZoom, 1e-12));
        }
        // Anywhere over an icon's rest slot the cursor stays over that zoomed
        // icon's slot (up to a sub-pixel drift right at the slot boundary).
        for (double cursor : positions(restCentre(2), restCentre(kCount - 3))) {
            const int slot = static_cast<int>(std::floor((cursor - (kRestStart - kSpacing / 2.0)) / kPitch));
            const auto layout = zoomAt(cursor, maxZoom);
            REQUIRE(std::abs(cursor - zoomedCentre(layout, slot)) <= zoomedWidth(layout, slot) / 2.0 + kSpacing / 2.0 + 0.1);
        }
    }
}

TEST_CASE("Parabolic zoom mirrors about the row centre", "[zoom][layout]")
{
    for (double maxZoom : kZoomLevels) {
        for (double cursor : sweepPositions()) {
            const auto forward = zoomAt(cursor, maxZoom);
            const auto mirrored = zoomAt(2.0 * kRowCentre - cursor, maxZoom);
            for (int i = 0; i < kCount; ++i) {
                const auto j = static_cast<std::size_t>(kCount - 1 - i);
                const auto k = static_cast<std::size_t>(i);
                REQUIRE_THAT(forward.scales[k], WithinAbs(mirrored.scales[j], 1e-9));
                REQUIRE_THAT(forward.offsets[k], WithinAbs(-mirrored.offsets[j], 1e-9));
            }
            REQUIRE_THAT(forward.leadingGrowth, WithinAbs(mirrored.trailingGrowth, 1e-9));
            REQUIRE_THAT(forward.trailingGrowth, WithinAbs(mirrored.leadingGrowth, 1e-9));
        }
    }
}

TEST_CASE("Dock zoom layout changes continuously as the cursor sweeps", "[zoom][layout]")
{
    for (krema::ZoomStyle style : kStyles) {
        for (double maxZoom : kZoomLevels) {
            const auto cursors = sweepPositions();
            auto previous = zoomAt(cursors.front(), maxZoom, style);
            for (std::size_t k = 1; k < cursors.size(); ++k) {
                const auto current = zoomAt(cursors[k], maxZoom, style);
                for (std::size_t i = 0; i < static_cast<std::size_t>(kCount); ++i) {
                    // A 0.25px cursor step must never make an icon jump.
                    REQUIRE(std::abs(current.offsets[i] - previous.offsets[i]) < 0.5);
                    REQUIRE(std::abs(current.scales[i] - previous.scales[i]) < 0.02);
                }
                REQUIRE(std::abs(current.leadingGrowth - previous.leadingGrowth) < 0.5);
                REQUIRE(std::abs(current.trailingGrowth - previous.trailingGrowth) < 0.5);
                previous = current;
            }
        }
    }
}

TEST_CASE("Parabolic zoom growth is capped by the surface bounds", "[zoom][layout]")
{
    SECTION("Ample room leaves the layout unchanged")
    {
        for (double cursor : sweepPositions()) {
            const auto free = zoomAt(cursor, 1.6);
            const auto bounded = zoomAt(cursor, 1.6, krema::ZoomStyle::Parabolic, true, kBgStart - 1000.0, kBgEnd + 1000.0);
            REQUIRE(bounded.scales == free.scales);
            REQUIRE(bounded.offsets == free.offsets);
            REQUIRE(bounded.leadingGrowth == free.leadingGrowth);
            REQUIRE(bounded.trailingGrowth == free.trailingGrowth);
        }
    }

    SECTION("Tight room keeps the growth inside the bounds and the full zoom")
    {
        constexpr double roomL = 10.0;
        constexpr double roomR = 15.0;
        for (double maxZoom : kZoomLevels) {
            for (double cursor : sweepPositions()) {
                const auto free = zoomAt(cursor, maxZoom);
                const auto layout = zoomAt(cursor, maxZoom, krema::ZoomStyle::Parabolic, true, kBgStart - roomL, kBgEnd + roomR);
                REQUIRE(layout.leadingGrowth <= roomL + 1e-9);
                REQUIRE(layout.trailingGrowth <= roomR + 1e-9);
                REQUIRE(layout.scales == free.scales);
                // The row's positional extent stays inside the grown background.
                REQUIRE(zoomedCentre(layout, 0) - kIconSize / 2.0 >= kRestStart - roomL - 1e-9);
                REQUIRE(zoomedCentre(layout, kCount - 1) + kIconSize / 2.0 <= kRestEnd + roomR + 1e-9);
            }
        }
    }

    SECTION("One blocked side sends all growth to the other")
    {
        const auto layout = zoomAt(kRowCentre, 1.6, krema::ZoomStyle::Parabolic, true, kBgStart, kInf);
        REQUIRE(layout.leadingGrowth == 0.0);
        REQUIRE_THAT(layout.trailingGrowth, WithinAbs(totalGrowth(layout), 1e-9));
    }

    SECTION("No room at all zooms in place")
    {
        const auto layout = zoomAt(kRowCentre, 1.6, krema::ZoomStyle::Parabolic, true, kBgStart, kBgEnd);
        const auto inPlace = zoomAt(kRowCentre, 1.6, krema::ZoomStyle::InPlace);
        REQUIRE(layout.scales == inPlace.scales);
        for (double offset : layout.offsets) {
            REQUIRE_THAT(offset, WithinAbs(0.0, 1e-9));
        }
        REQUIRE(layout.leadingGrowth == 0.0);
        REQUIRE(layout.trailingGrowth == 0.0);
    }
}

TEST_CASE("In-place dock zoom scales without moving anything", "[zoom][layout]")
{
    for (double maxZoom : kZoomLevels) {
        for (double cursor : sweepPositions()) {
            const auto layout = zoomAt(cursor, maxZoom, krema::ZoomStyle::InPlace);
            const auto bounded = zoomAt(cursor, maxZoom, krema::ZoomStyle::InPlace, true, kBgStart, kBgEnd);
            for (int i = 0; i < kCount; ++i) {
                const double expected = krema::parabolicZoomFactor(restCentre(i) - cursor, kIconSize, maxZoom);
                REQUIRE_THAT(layout.scales[static_cast<std::size_t>(i)], WithinAbs(expected, 1e-12));
                REQUIRE(layout.offsets[static_cast<std::size_t>(i)] == 0.0);
            }
            REQUIRE(layout.leadingGrowth == 0.0);
            REQUIRE(layout.trailingGrowth == 0.0);
            REQUIRE(bounded.scales == layout.scales);
            REQUIRE(bounded.offsets == layout.offsets);
        }
    }
}
