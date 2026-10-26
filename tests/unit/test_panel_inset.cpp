// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include <catch2/catch_test_macros.hpp>

#include "utils/panelinset.h"

// Frames below are the ones KWin 6.7.5 reported for a real plasmashell 6.7.5
// panel next to Krema's probe surface on a 1024x768 output.
TEST_CASE("Panel inset from the probe surface", "[panel-inset]")
{
    const QRect output(0, 0, 1024, 768);

    SECTION("No probe reported yet: screen edge")
    {
        REQUIRE(krema::computePanelInset(output, QRect(), {QRect(0, 724, 1024, 44)}, 1) == krema::PanelInset{});
    }

    SECTION("Probe at the screen edge: no panel reserves space, panels there are ignored")
    {
        // An auto-hide / dodge / windows-go-below panel reserves nothing.
        const QRect probe(511, 767, 2, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(262, 708, 500, 60)}, 1) == krema::PanelInset{});
    }

    SECTION("Non-floating bottom panel: the reserved depth")
    {
        const QRect probe(511, 723, 2, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(0, 724, 1024, 44)}, 1) == krema::PanelInset{44, 44});
    }

    SECTION("Floating bottom panel: clears its surface, gap included")
    {
        // Reserves 44 px, but its surface is 60 px tall.
        const QRect probe(511, 723, 2, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(262, 708, 500, 60)}, 1) == krema::PanelInset{44, 60});
    }

    SECTION("Panels on other edges and other outputs do not count")
    {
        const QRect probe(511, 723, 2, 1);
        const QList<QRect> panels{
            QRect(0, 0, 60, 768), // left panel spanning the whole height
            QRect(0, 0, 1024, 44), // top panel
            QRect(1024, 708, 1024, 60), // bottom panel of the next output
        };
        REQUIRE(krema::computePanelInset(output, probe, panels, 1) == krema::PanelInset{44, 44});
    }

    SECTION("Top, left and right edges")
    {
        REQUIRE(krema::computePanelInset(output, QRect(511, 44, 2, 1), {QRect(0, 0, 1024, 60)}, 0) == krema::PanelInset{44, 60});
        REQUIRE(krema::computePanelInset(output, QRect(44, 383, 1, 2), {QRect(0, 184, 60, 400)}, 2) == krema::PanelInset{44, 60});
        REQUIRE(krema::computePanelInset(output, QRect(979, 383, 1, 2), {QRect(964, 184, 60, 400)}, 3) == krema::PanelInset{44, 60});
    }

    SECTION("A perpendicular panel moves the probe along the edge, not its depth")
    {
        // KWin centres the probe in the area a 48 px left panel leaves.
        const QRect probe(535, 723, 2, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(0, 0, 48, 768), QRect(0, 724, 1024, 44)}, 1) == krema::PanelInset{44, 44});
    }

    SECTION("Same-edge floating panels retain their total surface extension")
    {
        REQUIRE(krema::computePanelInset(output, QRect(511, 44, 2, 1), {QRect(262, 0, 500, 60)}, 0) == krema::PanelInset{44, 60});
        REQUIRE(krema::computePanelInset(output, QRect(511, 723, 2, 1), {QRect(262, 708, 500, 60)}, 1) == krema::PanelInset{44, 60});
        REQUIRE(krema::computePanelInset(output, QRect(44, 383, 1, 2), {QRect(0, 184, 60, 400)}, 2) == krema::PanelInset{44, 60});
        REQUIRE(krema::computePanelInset(output, QRect(979, 383, 1, 2), {QRect(964, 184, 60, 400)}, 3) == krema::PanelInset{44, 60});
    }

    SECTION("Output not at the origin")
    {
        const QRect second(1024, 0, 1024, 768);
        REQUIRE(krema::computePanelInset(second, QRect(1535, 723, 2, 1), {QRect(1024, 708, 1024, 60)}, 1) == krema::PanelInset{44, 60});
    }

    SECTION("A probe from another edge is no measurement")
    {
        // Right after the dock moves, the last report still has the probe
        // where the old edge put it.
        const QRect bottomProbe(511, 767, 2, 1);
        REQUIRE(krema::computePanelInset(output, bottomProbe, {}, 0) == krema::PanelInset{});
        REQUIRE(krema::computePanelInset(output, bottomProbe, {}, 2) == krema::PanelInset{});
        REQUIRE(krema::computePanelInset(output, bottomProbe, {}, 3) == krema::PanelInset{});
        const QRect leftProbe(44, 383, 1, 2);
        REQUIRE(krema::computePanelInset(output, leftProbe, {}, 1) == krema::PanelInset{});
        REQUIRE(krema::computePanelInset(output, leftProbe, {}, 3) == krema::PanelInset{});
    }
}
