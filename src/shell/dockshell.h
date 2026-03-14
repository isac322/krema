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
class SettingsWindow;

/**
 * Encapsulates all objects for a single dock instance.
 *
 * Owns DockView, DockActions, DockContextMenu, PreviewController,
 * and SettingsWindow. Wires settings signals to subsystems.
 * Designed for future multi-monitor support (one DockShell per screen).
 */
class DockShell : public QObject
{
    Q_OBJECT

public:
    explicit DockShell(KremaSettings *settings,
                       DockModel *model,
                       NotificationTracker *tracker,
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
    DockModel *m_model;

    std::unique_ptr<DockView> m_view;
    std::unique_ptr<DockActions> m_actions;
    std::unique_ptr<DockContextMenu> m_contextMenu;
    PreviewController *m_previewController = nullptr;
    std::unique_ptr<SettingsWindow> m_settingsWindow;
};

} // namespace krema
