// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"

#include <QRegion>
#include <QSize>

namespace krema::testing
{

class MockDockPlatform : public DockPlatform
{
public:
    void setupWindow(QWindow *) override
    {
    }
    void setEdge(Edge edge) override
    {
        m_edge = edge;
    }
    void setExclusiveZone(int zone) override
    {
        m_exclusiveZone = zone;
    }
    void setMargin(int margin) override
    {
        m_margin = margin;
    }
    void setVisibilityMode(VisibilityMode mode) override
    {
        m_visibilityMode = mode;
    }
    void setSize(const QSize &size) override
    {
        m_size = size;
    }
    void setInputRegion(const QRegion &region) override
    {
        m_inputRegion = region;
    }
    void setKeyboardInteractivity(bool enabled) override
    {
        m_keyboardInteractive = enabled;
    }
    [[nodiscard]] Edge edge() const override
    {
        return m_edge;
    }

    Edge m_edge = Edge::Bottom;
    int m_exclusiveZone = 0;
    int m_margin = 0;
    VisibilityMode m_visibilityMode = VisibilityMode::AlwaysVisible;
    QSize m_size;
    QRegion m_inputRegion;
    bool m_keyboardInteractive = false;
};

} // namespace krema::testing
