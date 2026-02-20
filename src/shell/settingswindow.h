// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>

class QQmlApplicationEngine;

namespace krema
{

class DockSettings;

/**
 * Settings dialog window.
 *
 * Opens a separate Kirigami-based QML window for configuring the dock.
 * Uses QQmlApplicationEngine (separate from the dock's QQuickView)
 * to support Kirigami.ApplicationWindow.
 */
class SettingsWindow : public QObject
{
    Q_OBJECT

public:
    explicit SettingsWindow(DockSettings *settings, QObject *parent = nullptr);
    ~SettingsWindow() override;

    /// Show the settings dialog, or raise it if already visible.
    void show();

Q_SIGNALS:
    void visibleChanged(bool visible);

private:
    void ensureEngine();

    DockSettings *m_settings;
    QQmlApplicationEngine *m_engine = nullptr;
};

} // namespace krema
