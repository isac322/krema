// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockview.h"
#include "config/screensettings.h"
#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "models/taskiconprovider.h"
#include "utils/surfacegeometry.h"
#include <KIconLoader>
#include <KLocalizedQmlContext>
#include <QLoggingCategory>
#include <QPainterPath>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <QtQml>

#include "utils/debugmanager.h"

namespace krema
{

DockView::DockView(std::unique_ptr<DockPlatform> platform, KremaSettings *settings, QWindow *parent)
    : QQuickView(parent)
    , m_platform(std::move(platform))
    , m_settings(settings)
{
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
}

DockView::~DockView() = default;

void DockView::initialize(QAbstractItemModel *tasksModel,
                          TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                          TaskManager::ActivityInfo *activityInfo,
                          DockPlatform::Edge edge,
                          DockPlatform::VisibilityMode visibilityMode)
{
    m_platform->setupWindow(this);
    m_platform->setEdge(edge);
    m_edge = edge;

    m_visibilityController = new DockVisibilityController(m_platform.get(), tasksModel, virtualDesktopInfo, activityInfo, this, this);
    m_visibilityController->setMode(visibilityMode);

    m_iconProvider = new TaskIconProvider(m_settings->iconNormalization());
    m_iconProvider->setIndicatorOffset(m_settings->indicatorOffset());
    engine()->addImageProvider(QStringLiteral("taskicon"), m_iconProvider);

    connect(KIconLoader::global(), &KIconLoader::iconChanged, this, [this]() {
        m_iconProvider->clearCache();
        bumpIconCacheVersion();
    });

    KLocalization::setupLocalizedContext(engine());
    engine()->rootContext()->setContextProperty(QStringLiteral("DockVisibility"), m_visibilityController);

    connect(m_visibilityController, &DockVisibilityController::liveEditModeChanged, this, [this]() {
        updateSize();
        applyBackgroundStyle();
    });

    connect(m_visibilityController, &DockVisibilityController::dockVisibleChanged, this, &DockView::updateSize);
    connect(m_visibilityController, &DockVisibilityController::dockVisibleChanged, this, &DockView::applyBackgroundStyle);

    applyBackgroundStyle();
    setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));

    if (status() == QQuickView::Error) {
        for (const auto &err : errors())
            qCCritical(lcShell) << "QML error:" << err.toString();
        return;
    }

    updateSize();
    connect(this, &QWindow::screenChanged, this, &DockView::handleScreenChanged);
    if (screen())
        m_screenGeometryConnection = connect(screen(), &QScreen::geometryChanged, this, &DockView::handleScreenGeometryChanged);

    show();
}

QColor DockView::backgroundColor() const
{
    auto type = static_cast<BackgroundStyleType>(m_settings->backgroundStyle());
    return computeBackgroundColor(type, m_settings->tintColor(), m_settings->backgroundOpacity(), m_settings->useAccentColor(), m_settings->useSystemColor());
}

void DockView::updateSize()
{
    const int iconSize = m_screenSettings ? m_screenSettings->iconSize() : m_settings->iconSize();
    const double maxZoom = m_screenSettings ? m_screenSettings->maxZoomFactor() : m_settings->maxZoomFactor();

    const int baseIconSize = 48;
    const int userH = iconSize + ((m_screenSettings ? m_screenSettings->panelHeight() : m_settings->panelHeight()) - baseIconSize);

    const int maxZoomExt = static_cast<int>(iconSize * (maxZoom - 1.0)) + 10;

    const QRect screenGeo = screen() ? screen()->geometry() : QRect();

    int surfaceSize;
    if (m_visibilityController && m_visibilityController->liveEditMode()) {
        int baseDim = isVertical() ? screenGeo.width() : screenGeo.height();
        surfaceSize = (baseDim / 4) - 90 + 800;
    } else {
        // We use 350px to ensure long tooltips are never clipped by the Wayland surface boundaries.
        surfaceSize = userH + maxZoomExt + 350;
    }

    surfaceSize += floatingPadding();

    if (isVertical()) {
        setWidth(surfaceSize);
        setHeight(screenGeo.height());
        m_platform->setSize(QSize(surfaceSize, 0));
    } else {
        setWidth(screenGeo.width());
        setHeight(surfaceSize);
        m_platform->setSize(QSize(0, surfaceSize));
    }

    if (m_visibilityController) {
        m_visibilityController->setZoomOverflowHeight(zoomOverflowHeight());
        m_visibilityController->updateRegionGeometry();
    }
}

