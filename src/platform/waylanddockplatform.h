// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "dockplatform.h"

#include <QPointer>
#include <QScreen>

#include <memory>

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
    void setScreen(QScreen *screen) override;
    void setEdge(Edge edge) override;
    void setExclusiveZone(int zone) override;
    void setPanelRelative(bool enabled) override;
    void setMargin(int margin) override;
    void setVisibilityMode(VisibilityMode mode) override;
    void setSize(const QSize &size) override;
    void setInputRegion(const QRegion &region) override;
    [[nodiscard]] Edge edge() const override;
    void setKeyboardInteractivity(bool enabled) override;

private:
    void applyAnchors();
    void applyExclusiveZone();
    void showProbe();

    LayerShellQt::Window *m_layerWindow = nullptr;
    QWindow *m_window = nullptr;
    /// Screen the dock surface was pinned to (QWindow::screen() can lag behind).
    QPointer<QScreen> m_screen;
    /// 1 px probe surface on the Bottom layer while panel-relative (see setPanelRelative).
    std::unique_ptr<QWindow> m_probe;
    LayerShellQt::Window *m_probeLayerWindow = nullptr;
    Edge m_edge = Edge::Bottom;
    VisibilityMode m_visibilityMode = VisibilityMode::AlwaysVisible;
    int m_margin = 0;
    int m_exclusiveZone = 0;
    bool m_panelRelative = false;
    /// The probe is mapped, so it can take over the reservation.
    bool m_probeMapped = false;
    /// The probe has committed the reservation; transient exposure loss does not revoke it.
    bool m_probeOwnsReservation = false;
    /// Suppress lifecycle callbacks while deliberately replacing the probe surface.
    bool m_probeResetting = false;
};

} // namespace krema
