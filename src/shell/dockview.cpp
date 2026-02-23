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

    auto *visibility = m_visibilityController;
    qmlRegisterSingletonType<DockVisibilityController>("com.bhyoo.krema", 1, 0, "DockVisibility", [visibility](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(visibility, QQmlEngine::CppOwnership);
        return visibility;
    });

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
    m_backgroundStyle->applyToWindow(this);
    Q_EMIT backgroundColorChanged();
}

} // namespace krema