void DockView::applyBackgroundStyle()
{
    auto type = static_cast<BackgroundStyleType>(m_settings->backgroundStyle());
    QRegion visualRegion;

    // ... inside DockView::applyBackgroundStyle() ...
    if (m_visibilityController) {
        // We ALWAYS want to apply the background style to the dock panel,
        // even when the blueprint grid is visible in Edit Mode.
        const QRect panel = m_visibilityController->panelRect();
        if (panel.isValid()) {
            visualRegion += panel;
        }
    }

    // Guard: Never pass an empty region to blur-using styles.
    // An empty QRegion tells KWin to blur the ENTIRE window surface,
    // which causes the massive blurry block bug on startup (Bug #40).
    if (visualRegion.isEmpty() && styleUsesBlur(type)) {
        return;
    }

    applyBackgroundToWindow(this, type, visualRegion);
    Q_EMIT backgroundColorChanged();
    Q_EMIT backgroundStyleTypeChanged();
}

int DockView::backgroundStyleType() const
{
    return m_settings->backgroundStyle();
}
int DockView::floatingPadding() const
{
    return m_settings->floating() ? s_floatingMargin : 0;
}
int DockView::iconCacheVersion() const
{
    return m_iconCacheVersion;
}
int DockView::edge() const
{
    return static_cast<int>(m_edge);
}
bool DockView::isVertical() const
{
    return m_edge == DockPlatform::Edge::Left || m_edge == DockPlatform::Edge::Right;
}

QObject *DockView::screenSettings() const
{
    return m_screenSettings;
}

void DockView::setEdge(DockPlatform::Edge edge)
{
    if (m_edge == edge)
        return;
    m_edge = edge;
    updateSize();
    Q_EMIT edgeChanged();
}

TaskIconProvider *DockView::iconProvider() const
{
    return m_iconProvider;
}

void DockView::bumpIconCacheVersion()
{
    ++m_iconCacheVersion;
    Q_EMIT iconCacheVersionChanged();
}

void DockView::setScreenSettings(ScreenSettings *screenSettings)
{
    m_screenSettings = screenSettings;
}

int DockView::panelBarHeight() const
{
    const int iconSize = m_screenSettings ? m_screenSettings->iconSize() : m_settings->iconSize();
    return krema::panelBarHeight(iconSize, s_padding, floatingPadding());
}

bool DockView::isStyleAvailable(int styleType) const
{
    return krema::isStyleAvailable(static_cast<BackgroundStyleType>(styleType));
}

DockPlatform *DockView::platform() const
{
    return m_platform.get();
}
DockVisibilityController *DockView::visibilityController() const
{
    return m_visibilityController;
}

int DockView::zoomOverflowHeight() const
{
    const int iconSize = m_screenSettings ? m_screenSettings->iconSize() : m_settings->iconSize();
    const double maxZoom = m_screenSettings ? m_screenSettings->maxZoomFactor() : m_settings->maxZoomFactor();
    return krema::zoomOverflowHeight(iconSize, maxZoom);
}

void DockView::handleScreenChanged(QScreen *newScreen)
{
    disconnect(m_screenGeometryConnection);
    if (!newScreen)
        return;
    m_screenGeometryConnection = connect(newScreen, &QScreen::geometryChanged, this, &DockView::handleScreenGeometryChanged);
    if (newScreen->geometry().width() <= 0)
        return;
    hide();
    updateSize();
    applyBackgroundStyle();
    show();
    if (m_visibilityController)
        m_visibilityController->requestEvaluate();
}

void DockView::handleScreenGeometryChanged()
{
    if (!screen() || screen()->geometry().width() <= 0)
        return;
    updateSize();
    applyBackgroundStyle();
    if (m_visibilityController)
        m_visibilityController->requestEvaluate();
}

} // namespace krema
