// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>

class KremaSettings;
class QQmlComponent;
class QQmlEngine;
class QQuickWindow;
class QScreen;

namespace krema
{

/**
 * Settings dialog window.
 *
 * Creates the window from SettingsDialog.qml (a sidebar of pages next to the
 * selected page) on every open and destroys it once it is closed. The QML
 * engine and the compiled component are kept for faster reopening.
 *
 * One instance serves every dock and must outlive dock shells: settings
 * handlers (e.g. Monitor mode) rebuild the shells while they are running.
 */
class SettingsWindow : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList availableScreens READ availableScreens NOTIFY availableScreensChanged)
    Q_PROPERTY(bool hasSelectedMonitorFallback READ hasSelectedMonitorFallback NOTIFY hasSelectedMonitorFallbackChanged)

public:
    explicit SettingsWindow(KremaSettings *settings, QObject *parent = nullptr);
    ~SettingsWindow() override;

    /// Show the settings dialog, or raise it if already visible.
    void show();

    /// Show the settings dialog with a specific module selected.
    void show(const QString &defaultModule);

    /// Whether the dialog is open (between visibleChanged(true) and
    /// visibleChanged(false)).
    [[nodiscard]] bool isVisible() const;

    /// Value-only output rows: name, label, available and primary. Saved
    /// disconnected selections remain present until the user removes them.
    [[nodiscard]] QVariantList availableScreens() const;
    [[nodiscard]] bool hasSelectedMonitorFallback() const;

    /// Check if a background style is available on this system (for settings
    /// QML).
    Q_INVOKABLE bool isStyleAvailable(int styleType) const;

Q_SIGNALS:
    void visibleChanged(bool visible);
    void availableScreensChanged();
    void hasSelectedMonitorFallbackChanged();

private:
    void open(const QString &defaultModule);
    QQuickWindow *createWindow(const QString &defaultModule);
    void onWindowHidden(QQuickWindow *win);
    void watchScreen(QScreen *screen);
    void updateAvailableScreens();

    KremaSettings *m_settings;
    QQmlEngine *m_engine = nullptr;
    QQmlComponent *m_component = nullptr;
    // The open window, if any.
    QPointer<QQuickWindow> m_configWindow;
    // Closed windows whose deferred deletion has not run yet.
    QList<QPointer<QQuickWindow>> m_closedWindows;
    QVariantList m_availableScreens;
    bool m_hasSelectedMonitorFallback = false;
    bool m_visible = false;
};

} // namespace krema
