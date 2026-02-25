// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockshell.h"

#include "dockview.h"
#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "models/dockactions.h"
#include "models/dockcontextmenu.h"
#include "models/dockmodel.h"
#include "previewcontroller.h"
#include "settingswindow.h"

#include <QQuickItem>
#include <QtQml>

namespace krema
{

DockShell::DockShell(KremaSettings *settings, DockModel *model, std::unique_ptr<DockPlatform> platform, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_model(model)
    , m_view(std::make_unique<DockView>(std::move(platform), settings))
    , m_actions(std::make_unique<DockActions>(model, this))
    , m_contextMenu(std::make_unique<DockContextMenu>(model, m_actions.get(), this))
    , m_previewController(new PreviewController(model, m_view.get(), settings, m_view.get()))
{
}

DockShell::~DockShell() = default;

void DockShell::initialize(DockPlatform::Edge edge, DockPlatform::VisibilityMode visibilityMode)
{
    // Register QML singletons for shell-owned objects (must be before QML loading)
    // Use qmlRegisterSingletonType (not qmlRegisterSingletonInstance) so multiple
    // QML engines (dock + settings window) can access the same C++ objects.
    auto *view = m_view.get();
    qmlRegisterSingletonType<DockView>("com.bhyoo.krema", 1, 0, "DockView", [view](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(view, QQmlEngine::CppOwnership);
        return view;
    });
    auto *actions = m_actions.get();
    qmlRegisterSingletonType<DockActions>("com.bhyoo.krema", 1, 0, "DockActions", [actions](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(actions, QQmlEngine::CppOwnership);
        return actions;
    });
    auto *contextMenu = m_contextMenu.get();
    qmlRegisterSingletonType<DockContextMenu>("com.bhyoo.krema", 1, 0, "DockContextMenu", [contextMenu](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(contextMenu, QQmlEngine::CppOwnership);
        return contextMenu;
    });
    auto *preview = m_previewController;
    qmlRegisterSingletonType<PreviewController>("com.bhyoo.krema", 1, 0, "PreviewController", [preview](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(preview, QQmlEngine::CppOwnership);
        return preview;
    });

    // Initialize dock view (creates DockVisibility, registers it, loads QML, shows window)
    m_view->initialize(m_model->tasksModel(), m_model->virtualDesktopInfo(), m_model->activityInfo(), edge, visibilityMode);

    // Configure and initialize preview surface (needs dock height for margins)
    m_previewController->setHideDelay(m_settings->previewHideDelay());
    m_previewController->initialize();

    // Apply initial delay settings to visibility controller
    m_view->visibilityController()->setShowDelay(m_settings->showDelay());
    m_view->visibilityController()->setHideDelay(m_settings->hideDelay());
    m_view->visibilityController()->setDodgeActiveOnly(m_settings->dodgeActiveOnly());

    // Settings window
    m_settingsWindow = std::make_unique<SettingsWindow>(m_settings, this);

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

    // DockView surface size needs recalculation
    connect(s, &KremaSettings::IconSizeChanged, m_view.get(), &DockView::updateSize);
    connect(s, &KremaSettings::MaxZoomFactorChanged, m_view.get(), &DockView::updateSize);
    connect(s, &KremaSettings::FloatingChanged, m_view.get(), [this]() {
        m_view->updateSize();
        Q_EMIT m_view->floatingPaddingChanged();
    });

    // Shadow: no surface resize needed — shadow renders within available space
    // and naturally clips at surface boundaries (QML ShaderEffect computes its own margin)

    // Background style changes — applyBackgroundStyle re-applies both compositor effects
    // (blur/contrast region) and emits backgroundColorChanged, so it's the single entry point.
    // Opacity changes must also re-apply because blur is disabled at opacity=0.
    connect(s, &KremaSettings::BackgroundOpacityChanged, m_view.get(), &DockView::applyBackgroundStyle);
    connect(s, &KremaSettings::BackgroundStyleChanged, m_view.get(), &DockView::applyBackgroundStyle);
    connect(s, &KremaSettings::TintColorChanged, m_view.get(), &DockView::applyBackgroundStyle);
    connect(s, &KremaSettings::UseAccentColorChanged, m_view.get(), &DockView::applyBackgroundStyle);
    connect(s, &KremaSettings::UseSystemColorChanged, m_view.get(), &DockView::applyBackgroundStyle);

    // Re-apply blur region when panel geometry or corner radius changes
    connect(m_view->visibilityController(), &DockVisibilityController::panelRectChanged, m_view.get(), &DockView::applyBackgroundStyle);
    connect(s, &KremaSettings::CornerRadiusChanged, m_view.get(), &DockView::applyBackgroundStyle);

    // Platform-level changes
    connect(s, &KremaSettings::EdgeChanged, this, [this]() {
        m_view->platform()->setEdge(static_cast<DockPlatform::Edge>(m_settings->edge()));
    });
    connect(s, &KremaSettings::VisibilityModeChanged, this, [this]() {
        m_view->visibilityController()->setMode(static_cast<DockPlatform::VisibilityMode>(m_settings->visibilityMode()));
    });
    connect(s, &KremaSettings::DodgeActiveOnlyChanged, this, [this]() {
        m_view->visibilityController()->setDodgeActiveOnly(m_settings->dodgeActiveOnly());
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
