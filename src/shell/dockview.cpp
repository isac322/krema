// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockview.h"

#include "config/screensettings.h"
#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "models/taskiconprovider.h"
#include "utils/surfacegeometry.h"
#include "utils/zoomcalculator.h"

#include <QDBusConnection>
#include <QLoggingCategory>
#include <QPainterPath>

#include <KIconLoader>
#include <KLocalizedQmlContext>

#include <QQmlEngine>
#include <QScreen>
#include <QtQml>

Q_LOGGING_CATEGORY(lcDockView, "krema.shell.dockview")

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

void DockView::initialize(TaskManager::TasksModel *tasksModel,
                          TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                          TaskManager::ActivityInfo *activityInfo,
                          DockPlatform::Edge edge,
                          DockPlatform::VisibilityMode visibilityMode)
{
    // Capture the screen MultiDockManager assigned before setupWindow():
    // LayerShellQt < 6.6 creates the platform window in Window::get(), and
    // QtWayland then re-derives screen() from the still-empty geometry.
    QScreen *targetScreen = screen();

    // Configure the platform layer (LayerShellQt on Wayland)
    m_platform->setupWindow(this);
    m_platform->setEdge(edge);
    // Pin the layer surface to this dock's screen before show():
    // QWindow::setScreen alone does not bind the layer surface on QtWayland,
    // so the platform pins it (LayerShellQt::Window screen, or the Qt screen
    // and position on LayerShellQt < 6.6). Layer-shell surfaces bind their
    // wl_output when the surface is created on show().
    m_platform->setScreen(targetScreen);

    // Store edge for QML access (must be set before QML loading)
    m_edge = edge;

    // Create visibility controller (manages show/hide logic for all modes)
    m_visibilityController = new DockVisibilityController(m_platform.get(), tasksModel, virtualDesktopInfo, activityInfo, this, this);
    m_visibilityController->setMode(visibilityMode);

    // Register the icon image provider for QML
    m_iconProvider = new TaskIconProvider(m_settings->iconNormalization());
    m_iconProvider->setIconScale(m_settings->iconScale());
    engine()->addImageProvider(QStringLiteral("icon"), m_iconProvider);

    // Invalidate icon normalization cache when icon theme changes
    connect(KIconLoader::global(), &KIconLoader::iconChanged, this, [this]() {
        m_iconProvider->clearCache();
        bumpIconCacheVersion();
    });

    // Enable i18n() in QML (required for Accessible.name/description strings)
    KLocalization::setupLocalizedContext(engine());

    // Per-engine context property (supports multiple DockView instances for M8 multi-monitor)
    engine()->rootContext()->setContextProperty(QStringLiteral("DockVisibility"), m_visibilityController);

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

    // React to screen changes (e.g. DPMS off/on replaces QScreen objects)
    connect(this, &QWindow::screenChanged, this, &DockView::handleScreenChanged);
    if (screen()) {
        m_screenGeometryConnection = connect(screen(), &QScreen::geometryChanged, this, &DockView::handleScreenGeometryChanged);
    }

    // React to screen lock/unlock (QScreen object stays the same, so screenChanged doesn't fire)
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.ScreenSaver"),
                                          QStringLiteral("/ScreenSaver"),
                                          QStringLiteral("org.freedesktop.ScreenSaver"),
                                          QStringLiteral("ActiveChanged"),
                                          this,
                                          SLOT(handleScreenLockChanged(bool)));

    show();
}

