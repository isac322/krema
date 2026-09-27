// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QPointer>
#include <QWindowList>

class KremaSettings;
class QQmlApplicationEngine;
class QQuickWindow;
class QVariant;

namespace krema
{

/**
 * Settings dialog window.
 *
 * Opens a ConfigurationView-based settings window with sidebar navigation.
 * Uses QQmlApplicationEngine to load a host ApplicationWindow, which then
 * opens a ConfigurationView (creates its own ConfigWindow on desktop).
 *
 * One instance serves every dock and must outlive dock shells: settings
 * handlers (e.g. Monitor mode) rebuild the shells while they are running.
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

    /// Whether the dialog is open (between visibleChanged(true) and
    /// visibleChanged(false)).
    [[nodiscard]] bool isVisible() const;

    /// Check if a background style is available on this system (for settings
    /// QML).
    Q_INVOKABLE bool isStyleAvailable(int styleType) const;

Q_SIGNALS:
    void visibleChanged(bool visible);

private:
    void open(const QVariant &defaultModule);
    void ensureEngine();
    [[nodiscard]] QQuickWindow *windowCreatedSince(const QWindowList &windowsBefore) const;
    void trackConfigWindow(QQuickWindow *win, bool deleteOnClose);

    KremaSettings *m_settings;
    QQmlApplicationEngine *m_engine = nullptr;
    QPointer<QQuickWindow> m_configWindow;
    bool m_visible = false;
};

} // namespace krema
