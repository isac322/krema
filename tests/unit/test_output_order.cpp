// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Pure resolution logic of OutputOrderMonitor (issue #18): the Plasma
// primary is the FIRST ordered output that maps to a live QScreen, so a
// list arriving before the matching wl_output still resolves correctly and
// unknown names are skipped.

#include "shell/outputordermonitor.h"

#include <catch2/catch_test_macros.hpp>

using krema::OutputOrderMonitor;

TEST_CASE("OutputOrderMonitor::firstOrderedMatch resolves the first known output", "[output-order]")
{
    const QStringList screens = {QStringLiteral("Virtual-0"), QStringLiteral("Virtual-1")};

    SECTION("empty order yields -1 (fallback to QGuiApplication::primaryScreen)")
    {
        CHECK(OutputOrderMonitor::firstOrderedMatch({}, screens) == -1);
    }

    SECTION("primary-first order resolves to index 0")
    {
        CHECK(OutputOrderMonitor::firstOrderedMatch({QStringLiteral("Virtual-0"), QStringLiteral("Virtual-1")}, screens) == 0);
        CHECK(OutputOrderMonitor::firstOrderedMatch({QStringLiteral("Virtual-1"), QStringLiteral("Virtual-0")}, screens) == 0);
    }

    SECTION("unknown names are skipped until a known output is found")
    {
        // Order list may contain outputs Qt has not announced yet.
        CHECK(OutputOrderMonitor::firstOrderedMatch({QStringLiteral("HDMI-A-9"), QStringLiteral("Virtual-1")}, screens) == 1);
        CHECK(OutputOrderMonitor::firstOrderedMatch({QStringLiteral("HDMI-A-9"), QStringLiteral("DP-2")}, screens) == -1);
    }

    SECTION("empty screen list yields -1")
    {
        CHECK(OutputOrderMonitor::firstOrderedMatch({QStringLiteral("Virtual-0")}, {}) == -1);
    }
}
