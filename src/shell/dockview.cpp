// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockview.h"

#include "dockvisibilitycontroller.h"
#include "models/taskiconprovider.h"
#include "style/panelinheritstyle.h"

#include <QLoggingCategory>

#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <cmath>

Q_LOGGING_CATEGORY(lcDockView, "krema.shell.dockview")

namespace krema
{

DockView::DockView(std::unique_ptr<DockPlatform> platform, QWindow *parent)
    : QQuickView(parent)
    , m_platform(std::move(platform))
    , m_backgroundStyle(std::make_unique<PanelInheritStyle>())
{
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);

    // Expose this object to QML as "dockView"
    rootContext()->setContextProperty(QStringLiteral("dockView"), this);
}

DockView::~DockView() = default;

void DockView::initialize(TaskManager::TasksModel *tasksModel,
                          TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                          TaskManager::ActivityInfo *activityInfo,
                          DockPlatform::Edge edge,
                          DockPlatform::VisibilityMode visibilityMode)
{
    // Configure the platform layer (LayerShellQt on Wayland)
    m_platform->setupWindow(this);
    m_platform->setEdge(edge);

    // Create visibility controller (manages show/hide logic for all modes)
    m_visibilityController = new DockVisibilityController(m_platform.get(), tasksModel, virtualDesktopInfo, activityInfo, this, this);
    m_visibilityController->setMode(visibilityMode);

    // Register the icon image provider for QML
    engine()->addImageProvider(QStringLiteral("icon"), new TaskIconProvider());

    // Expose visibility controller to QML
    rootContext()->setContextProperty(QStringLiteral("dockVisibility"), m_visibilityController);

    // Apply background effects (blur, contrast)
    applyBackgroundStyle();

    // Load the QML UI
    setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));

    if (status() == QQuickView::Error) {
        const auto errs = errors();
        for (const auto &err : errs) {
            qCCritical(lcDockView) << "QML load error:" << err.toString();
        }
        return;
    }

    // Set initial window size
    updateSize();

    show();
}

QColor DockView::backgroundColor() const
{
    return m_backgroundStyle->backgroundColor();
}

int DockView::iconSize() const
{
    return m_iconSize;
}

void DockView::setIconSize(int size)
{
    size = qBound(24, size, 96);
    if (m_iconSize == size) {
        return;
    }
    m_iconSize = size;
    updateSize();
    Q_EMIT iconSizeChanged();
}

int DockView::iconSpacing() const
{
    return m_iconSpacing;
}

void DockView::setIconSpacing(int spacing)
{
    spacing = qBound(0, spacing, 16);
    if (m_iconSpacing == spacing) {
        return;
    }
    m_iconSpacing = spacing;
    Q_EMIT iconSpacingChanged();
}

qreal DockView::maxZoomFactor() const
{
    return m_maxZoomFactor;
}

void DockView::setMaxZoomFactor(qreal factor)
{
    factor = qBound(1.0, factor, 2.0);
    if (qFuzzyCompare(m_maxZoomFactor, factor)) {
        return;
    }
    m_maxZoomFactor = factor;
    updateSize();
    Q_EMIT maxZoomFactorChanged();
}

int DockView::cornerRadius() const
{
    return m_cornerRadius;
}

void DockView::setCornerRadius(int radius)
{
    radius = qBound(0, radius, 24);
    if (m_cornerRadius == radius) {
        return;
    }
    m_cornerRadius = radius;
    Q_EMIT cornerRadiusChanged();
}

bool DockView::isFloating() const
{
    return m_floating;
}

void DockView::setFloating(bool floating)
{
    if (m_floating == floating) {
        return;
    }
    m_floating = floating;

    // Surface size changes because floatingPadding changed
    updateSize();

    Q_EMIT floatingChanged();
    Q_EMIT floatingPaddingChanged();
}

int DockView::floatingPadding() const
{
    return m_floating ? s_floatingMargin : 0;
}

DockPlatform *DockView::platform() const
{
    return m_platform.get();
}

DockVisibilityController *DockView::visibilityController() const
{
    return m_visibilityController;
}

void DockView::updateSize()
{
    const int dockHeight = m_iconSize + s_padding * 2;
    const int overflowHeight = std::max(zoomOverflowHeight(), s_tooltipReserve);
    const int surfaceHeight = dockHeight + overflowHeight + floatingPadding();
    const int screenWidth = screen() ? screen()->geometry().width() : 0;

    setWidth(screenWidth);
    setHeight(surfaceHeight);

    // Tell layer-shell the desired size.
    // Use screen width explicitly for QWindow; layer-shell will
    // auto-stretch width when left+right anchors are set.
    // Surface height includes extra space above the panel for zoomed icons.
    m_platform->setSize(QSize(screenWidth, surfaceHeight));

    if (m_visibilityController) {
        m_visibilityController->setZoomOverflowHeight(zoomOverflowHeight());
    }
}

int DockView::zoomOverflowHeight() const
{
    return static_cast<int>(std::ceil(m_iconSize * (m_maxZoomFactor - 1.0)));
}

void DockView::applyBackgroundStyle()
{
    m_backgroundStyle->applyToWindow(this);
    Q_EMIT backgroundColorChanged();
}

} // namespace krema
