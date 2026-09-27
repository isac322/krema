// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Pure resolution + pending-order logic of OutputOrderMonitor (issue #18):
// the Plasma primary is the FIRST ordered output that maps to a live QScreen,
// so a list arriving before the matching wl_output still resolves correctly
// and unknown names are skipped. An order is adopted only once every name
// maps to a QScreen; until then it stays pending.
//
// These tests run headless (no QGuiApplication): with no screens the adoption
// gate can only be exercised on the pending side. The adopt-once-mapped path
// is covered by the real two-output cases in
// tests/kwin/test_primary_output.cpp.

#include "outputordermonitortestaccess.h"
#include "shell/outputordermonitor.h"

#include <catch2/catch_test_macros.hpp>

#include <QObject>

using krema::OutputOrderMonitor;
using krema::OutputOrderMonitorTestAccess;

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

TEST_CASE("OutputOrderMonitor keeps an order pending until every name maps to a QScreen", "[output-order]")
{
    // Headless: QGuiApplication::screens() is empty, so no name ever maps —
    // the order must remain pending and never be adopted.
    QObject parent;
    auto *monitor = OutputOrderMonitorTestAccess::create(&parent);

    int readyCount = 0;
    QObject::connect(monitor, &OutputOrderMonitor::orderReadyChanged, &parent, [&readyCount] {
        ++readyCount;
    });

    SECTION("order is held pending when no output name maps")
    {
        OutputOrderMonitorTestAccess::deliverOrder(monitor, {QStringLiteral("Ghost-0"), QStringLiteral("Ghost-1")});
        CHECK(OutputOrderMonitorTestAccess::pendingOrder(monitor) == QStringList{QStringLiteral("Ghost-0"), QStringLiteral("Ghost-1")});
        // Nothing adopted: the resolved order stays empty.
        CHECK(monitor->outputOrder().isEmpty());
    }

    SECTION("a screen change retries adoption of the pending order")
    {
        OutputOrderMonitorTestAccess::deliverOrder(monitor, {QStringLiteral("Ghost-0")});
        CHECK(OutputOrderMonitorTestAccess::pendingOrder(monitor) == QStringList{QStringLiteral("Ghost-0")});
        // Still nothing mapped, so re-running adoption keeps it pending.
        OutputOrderMonitorTestAccess::screensChanged(monitor);
        CHECK(OutputOrderMonitorTestAccess::pendingOrder(monitor) == QStringList{QStringLiteral("Ghost-0")});
        CHECK(monitor->outputOrder().isEmpty());
    }

    SECTION("orderReadyChanged does not fire while the order stays pending")
    {
        // orderReadyChanged is an adoption edge: unmapped orders never adopt
        // headless, so no emission occurs. (The single-firing across adopted
        // orders is covered by the real two-output KWin case.)
        OutputOrderMonitorTestAccess::deliverOrder(monitor, {QStringLiteral("Ghost-0")});
        OutputOrderMonitorTestAccess::deliverOrder(monitor, {QStringLiteral("Ghost-0"), QStringLiteral("Ghost-1")});
        CHECK(readyCount == 0);
        // orderReady() still reports true only through the no-protocol
        // fallback, not because an order was adopted.
        CHECK(monitor->outputOrder().isEmpty());
    }

    SECTION("with no protocol client the fallback is ready immediately")
    {
        CHECK(monitor->orderReady());
        CHECK_FALSE(monitor->protocolActive());
    }
}
