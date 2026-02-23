// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockview.h"

#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "models/taskiconprovider.h"
#include "style/panelinheritstyle.h"
#include "utils/surfacegeometry.h"

#include <QLoggingCategory>

#include <QQmlEngine>
#include <QScreen>
#include <QtQml>

Q_LOGGING_CATEGORY(lcDockView, "krema.shell.dockview")

namespace krema
{

DockView::DockView(std::unique_ptr<DockPlatform> platform, KremaSettings *settings, QWindow *parent)
    : QQuickView(parent)
    , m_platform(std::move(platform))
    , m_backgroundStyle(std::make_unique<PanelInheritStyle>())
    , m_settings(settings)
{
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
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

    qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockVisibility", m_visibilityController);

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
    QColor color = m_backgroundStyle->backgroundColor();
    color.setAlphaF(m_settings->backgroundOpacity());
    return color;
}

int DockView::floatingPadding() const
{
    return m_settings->floating() ? s_floatingMargin : 0;
}

int DockView::panelBarHeight() const
{
    return krema::panelBarHeight(m_settings->iconSize(), s_padding, floatingPadding());
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
    const int h = krema::surfaceHeight(m_settings->iconSize(), s_padding, m_settings->maxZoomFactor(), s_tooltipReserve, floatingPadding());
    const int screenWidth = screen() ? screen()->geometry().width() : 0;

    setWidth(screenWidth);
    setHeight(h);

    // Tell layer-shell the desired size.
    m_platform->setSize(QSize(screenWidth, h));

    if (m_visibilityController) {
        m_visibilityController->setZoomOverflowHeight(zoomOverflowHeight());
    }
}

int DockView::zoomOverflowHeight() const
{
    return krema::zoomOverflowHeight(m_settings->iconSize(), m_settings->maxZoomFactor());
}

void DockView::applyBackgroundStyle()
{
    m_backgroundStyle->applyToWindow(this);
    Q_EMIT backgroundColorChanged();
}

} // namespace krema
