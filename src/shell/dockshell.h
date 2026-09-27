// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"

#include <QObject>

#include <memory>

class KremaSettings;

namespace krema
{

class DockActions;
class DockContextMenu;
class DockModel;
class DockView;
class NotificationTracker;
class PreviewController;
class ScreenSettings;
class SettingsWindow;

/**
 * Encapsulates all objects for a single dock instance.
 *
 * Owns DockView, DockActions, DockContextMenu and PreviewController, and
 * wires settings signals to them. One DockShell exists per docked screen;
 * MultiDockManager destroys and recreates shells on mode or topology changes.
 * The settings dialog is shared by all shells and owned by MultiDockManager.
 */
class DockShell : public QObject
{
    Q_OBJECT

public:
    explicit DockShell(KremaSettings *globalSettings,
                       ScreenSettings *screenSettings,
                       DockModel *model,
                       NotificationTracker *tracker,
                       SettingsWindow *settingsWindow,
                       std::unique_ptr<DockPlatform> platform,
                       QObject *parent = nullptr);
    ~DockShell() override;

    /// Initialize the dock: register QML singletons, load QML, connect signals.
    void initialize(DockPlatform::Edge edge, DockPlatform::VisibilityMode visibilityMode);

    [[nodiscard]] DockView *view() const;
    [[nodiscard]] DockActions *actions() const;
    [[nodiscard]] DockContextMenu *contextMenu() const;
    [[nodiscard]] PreviewController *previewController() const;

    /// Activate keyboard focus on the dock (called from global shortcut).
    void focusDock();

private:
    void connectSettingsSignals();
    void connectMenuSignals();

    KremaSettings *m_settings;
    ScreenSettings *m_screenSettings;
    DockModel *m_model;
    SettingsWindow *m_settingsWindow; // shared, owned by MultiDockManager

    std::unique_ptr<DockView> m_view;
    std::unique_ptr<DockActions> m_actions;
    std::unique_ptr<DockContextMenu> m_contextMenu;
    // Declared after m_view so it is destroyed first: the preview surface
    // shares the dock's QML engine and binds to its "DockView" context property.
    std::unique_ptr<PreviewController> m_previewController;
};

} // namespace krema