QColor DockView::backgroundColor() const
{
    auto type = static_cast<BackgroundStyleType>(m_settings->backgroundStyle());
    qreal opacity = m_settings->backgroundOpacity();

    return computeBackgroundColor(type, m_settings->tintColor(), opacity, m_settings->useAccentColor(), m_settings->useSystemColor());
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

int DockView::sideTooltipReserve() const
{
    return m_sideTooltipReserve;
}

void DockView::setSideTooltipReserve(int reserve)
{
    if (m_sideTooltipReserve == reserve) {
        return;
    }
    m_sideTooltipReserve = reserve;
    if (isVertical()) {
        updateSize();
    }
    Q_EMIT sideTooltipReserveChanged();
}

void DockView::setEdge(DockPlatform::Edge edge)
{
    if (m_edge == edge) {
        return;
    }
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

DockPlatform *DockView::platform() const
{
    return m_platform.get();
}

DockVisibilityController *DockView::visibilityController() const
{
    return m_visibilityController;
}

QVariantMap DockView::zoomLayout(int count,
                                 qreal restStart,
                                 qreal iconSize,
                                 qreal spacing,
                                 int boundary,
                                 qreal boundaryGap,
                                 qreal restBackgroundStart,
                                 qreal restBackgroundEnd,
                                 qreal maxZoomFactor,
                                 int style,
                                 bool active,
                                 qreal cursor,
                                 qreal minEdge,
                                 qreal maxEdge) const
{
    // Unknown values fall back to the default, Parabolic.
    const ZoomStyle zoomStyle = style == static_cast<int>(ZoomStyle::InPlace) ? ZoomStyle::InPlace : ZoomStyle::Parabolic;
    const DockZoomLayout layout = computeDockZoom(count,
                                                  restStart,
                                                  iconSize,
                                                  spacing,
                                                  boundary,
                                                  boundaryGap,
                                                  restBackgroundStart,
                                                  restBackgroundEnd,
                                                  maxZoomFactor,
                                                  zoomStyle,
                                                  active,
                                                  cursor,
                                                  minEdge,
                                                  maxEdge);

    QVariantList scales;
    scales.reserve(static_cast<qsizetype>(layout.scales.size()));
    for (double scale : layout.scales) {
        scales.append(scale);
    }
    QVariantList offsets;
    offsets.reserve(static_cast<qsizetype>(layout.offsets.size()));
    for (double offset : layout.offsets) {
        offsets.append(offset);
    }

    return {
        {QStringLiteral("scales"), scales},
        {QStringLiteral("offsets"), offsets},
        {QStringLiteral("leadingGrowth"), layout.leadingGrowth},
        {QStringLiteral("trailingGrowth"), layout.trailingGrowth},
    };
}

void DockView::updateSize()
{
    const int iconSize = m_screenSettings ? m_screenSettings->iconSize() : m_settings->iconSize();
    const double maxZoom = m_screenSettings ? m_screenSettings->maxZoomFactor() : m_settings->maxZoomFactor();
    // Horizontal docks open the tooltip above/below the panel (fits s_tooltipReserve);
    // vertical docks open it beside the panel, so the reserve must fit its full width.
    const int tooltipReserve = isVertical() ? std::max(s_tooltipReserve, m_sideTooltipReserve) : s_tooltipReserve;
    const int h = krema::surfaceHeight(iconSize, s_padding, maxZoom, tooltipReserve, floatingPadding());
    const QRect screenGeo = screen() ? screen()->geometry() : QRect();

    if (isVertical()) {
        // Vertical dock: h becomes width, screen height becomes height
        setWidth(h);
        setHeight(screenGeo.height());
        // Layer-shell: 0 on double-anchored axis (Top+Bottom) lets compositor decide
        m_platform->setSize(QSize(h, 0));
    } else {
        // Horizontal dock: screen width becomes width, h becomes height
        setWidth(screenGeo.width());
        setHeight(h);
        // Layer-shell: 0 on double-anchored axis (Left+Right) lets compositor decide
        m_platform->setSize(QSize(0, h));
    }

    // Reserve the panel bar only when screen-space reservation is enabled.
    m_platform->setExclusiveZone(m_settings->reserveScreenSpace() ? panelBarHeight() : 0);

    if (m_visibilityController) {
        m_visibilityController->setZoomOverflowHeight(zoomOverflowHeight());
    }
}

int DockView::zoomOverflowHeight() const
{
    const int iconSize = m_screenSettings ? m_screenSettings->iconSize() : m_settings->iconSize();
    const double maxZoom = m_screenSettings ? m_screenSettings->maxZoomFactor() : m_settings->maxZoomFactor();
    return krema::zoomOverflowHeight(iconSize, maxZoom);
}

void DockView::handleScreenChanged(QScreen *newScreen)
{
    // Disconnect from old screen's geometry signal
    disconnect(m_screenGeometryConnection);

    if (!newScreen) {
        qCDebug(lcDockView) << "Screen changed to nullptr (DPMS off / placeholder)";
        return;
    }

    // Connect to new screen's geometry signal
    m_screenGeometryConnection = connect(newScreen, &QScreen::geometryChanged, this, &DockView::handleScreenGeometryChanged);

    const QRect geo = newScreen->geometry();
    qCDebug(lcDockView) << "Screen changed:" << newScreen->name() << "geometry=" << geo;

    // Skip recovery if geometry is invalid (placeholder screen during DPMS off)
    if (geo.width() <= 0 || geo.height() <= 0) {
        return;
    }

    // Not created yet: this is the screen assignment made while the platform
    // window is being created, and the surface will be created on newScreen.
    // hide()+show() here would re-enter QWindow::create() and leak a second
    // platform window whose QWindow pointer dangles after this view is destroyed.
    if (!handle()) {
        return;
    }

    // Force surface re-creation: the compositor destroys the layer-shell surface
    // when the associated output is removed. hide()+show() creates a fresh surface.
    hide();
    updateSize();
    applyBackgroundStyle();
    show();
    if (m_visibilityController) {
        m_visibilityController->requestEvaluate();
    }
}

void DockView::handleScreenGeometryChanged()
{
    if (!screen()) {
        return;
    }

    const QRect geo = screen()->geometry();
    qCDebug(lcDockView) << "Screen geometry changed:" << geo;

    if (geo.width() <= 0 || geo.height() <= 0) {
        return;
    }

    updateSize();
    applyBackgroundStyle();
    if (m_visibilityController) {
        m_visibilityController->requestEvaluate();
    }
}

void DockView::handleScreenLockChanged(bool active)
{
    if (active) {
        return;
    }

    qCDebug(lcDockView) << "Screen unlocked — recovering dock";
    // Surface may have been destroyed during DPMS-off while locked.
    // hide()+show() ensures a fresh surface on the current screen.
    hide();
    updateSize();
    applyBackgroundStyle();
    show();
    if (m_visibilityController) {
        m_visibilityController->requestEvaluate();
    }
}

void DockView::applyBackgroundStyle()
{
    auto type = static_cast<BackgroundStyleType>(m_settings->backgroundStyle());
    qreal opacity = m_settings->backgroundOpacity();

    // When opacity is 0, compositor effects (blur/contrast) must also be disabled.
    // Otherwise KWindowEffects creates a visible frosted layer even with transparent QML color.
    if (styleUsesBlur(type) && qFuzzyIsNull(opacity)) {
        removeBackgroundFromWindow(this);
        Q_EMIT backgroundColorChanged();
        Q_EMIT backgroundStyleTypeChanged();
        return;
    }

    // Restrict blur/contrast to the panel rectangle only.
    // Without a region, KWindowEffects applies effects to the entire layer-shell surface,
    // which covers the full screen width and causes a colored bar across the bottom.
    // Use a rounded rectangle to match the panel's corner radius.
    QRegion region;
    if (m_visibilityController) {
        const QRect panel = m_visibilityController->panelRect();
        if (panel.width() > 0) {
            const int radius = m_settings->cornerRadius();
            if (radius > 0) {
                QPainterPath path;
                path.addRoundedRect(QRectF(panel), radius, radius);
                region = QRegion(path.toFillPolygon().toPolygon());
            } else {
                region = QRegion(panel);
            }
        }
    }

    // An empty region means "whole surface" to KWin, which would blur the entire
    // (mostly transparent) layer-shell surface. Until QML reports the panel rect,
    // keep compositor effects off; panelRectChanged re-applies them.
    if (region.isEmpty() && styleUsesBlur(type)) {
        removeBackgroundFromWindow(this);
    } else {
        applyBackgroundToWindow(this, type, region);
    }
    Q_EMIT backgroundColorChanged();
    Q_EMIT backgroundStyleTypeChanged();
}

} // namespace krema
