// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "hyprlanddockplatform.h"

#include <LayerShellQt/Window>
#include <QLoggingCategory>
#include <QMargins>

Q_LOGGING_CATEGORY(lcHyprland, "krema.platform.hyprland")

namespace krema
{

HyprlandDockPlatform::HyprlandDockPlatform() = default;

void HyprlandDockPlatform::setupWindow(QWindow *window)
{
    m_window = window;
    m_layerWindow = LayerShellQt::Window::get(window);

    if (!m_layerWindow) {
        qCCritical(lcHyprland) << "LayerShellQt::Window::get() returned null!";
        return;
    }

    m_layerWindow->setLayer(LayerShellQt::Window::LayerTop);
    m_layerWindow->setScope(QStringLiteral("krema-dock"));
    m_layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_layerWindow->setCloseOnDismissed(false);

    applyAnchors();
}

void HyprlandDockPlatform::setEdge(Edge edge)
{
    if (m_edge == edge) {
        return;
    }
    m_edge = edge;
    if (m_layerWindow) {
        applyAnchors();
    }
}

void HyprlandDockPlatform::setExclusiveZone(int zone)
{
    m_exclusiveZone = zone;
    if (m_layerWindow) {
        m_layerWindow->setExclusiveZone(zone);
    }
}

void HyprlandDockPlatform::setMargin(int margin)
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

void HyprlandDockPlatform::setVisibilityMode(VisibilityMode mode)
{
    if (!m_layerWindow) {
        return;
    }

    m_visibilityMode = mode;

    switch (mode) {
    case VisibilityMode::AlwaysVisible:
        m_layerWindow->setExclusiveZone(m_exclusiveZone > 0 ? m_exclusiveZone : 0);
        break;
    case VisibilityMode::AutoHide:
    case VisibilityMode::DodgeWindows:
        // Respect other surfaces' exclusive zones (like shell panels)
        // by setting to 0, which anchors to the usable area instead of absolute edge.
        m_layerWindow->setExclusiveZone(0);
        break;
    }
}

void HyprlandDockPlatform::setSize(const QSize &size)
{
    if (m_layerWindow) {
        m_layerWindow->setDesiredSize(size);
    }
}

void HyprlandDockPlatform::setInputRegion(const QRegion &region)
{
    if (m_window) {
        m_window->setMask(region);
    }
}

void HyprlandDockPlatform::setBlurRegion(const QRegion &region)
{
    // KWin-specific KWindowEffects are skipped here.
    // On Hyprland, blur is typically managed via window rules:
    // 'layerrule = blur, krema-dock'
    Q_UNUSED(region);
}

DockPlatform::Edge HyprlandDockPlatform::edge() const
{
    return m_edge;
}

void HyprlandDockPlatform::setKeyboardInteractivity(bool enabled)
{
    if (!m_layerWindow) {
        return;
    }
    m_layerWindow->setKeyboardInteractivity(enabled ? LayerShellQt::Window::KeyboardInteractivityExclusive : LayerShellQt::Window::KeyboardInteractivityNone);
}

void HyprlandDockPlatform::applyAnchors()
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
    setMargin(m_margin);
}

} // namespace krema
