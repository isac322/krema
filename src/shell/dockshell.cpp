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
    // Pass per-screen settings to DockView for correct surface sizing
    m_view->setScreenSettings(m_screenSettings);

    // Set context properties for shell-owned objects (must be before QML loading).
    // Context properties are per-engine, unlike qmlRegisterSingletonType which is
    // process-global. This supports multiple DockShell instances (M8: multi-monitor).
    auto *ctx = m_view->engine()->rootContext();
    ctx->setContextProperty(QStringLiteral("DockView"), m_view.get());
    ctx->setContextProperty(QStringLiteral("DockActions"), m_actions.get());
    ctx->setContextProperty(QStringLiteral("DockContextMenu"), m_contextMenu.get());
    ctx->setContextProperty(QStringLiteral("PreviewController"), m_previewController);

    // Initialize dock view (creates DockVisibility, registers it, loads QML, shows window)
    m_view->initialize(m_model->tasksModel(), m_model->virtualDesktopInfo(), m_model->activityInfo(), edge, visibilityMode);

    // Configure and initialize preview surface (needs dock height for margins)
    m_previewController->setHideDelay(m_settings->previewHideDelay());
    m_previewController->initialize();

    // Apply initial delay settings to visibility controller
    m_view->visibilityController()->setShowDelay(m_settings->showDelay());
    m_view->visibilityController()->setHideDelay(m_settings->hideDelay());
    m_view->visibilityController()->setDodgeActiveOnly(m_settings->dodgeActiveOnly());

    // Settings window (pass DockView for QML context property access, e.g. isStyleAvailable)
    m_settingsWindow = std::make_unique<SettingsWindow>(m_settings, m_view.get(), this);

    // Connect all signals
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

    // Per-screen overrideable: iconSize, maxZoomFactor, floating → use ScreenSettings signals
    connect(ss, &ScreenSettings::iconSizeChanged, m_view.get(), &DockView::updateSize);
    connect(ss, &ScreenSettings::maxZoomFactorChanged, m_view.get(), &DockView::updateSize);
    connect(ss, &ScreenSettings::floatingChanged, m_view.get(), [this]() {
        m_view->updateSize();
        Q_EMIT m_view->floatingPaddingChanged();
    });

    // Shadow: no surface resize needed — shadow renders within available space
    // and naturally clips at surface boundaries (QML ShaderEffect computes its own margin)

    // Background style changes — per-screen overrideable: backgroundOpacity, backgroundStyle, cornerRadius
    connect(ss, &ScreenSettings::backgroundOpacityChanged, m_view.get(), &DockView::applyBackgroundStyle);
    connect(ss, &ScreenSettings::backgroundStyleChanged, m_view.get(), &DockView::applyBackgroundStyle);
    // Tint/accent/system colors are global-only (not per-screen overrideable)
    connect(s, &KremaSettings::TintColorChanged, m_view.get(), &DockView::applyBackgroundStyle);
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

    // Icon normalization toggle
    connect(s, &KremaSettings::IconNormalizationChanged, this, [this]() {
        m_view->iconProvider()->setNormalizationEnabled(m_settings->iconNormalization());
        m_view->iconProvider()->clearCache();
        m_view->bumpIconCacheVersion();
    });

    // Icon scale
    connect(s, &KremaSettings::IconScaleChanged, this, [this]() {
        m_view->iconProvider()->setIconScale(m_settings->iconScale());
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
