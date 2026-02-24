// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "dockplatform.h"

namespace LayerShellQt
{
class Window;
}

namespace krema
{

/**
 * Wayland implementation of DockPlatform using LayerShellQt.
 *
 * Uses the wlr-layer-shell protocol to position the dock at a screen edge,
 * manage exclusive zones, and control input regions.
 */
class WaylandDockPlatform : public DockPlatform
{
public:
    WaylandDockPlatform();

    void setupWindow(QWindow *window) override;
    void setEdge(Edge edge) override;
    void setExclusiveZone(int zone) override;
    void setMargin(int margin) override;
    void setVisibilityMode(VisibilityMode mode) override;
    void setSize(const QSize &size) override;
    void setInputRegion(const QRegion &region) override;
    [[nodiscard]] Edge edge() const override;
    void setKeyboardInteractivity(bool enabled) override;

private:
    void applyAnchors();

    LayerShellQt::Window *m_layerWindow = nullptr;
    QWindow *m_window = nullptr;
    Edge m_edge = Edge::Bottom;
    VisibilityMode m_visibilityMode = VisibilityMode::AlwaysVisible;
    int m_margin = 0;
    int m_exclusiveZone = 0;
};

} // namespace krema
