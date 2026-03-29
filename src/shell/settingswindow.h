// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QPointer>

class KremaSettings;
class QQmlApplicationEngine;
class QQuickWindow;

namespace krema
{

class DockView;

/**
 * Settings dialog window.
 *
 * Opens a ConfigurationView-based settings window with sidebar navigation.
 * Uses QQmlApplicationEngine to load a host ApplicationWindow, which then
 * opens a ConfigurationView (creates its own ConfigWindow on desktop).
 */
class SettingsWindow : public QObject
{
    Q_OBJECT

public:
    explicit SettingsWindow(KremaSettings *settings, DockView *dockView, QObject *parent = nullptr);
    ~SettingsWindow() override;

    /// Show the settings dialog, or raise it if already visible.
    void show();

    /// Show the settings dialog with a specific module pre-selected.
    void show(const QString &defaultModule);

Q_SIGNALS:
    void visibleChanged(bool visible);

private:
    void ensureEngine();
    void trackConfigWindow(QObject *configView);

    KremaSettings *m_settings;
    DockView *m_dockView;
    QQmlApplicationEngine *m_engine = nullptr;
    QPointer<QQuickWindow> m_configWindow;
};

} // namespace krema
