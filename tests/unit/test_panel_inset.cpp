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
        const QRect probe(512, 767, 1, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(262, 708, 500, 60)}, 1) == krema::PanelInset{});
    }

    SECTION("Non-floating bottom panel: the reserved depth")
    {
        const QRect probe(512, 723, 1, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(0, 724, 1024, 44)}, 1) == krema::PanelInset{44, 44});
    }

    SECTION("Floating bottom panel: clears its surface, gap included")
    {
        // Reserves 44 px, but its surface is 60 px tall.
        const QRect probe(512, 723, 1, 1);
        REQUIRE(krema::computePanelInset(output, probe, {QRect(262, 708, 500, 60)}, 1) == krema::PanelInset{44, 60});
    }

    SECTION("Panels on other edges and other outputs do not count")
    {
        const QRect probe(512, 723, 1, 1);
        const QList<QRect> panels{
            QRect(0, 0, 60, 768), // left panel spanning the whole height
            QRect(0, 0, 1024, 44), // top panel
            QRect(1024, 708, 1024, 60), // bottom panel of the next output
        };
        REQUIRE(krema::computePanelInset(output, probe, panels, 1) == krema::PanelInset{44, 44});
    }

    SECTION("Top, left and right edges")
    {
        REQUIRE(krema::computePanelInset(output, QRect(512, 44, 1, 1), {QRect(0, 0, 1024, 60)}, 0) == krema::PanelInset{44, 60});
        REQUIRE(krema::computePanelInset(output, QRect(44, 379, 1, 1), {QRect(0, 184, 60, 400)}, 2) == krema::PanelInset{44, 60});
        REQUIRE(krema::computePanelInset(output, QRect(979, 379, 1, 1), {QRect(964, 184, 60, 400)}, 3) == krema::PanelInset{44, 60});
    }

    SECTION("Output not at the origin")
    {
        const QRect second(1024, 0, 1024, 768);
        REQUIRE(krema::computePanelInset(second, QRect(1536, 723, 1, 1), {QRect(1024, 708, 1024, 60)}, 1) == krema::PanelInset{44, 60});
    }

    SECTION("Never past the middle of the screen")
    {
        REQUIRE(krema::computePanelInset(output, QRect(512, 100, 1, 1), {}, 1) == krema::PanelInset{384, 384});
    }
}
