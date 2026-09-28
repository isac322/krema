// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "edgetrigger.h"

#include "platform/dockplatformfactory.h"
#include "utils/inputregion.h"

#include <QPainter>
#include <QScreen>
#include <QSurfaceFormat>

namespace krema
{

EdgeTrigger::EdgeTrigger(QScreen *screen, DockPlatform::Edge edge)
    : m_platform(DockPlatformFactory::create())
{
    QSurfaceFormat format = requestedFormat();
    format.setAlphaBufferSize(8);
    setFormat(format);

    // Same pinning order as the dock (MultiDockManager::createShellForScreen,
    // DockView::initialize): the QWindow screen and a position on it before the
    // platform window exists, then the layer-surface output.
    setScreen(screen);
    if (screen) {
        setPosition(screen->geometry().topLeft());
    }

    if (m_platform) {
        m_platform->setupWindow(this);
        m_platform->setEdge(edge);
        m_platform->setScreen(screen);
        // Like an auto-hide dock: sit at the real screen edge, reserve nothing.
        m_platform->setVisibilityMode(DockPlatform::VisibilityMode::AutoHide);
    }
    applySize();
}

EdgeTrigger::~EdgeTrigger() = default;

void EdgeTrigger::setEdge(DockPlatform::Edge edge)
{
    if (!m_platform || m_platform->edge() == edge) {
        return;
    }
    m_platform->setEdge(edge);
    applySize();
}

bool EdgeTrigger::isHovered() const
{
    return m_hovered;
}

void EdgeTrigger::applySize()
{
    const QRect screenGeo = screen() ? screen()->geometry() : QRect();
    const bool vertical = m_platform && (m_platform->edge() == DockPlatform::Edge::Left || m_platform->edge() == DockPlatform::Edge::Right);
    // Layer-shell: 0 on the double-anchored axis lets the compositor stretch
    // the strip along the whole edge. The QWindow size is set last: on the
    // KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE path setSize() resizes the
    // window itself, and a 0-wide window never gets a buffer.
    if (vertical) {
        if (m_platform) {
            m_platform->setSize(QSize(kEdgeTriggerThickness, 0));
        }
        resize(kEdgeTriggerThickness, screenGeo.height());
    } else {
        if (m_platform) {
            m_platform->setSize(QSize(0, kEdgeTriggerThickness));
        }
        resize(screenGeo.width(), kEdgeTriggerThickness);
    }
}

void EdgeTrigger::setHovered(bool hovered)
{
    if (m_hovered == hovered) {
        return;
    }
    m_hovered = hovered;
    Q_EMIT hoveredChanged(hovered);
}

bool EdgeTrigger::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::Enter:
        setHovered(true);
        break;
    case QEvent::Leave:
        setHovered(false);
        break;
    case QEvent::Hide:
        // An unmapped surface gets no leave event.
        setHovered(false);
        break;
    default:
        break;
    }
    return QRasterWindow::event(event);
}

void EdgeTrigger::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(QRect(QPoint(), size()), Qt::transparent);
}

} // namespace krema
