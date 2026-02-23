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
    explicit SettingsWindow(KremaSettings *settings, QObject *parent = nullptr);
    ~SettingsWindow() override;

    /// Show the settings dialog, or raise it if already visible.
    void show();

    /// Show the settings dialog with a specific module pre-selected.
    void show(const QString &defaultModule);

Q_SIGNALS:
    void visibleChanged(bool visible);

private:
    void ensureEngine();
    void findAndTrackConfigWindow();

    KremaSettings *m_settings;
    QQmlApplicationEngine *m_engine = nullptr;
    QPointer<QQuickWindow> m_configWindow;
};

} // namespace krema
