#include "HyprlandProtocol.hpp"
#include "../../core/debug/KremaConsole.hpp"
#include <LayerShellQt/Window>
#include <QDebug>
#include <QWindow>

namespace Krema
{

extern bool g_debugGeom;

HyprlandProtocol::HyprlandProtocol(QObject *parent)
    : IProtocol(parent)
{
}

HyprlandProtocol::~HyprlandProtocol()
{
}

bool HyprlandProtocol::initialize()
{
    if (m_layerWindow) {
        emit initialized();
        return true;
    }
    return false;
}

void HyprlandProtocol::setWindow(QWindow *window)
{
    m_window = window;
    if (!window) {
        m_layerWindow = nullptr;
        return;
    }

    m_layerWindow = LayerShellQt::Window::get(window);
    if (m_layerWindow) {
        m_layerWindow->setScope(QStringLiteral("krema-dock"));

        connect(window, &QWindow::widthChanged, this, [this, window]() {
            emit surfaceConfigured(window->size());
        });
        connect(window, &QWindow::heightChanged, this, [this, window]() {
            emit surfaceConfigured(window->size());
        });

        emit initialized();
    } else {
        if (g_debugGeom) {
            qWarning() << "[HYPRLAND PROTOCOL] Failed to get LayerShell handle.";
        }
    }
}

void HyprlandProtocol::setLayer(Layer layer)
{
    if (!m_layerWindow)
        return;

    LayerShellQt::Window::Layer targetLayer;
    switch (layer) {
    case Layer::Background:
        targetLayer = LayerShellQt::Window::LayerBackground;
        break;
    case Layer::Bottom:
        targetLayer = LayerShellQt::Window::LayerBottom;
        break;
    case Layer::Top:
        targetLayer = LayerShellQt::Window::LayerTop;
        break;
    case Layer::Overlay:
        targetLayer = LayerShellQt::Window::LayerOverlay;
        break;
    }
    m_layerWindow->setLayer(targetLayer);
}

void HyprlandProtocol::setExclusiveZone(int zone)
{
    if (!m_layerWindow)
        return;
    m_layerWindow->setExclusiveZone(zone);
}

void HyprlandProtocol::setInputRegion(const QRect &rect)
{
    if (m_window) {
        if (g_debugGeom) {
            KREMA_GEOM_LOG(QStringLiteral("[HYPRLAND] Input Region: %1x%2@%3,%4").arg(rect.width()).arg(rect.height()).arg(rect.x()).arg(rect.y()));
        }
        m_window->setMask(QRegion(rect));
    }
}

void HyprlandProtocol::setBlurRegion(const QRect &rect)
{
    Q_UNUSED(rect);
}

void HyprlandProtocol::setMargins(int top, int right, int bottom, int left)
{
    if (!m_layerWindow)
        return;
    m_layerWindow->setMargins(QMargins(left, top, right, bottom));
}

void HyprlandProtocol::setAnchors(Qt::Edges anchors)
{
    if (!m_layerWindow)
        return;

    LayerShellQt::Window::Anchors targetAnchors = LayerShellQt::Window::AnchorNone;
    if (anchors.testFlag(Qt::TopEdge))
        targetAnchors |= LayerShellQt::Window::AnchorTop;
    if (anchors.testFlag(Qt::BottomEdge))
        targetAnchors |= LayerShellQt::Window::AnchorBottom;
    if (anchors.testFlag(Qt::LeftEdge))
        targetAnchors |= LayerShellQt::Window::AnchorLeft;
    if (anchors.testFlag(Qt::RightEdge))
        targetAnchors |= LayerShellQt::Window::AnchorRight;

    m_layerWindow->setAnchors(targetAnchors);
}

void HyprlandProtocol::requestSize(const QSize &size)
{
    if (!m_layerWindow)
        return;
    m_layerWindow->setDesiredSize(size);
}

} // namespace Krema
