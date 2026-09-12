// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockshell.h"

#include "config/screensettings.h"
#include "dockview.h"
#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "models/dockactions.h"
#include "models/dockcontextmenu.h"
#include "models/dockmodel.h"
#include "models/taskiconprovider.h"
#include "previewcontroller.h"
#include "settingswindow.h"

#include <QQuickItem>
#include <QtQml>

namespace krema
{

DockShell::DockShell(KremaSettings *globalSettings,
                     ScreenSettings *screenSettings,
                     DockModel *model,
                     NotificationTracker *tracker,
                     std::unique_ptr<DockPlatform> platform,
                     QObject *parent)
    : QObject(parent)
    , m_settings(globalSettings)
    , m_screenSettings(screenSettings)
    , m_model(model)
    , m_view(std::make_unique<DockView>(std::move(platform), globalSettings))
    , m_actions(std::make_unique<DockActions>(model, this))
    , m_contextMenu(std::make_unique<DockContextMenu>(model, m_actions.get(), tracker, this))
    , m_previewController(new PreviewController(model, m_view.get(), globalSettings, m_view.get()))
{
}

DockShell::~DockShell() = default;

void DockShell::initialize(DockPlatform::Edge edge, DockPlatform::VisibilityMode visibilityMode)
{
    // 1. Create the settings object FIRST
    m_settingsWindow = std::make_unique<SettingsWindow>(m_settings, m_view.get(), this);

    // 2. Pass per-screen settings to DockView
    m_view->setScreenSettings(m_screenSettings);

    // 3. Set context properties (SettingsController is now valid!)
    auto *ctx = m_view->engine()->rootContext();
    ctx->setContextProperty(QStringLiteral("DockView"), m_view.get());
    ctx->setContextProperty(QStringLiteral("DockActions"), m_actions.get());
    ctx->setContextProperty(QStringLiteral("DockContextMenu"), m_contextMenu.get());
    ctx->setContextProperty(QStringLiteral("PreviewController"), m_previewController);
    ctx->setContextProperty(QStringLiteral("SettingsController"), m_settingsWindow.get());

    // 4. Initialize dock view
    m_view->initialize(m_model->tasksModel(), m_model->virtualDesktopInfo(), m_model->activityInfo(), edge, visibilityMode);

    // Reload configuration when settings are changed in the UI
    connect(m_settingsWindow.get(), &SettingsWindow::requestSync, this, [this]() {
        m_settings->load();
        m_view->updateSize();
    });

    // 5. Configure and initialize preview surface
    m_previewController->setHideDelay(m_settings->previewHideDelay());
    m_previewController->initialize();

    // 6. Apply initial visibility settings
    m_view->visibilityController()->setShowDelay(m_settings->showDelay());
    m_view->visibilityController()->setHideDelay(m_settings->hideDelay());
    m_view->visibilityController()->setDodgeActiveOnly(m_settings->dodgeActiveOnly());
    m_view->visibilityController()->setReserveSpace(m_settings->reserveSpace());
    m_view->visibilityController()->setReserveMode(m_settings->reserveMode());
    m_view->visibilityController()->setFloatingPadding(m_view->floatingPadding());

    // 7. Connect all signals
    connectSettingsSignals();
    connectMenuSignals();
}

DockView *DockShell::view() const
{
    return m_view.get();
}

DockActions *DockShell::actions() const
{
    return m_actions.get();
}

DockContextMenu *DockShell::contextMenu() const
{
    return m_contextMenu.get();
}

PreviewController *DockShell::previewController() const
{
    return m_previewController;
}

void DockShell::focusDock()
{
    // Enable layer-shell keyboard interactivity so key events reach the surface
    m_view->platform()->setKeyboardInteractivity(true);

    // Ensure dock is visible and request keyboard focus on the QQuickView
    m_view->visibilityController()->setKeyboardActive(true);
    m_view->requestActivate();

    // Call QML startKeyboardNavigation() via the root object
    auto *rootObj = m_view->rootObject();
    if (rootObj) {
        QMetaObject::invokeMethod(rootObj, "startKeyboardNavigation", Qt::AutoConnection);
    }
}

void DockShell::connectSettingsSignals()
{
    auto *s = m_settings;
    auto *ss = m_screenSettings;

    // Global & Per-screen reactivity: ensure surface resizes instantly when either changes.
    // This fixes the 'Wiggle' bug where surface size would stay stale until a mouse event.
    // Throttle Wayland surface resizes to prevent compositor desync during slider drags
    auto resizeTimer = new QTimer(this);
    resizeTimer->setSingleShot(true);
    resizeTimer->setInterval(150);
    connect(resizeTimer, &QTimer::timeout, this, [this]() {
        m_view->updateSize();
    });
    auto updateThrottled = [resizeTimer]() {
        resizeTimer->start();
    };

    connect(s, &KremaSettings::IconSizeChanged, m_view.get(), updateThrottled);
    connect(s, &KremaSettings::MaxZoomFactorChanged, m_view.get(), updateThrottled);
    connect(ss, &ScreenSettings::iconSizeChanged, m_view.get(), updateThrottled);
    connect(ss, &ScreenSettings::maxZoomFactorChanged, m_view.get(), updateThrottled);

    connect(ss, &ScreenSettings::floatingChanged, m_view.get(), [this]() {
        m_view->updateSize();
        m_view->visibilityController()->setFloatingPadding(m_view->floatingPadding());
        Q_EMIT m_view->floatingPaddingChanged();
    });

    // Shadow: no surface resize needed — shadow renders within available space
    // and naturally clips at surface boundaries (QML ShaderEffect computes its own margin)

    // Background style changes — per-screen overrideable: backgroundOpacity, backgroundStyle, cornerRadius
    connect(s, &KremaSettings::configChanged, m_view.get(), &DockView::applyBackgroundStyle);
    connect(s, &KremaSettings::configChanged, this, []() { /* Saved via Application */ });
    connect(s, &KremaSettings::UseAccentColorChanged, m_view.get(), &DockView::applyBackgroundStyle);
    connect(s, &KremaSettings::UseSystemColorChanged, m_view.get(), &DockView::applyBackgroundStyle);

    // Re-apply blur region when panel geometry or corner radius changes
    connect(m_view->visibilityController(), &DockVisibilityController::panelRectChanged, m_view.get(), &DockView::applyBackgroundStyle);
    connect(ss, &ScreenSettings::cornerRadiusChanged, m_view.get(), &DockView::applyBackgroundStyle);

    // Platform-level changes — per-screen overrideable: edge, visibilityMode
    connect(ss, &ScreenSettings::edgeChanged, this, [this]() {
        auto edge = static_cast<DockPlatform::Edge>(m_screenSettings->edge());
        m_view->platform()->setEdge(edge);
        m_view->setEdge(edge);
        m_previewController->updateEdge();
    });
    connect(ss, &ScreenSettings::visibilityModeChanged, this, [this]() {
        m_view->visibilityController()->setMode(static_cast<DockPlatform::VisibilityMode>(m_screenSettings->visibilityMode()));
    });
    connect(s, &KremaSettings::DodgeActiveOnlyChanged, this, [this]() {
        m_view->visibilityController()->setDodgeActiveOnly(m_settings->dodgeActiveOnly());
    });

    connect(s, &KremaSettings::reserveSpaceChanged, this, [this]() {
        m_view->visibilityController()->setReserveSpace(m_settings->reserveSpace());
    });

    connect(s, &KremaSettings::reserveModeChanged, this, [this]() {
        m_view->visibilityController()->setReserveMode(m_settings->reserveMode());
    });

    // Icon normalization toggle
    connect(s, &KremaSettings::IconNormalizationChanged, this, [this]() {
        m_view->iconProvider()->setNormalizationEnabled(m_settings->iconNormalization());
        m_view->iconProvider()->clearCache();
        m_view->bumpIconCacheVersion();
    });

    // Indicator offset (formerly Icon scale)
    connect(s, &KremaSettings::IndicatorOffsetChanged, this, [this]() {
        m_view->iconProvider()->setIndicatorOffset(m_settings->indicatorOffset());
        m_view->iconProvider()->clearCache();
        m_view->bumpIconCacheVersion();
    });

    // Delay settings
    connect(s, &KremaSettings::ShowDelayChanged, this, [this]() {
        m_view->visibilityController()->setShowDelay(m_settings->showDelay());
    });
    connect(s, &KremaSettings::HideDelayChanged, this, [this]() {
        m_view->visibilityController()->setHideDelay(m_settings->hideDelay());
    });
    connect(s, &KremaSettings::PreviewHideDelayChanged, this, [this]() {
        m_previewController->setHideDelay(m_settings->previewHideDelay());
    });
}

void DockShell::connectMenuSignals()
{
    // Context menu interaction lock: dock stays visible while menu is open
    connect(m_contextMenu.get(), &DockContextMenu::visibleChanged, m_view->visibilityController(), &DockVisibilityController::setInteracting);

    connect(m_contextMenu.get(), &DockContextMenu::settingsRequested, this, [this]() {
        m_settingsWindow->show();
    });
    connect(m_contextMenu.get(), &DockContextMenu::aboutRequested, this, [this]() {
        m_settingsWindow->show(QStringLiteral("about"));
    });

    // Settings window interaction lock: dock stays visible while settings is open
    connect(m_settingsWindow.get(), &SettingsWindow::visibleChanged, m_view->visibilityController(), &DockVisibilityController::setInteracting);
}

} // namespace krema
