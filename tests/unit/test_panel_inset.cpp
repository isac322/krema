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

    SECTION("Probe at the screen edge: a visible panel with no reservation still clears its surface")
    {
        // An auto-hide / dodge / windows-go-below panel reserves nothing, but
        // its visible frame still occupies space the dock must clear.
        const QRect probe(511, 767, 2, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(262, 708, 500, 60)}, 1) == krema::PanelInset{0, 60});
    }

    SECTION("Zero-reservation visible panels at every edge clear their surface")
    {
        REQUIRE(krema::computePanelInset(output, QRect(511, 0, 2, 1), {QRect(262, 0, 500, 60)}, 0) == krema::PanelInset{0, 60});
        REQUIRE(krema::computePanelInset(output, QRect(511, 767, 2, 1), {QRect(262, 708, 500, 60)}, 1) == krema::PanelInset{0, 60});
        REQUIRE(krema::computePanelInset(output, QRect(0, 383, 1, 2), {QRect(0, 184, 60, 400)}, 2) == krema::PanelInset{0, 60});
        REQUIRE(krema::computePanelInset(output, QRect(1023, 383, 1, 2), {QRect(964, 184, 60, 400)}, 3) == krema::PanelInset{0, 60});
    }

    SECTION("Zero-reservation visible panels crossing the edge still contribute")
    {
        REQUIRE(krema::computePanelInset(output, QRect(511, 0, 2, 1), {QRect(262, -12, 500, 20)}, 0) == krema::PanelInset{0, 8});
        REQUIRE(krema::computePanelInset(output, QRect(511, 767, 2, 1), {QRect(262, 760, 500, 20)}, 1) == krema::PanelInset{0, 8});
        REQUIRE(krema::computePanelInset(output, QRect(0, 383, 1, 2), {QRect(-12, 184, 20, 400)}, 2) == krema::PanelInset{0, 8});
        REQUIRE(krema::computePanelInset(output, QRect(1023, 383, 1, 2), {QRect(1016, 184, 20, 400)}, 3) == krema::PanelInset{0, 8});
    }

    SECTION("Zero-reservation panels inset from the edge are ignored")
    {
        REQUIRE(krema::computePanelInset(output, QRect(511, 0, 2, 1), {QRect(262, 8, 500, 40)}, 0) == krema::PanelInset{});
        REQUIRE(krema::computePanelInset(output, QRect(511, 767, 2, 1), {QRect(262, 720, 500, 40)}, 1) == krema::PanelInset{});
        REQUIRE(krema::computePanelInset(output, QRect(0, 383, 1, 2), {QRect(8, 184, 40, 400)}, 2) == krema::PanelInset{});
        REQUIRE(krema::computePanelInset(output, QRect(1023, 383, 1, 2), {QRect(976, 184, 40, 400)}, 3) == krema::PanelInset{});
    }

    SECTION("Perpendicular and other-output panels are ignored at zero reservation")
    {
        const QList<QRect> panels{
            QRect(0, 0, 48, 768), // left panel on this output
            QRect(0, 0, 1024, 44), // top panel on this output
            QRect(1024, 708, 1024, 60), // bottom panel of the next output
        };
        REQUIRE(krema::computePanelInset(output, QRect(511, 767, 2, 1), panels, 1) == krema::PanelInset{});
    }

    SECTION("A displaced panel overlapping a reserved band extends its total depth")
    {
        const QRect probe(511, 723, 2, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(262, 700, 500, 48)}, 1) == krema::PanelInset{44, 68});
    }

    SECTION("A panel crossing a reserved edge extends its total depth")
    {
        const QRect probe(511, 723, 2, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(262, 700, 500, 100)}, 1) == krema::PanelInset{44, 68});
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
