// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockview.h"

#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "models/taskiconprovider.h"
#include "utils/surfacegeometry.h"

#include <QLoggingCategory>
#include <QPainterPath>

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
    // Configure the platform layer (LayerShellQt on Wayland)
    m_platform->setupWindow(this);
    m_platform->setEdge(edge);

    // Create visibility controller (manages show/hide logic for all modes)
    m_visibilityController = new DockVisibilityController(m_platform.get(), tasksModel, virtualDesktopInfo, activityInfo, this, this);
    m_visibilityController->setMode(visibilityMode);

    // Register the icon image provider for QML
    engine()->addImageProvider(QStringLiteral("icon"), new TaskIconProvider());

    auto *visibility = m_visibilityController;
    qmlRegisterSingletonType<DockVisibilityController>("com.bhyoo.krema", 1, 0, "DockVisibility", [visibility](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(visibility, QQmlEngine::CppOwnership);
        return visibility;
    });

    // Config migration: merge SemiTransparent into Tinted, renumber styles
    if (m_settings->configVersion() < 2) {
        int old = m_settings->backgroundStyle();
        // First: Mica (5) → old SemiTransparent (1) + accent color
        if (old == 5) {
            old = 1;
            m_settings->setUseAccentColor(true);
        }
        // Now remap old numbering to new
        switch (old) {
        case 1: // Old SemiTransparent → Tinted + UseSystemColor
            m_settings->setBackgroundStyle(2);
            m_settings->setUseSystemColor(true);
            break;
        case 2: // Old Transparent → new 1
            m_settings->setBackgroundStyle(1);
            break;
        case 3: // Old Tinted → new 2
            m_settings->setBackgroundStyle(2);
            break;
        case 4: // Old Acrylic → new 3
            m_settings->setBackgroundStyle(3);
            break;
        default: // PanelInherit (0) stays the same
            break;
        }
        m_settings->setConfigVersion(2);
        m_settings->save();
    }

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

int DockView::panelBarHeight() const
{
    return krema::panelBarHeight(m_settings->iconSize(), s_padding, floatingPadding());
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

    // Real screen restored — recalculate everything
    updateSize();
    applyBackgroundStyle();
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

    applyBackgroundToWindow(this, type, region);
    Q_EMIT backgroundColorChanged();
    Q_EMIT backgroundStyleTypeChanged();
}

} // namespace krema
