// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors
//
// Test-only seam into OutputOrderMonitor: the adoption/screen-change slots are
// private because they are driven by the Wayland client. Unit and KWin tests
// exercise them through this friend class (declared in outputordermonitor.h).

#pragma once

#include "shell/outputordermonitor.h"

namespace krema
{

class OutputOrderMonitorTestAccess
{
public:
    static OutputOrderMonitor *create(QObject *parent = nullptr)
    {
        return new OutputOrderMonitor(parent);
    }

    /// Simulate one completed kde_output_order_v1 list ('done' already applied).
    static void deliverOrder(OutputOrderMonitor *monitor, const QStringList &order)
    {
        monitor->onOrderReceived(order);
    }

    /// Simulate a wl_output add/remove.
    static void screensChanged(OutputOrderMonitor *monitor)
    {
        monitor->onScreenCountChanged();
    }

    /// The order still waiting for every name to map to a QScreen.
    static QStringList pendingOrder(const OutputOrderMonitor *monitor)
    {
        return monitor->m_pendingOrder;
    }
};

} // namespace krema
