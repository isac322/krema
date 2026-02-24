// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "waylanddockplatform.h"

#include <LayerShellQt/Window>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcWayland, "krema.platform.wayland")

namespace krema
{

WaylandDockPlatform::WaylandDockPlatform() = default;

void WaylandDockPlatform::setupWindow(QWindow *window)
{
    m_window = window;
    m_layerWindow = LayerShellQt::Window::get(window);

    if (!m_layerWindow) {
        qCCritical(lcWayland) << "LayerShellQt::Window::get() returned null!";
        return;
    }

    m_layerWindow->setLayer(LayerShellQt::Window::LayerTop);
    m_layerWindow->setScope(QStringLiteral("krema-dock"));
    m_layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_layerWindow->setCloseOnDismissed(false);

    applyAnchors();
}

void WaylandDockPlatform::setEdge(Edge edge)
{
    if (m_edge == edge) {
        return;
    }
    m_edge = edge;
    if (m_layerWindow) {
        applyAnchors();
    }
}

void WaylandDockPlatform::setExclusiveZone(int zone)
{
    m_exclusiveZone = zone;
    if (m_layerWindow) {
        m_layerWindow->setExclusiveZone(zone);
    }
}

void WaylandDockPlatform::setMargin(int margin)
{
    m_margin = margin;
    if (m_layerWindow) {
        QMargins margins;
        switch (m_edge) {
        case Edge::Bottom:
            margins.setBottom(margin);
            break;
        case Edge::Top:
            margins.setTop(margin);
            break;
        case Edge::Left:
            margins.setLeft(margin);
            break;
        case Edge::Right:
            margins.setRight(margin);
            break;
        }
        m_layerWindow->setMargins(margins);
    }
}

void WaylandDockPlatform::setVisibilityMode(VisibilityMode mode)
{
    if (!m_layerWindow) {
        return;
    }

    m_visibilityMode = mode;

    switch (mode) {
    case VisibilityMode::AlwaysVisible:
        // Positive exclusive zone reserves space for the dock
        m_layerWindow->setExclusiveZone(m_exclusiveZone > 0 ? m_exclusiveZone : 0);
        break;
    case VisibilityMode::AutoHide:
    case VisibilityMode::DodgeWindows:
        // No exclusive zone — windows can use the full screen
        m_layerWindow->setExclusiveZone(0);
        break;
    }
}

void WaylandDockPlatform::setSize(const QSize &size)
{
    if (m_layerWindow) {
        m_layerWindow->setDesiredSize(size);
    }
}

void WaylandDockPlatform::setInputRegion(const QRegion &region)
{
    if (m_window) {
        m_window->setMask(region);
    }
}

DockPlatform::Edge WaylandDockPlatform::edge() const
{
    return m_edge;
}

void WaylandDockPlatform::setKeyboardInteractivity(bool enabled)
{
    if (!m_layerWindow) {
        return;
    }
    m_layerWindow->setKeyboardInteractivity(enabled ? LayerShellQt::Window::KeyboardInteractivityExclusive : LayerShellQt::Window::KeyboardInteractivityNone);
}

void WaylandDockPlatform::applyAnchors()
{
    if (!m_layerWindow) {
        return;
    }

    LayerShellQt::Window::Anchors anchors;

    switch (m_edge) {
    case Edge::Bottom:
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        break;
    case Edge::Top:
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        break;
    case Edge::Left:
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        break;
    case Edge::Right:
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        break;
    }

    m_layerWindow->setAnchors(anchors);

    // Reapply margin after anchor change
    setMargin(m_margin);
}

} // namespace krema
