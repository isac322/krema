#include "KWinProtocol.hpp"
#include "../../core/debug/KremaConsole.hpp"
#include <LayerShellQt/Window>
#include <QDebug>
#include <QWindow>

namespace Krema
{

extern bool g_debugGeom;

KWinProtocol::KWinProtocol(QObject *parent)
    : IProtocol(parent)
{
}

KWinProtocol::~KWinProtocol()
{
}

bool KWinProtocol::initialize()
{
    if (m_layerWindow) {
        emit initialized();
        return true;
    }
    return false;
}

void KWinProtocol::setWindow(QWindow *window)
{
    m_window = window;
    if (!window) {
        m_layerWindow = nullptr;
        return;
    }

    // Reference Strategy: Get the handle BEFORE the window is created/shown
    m_layerWindow = LayerShellQt::Window::get(window);
    if (m_layerWindow) {
        m_layerWindow->setScope(QStringLiteral("krema-dock"));

        // Track size changes from compositor
        connect(window, &QWindow::widthChanged, this, [this, window]() {
            emit surfaceConfigured(window->size());
        });
        connect(window, &QWindow::heightChanged, this, [this, window]() {
            emit surfaceConfigured(window->size());
        });

        emit initialized();
    } else {
        if (g_debugGeom) {
            qWarning() << "[PROTOCOL] Failed to get LayerShell handle. Already has shell integration?";
        }
    }
}

void KWinProtocol::setLayer(Layer layer)
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

void KWinProtocol::setExclusiveZone(int zone)
{
    if (!m_layerWindow)
        return;
    m_layerWindow->setExclusiveZone(zone);
}

void KWinProtocol::setInputRegion(const QRect &rect)
{
    // Reference Strategy: Avoid frequent mask updates to prevent Wayland crashes.
    if (m_window) {
        if (g_debugGeom) {
            KREMA_GEOM_LOG(QStringLiteral("Input Region Set: X=%1, Y=%2, W=%3, H=%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()));
        }
        m_window->setMask(QRegion(rect));
    }
}

void KWinProtocol::setBlurRegion(const QRect &rect)
{
    Q_UNUSED(rect);
}

void KWinProtocol::setMargins(int top, int right, int bottom, int left)
{
    if (!m_layerWindow)
        return;
    m_layerWindow->setMargins(QMargins(left, top, right, bottom));
}

void KWinProtocol::setAnchors(Qt::Edges anchors)
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

void KWinProtocol::requestSize(const QSize &size)
{
    if (!m_layerWindow)
        return;

    // Reference Strategy: Use setDesiredSize instead of window->resize()
    // A width of 0 means "Stretch to anchors"
    m_layerWindow->setDesiredSize(size);
}

} // namespace Krema
