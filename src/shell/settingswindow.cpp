// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "settingswindow.h"

#include "krema.h"
#include "multidockmanager.h"
#include "outputordermonitor.h"
#include "style/backgroundstyle.h"

#include <KLocalizedQmlContext>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>

#include <utility>

Q_LOGGING_CATEGORY(lcSettingsWindow, "krema.settings.window")

namespace krema
{

SettingsWindow::SettingsWindow(KremaSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    auto *outputOrder = OutputOrderMonitor::instance();
    connect(outputOrder, &OutputOrderMonitor::primaryOutputChanged, this, &SettingsWindow::updateAvailableScreens);
    connect(m_settings, &KremaSettings::SelectedOutputsChanged, this, &SettingsWindow::updateAvailableScreens);
    connect(m_settings, &KremaSettings::MonitorModeChanged, this, &SettingsWindow::updateAvailableScreens);

    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
            watchScreen(screen);
            updateAvailableScreens();
        });
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &SettingsWindow::updateAvailableScreens);
        connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &SettingsWindow::updateAvailableScreens);
    }
    for (auto *screen : QGuiApplication::screens()) {
        watchScreen(screen);
    }
    updateAvailableScreens();
}

SettingsWindow::~SettingsWindow()
{
    // The settings window is the engine's root object, so deleting the engine
    // below destroys it while its pages and bindings are still intact.
    // Disconnect first so no close handling runs from here.
    if (m_configWindow) {
        disconnect(m_configWindow, nullptr, this, nullptr);
    }

    // Delete the engine while the "SettingsWindow" context property still
    // resolves to this object. m_engine is only a QObject child, so a default
    // destructor would destroy it after this object's QML bindings are gone
    // and teardown would log TypeError (e.g. "isStyleAvailable of null").
    delete m_engine;
    m_engine = nullptr;
}

bool SettingsWindow::isVisible() const
{
    return m_visible;
}

QVariantList SettingsWindow::availableScreens() const
{
    return m_availableScreens;
}

bool SettingsWindow::hasSelectedMonitorFallback() const
{
    return m_hasSelectedMonitorFallback;
}

void SettingsWindow::watchScreen(QScreen *screen)
{
    connect(screen, &QScreen::geometryChanged, this, &SettingsWindow::updateAvailableScreens);
}

void SettingsWindow::updateAvailableScreens()
{
    const auto screens = QGuiApplication::screens();
    const auto selectedOutputs = m_settings->selectedOutputs();
    const auto *primary = OutputOrderMonitor::instance()->primaryScreen();
    QVariantList rows;
    rows.reserve(screens.size() + selectedOutputs.size());
    QStringList outputNames;
    outputNames.reserve(screens.size() + selectedOutputs.size());
    bool hasLiveSelection = false;
    for (auto *screen : screens) {
        const QString name = screen->name();
        const bool available = !screen->geometry().isEmpty();
        rows.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("label"), name},
            {QStringLiteral("available"), available},
            {QStringLiteral("primary"), screen == primary},
        });
        outputNames.append(name);
        hasLiveSelection |= available && selectedOutputs.contains(name);
    }
    for (const auto &name : selectedOutputs) {
        if (outputNames.contains(name)) {
            continue;
        }
        rows.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("label"), name},
            {QStringLiteral("available"), false},
            {QStringLiteral("primary"), false},
        });
        outputNames.append(name);
    }

    const bool fallback = m_settings->monitorMode() == MultiDockManager::SelectedScreens && !hasLiveSelection;
    const bool rowsChanged = rows != m_availableScreens;
    const bool fallbackChanged = fallback != m_hasSelectedMonitorFallback;
    m_availableScreens = std::move(rows);
    m_hasSelectedMonitorFallback = fallback;
    if (rowsChanged) {
        Q_EMIT availableScreensChanged();
    }
    if (fallbackChanged) {
        Q_EMIT hasSelectedMonitorFallbackChanged();
    }
}

bool SettingsWindow::isStyleAvailable(int styleType) const
{
    return krema::isStyleAvailable(static_cast<BackgroundStyleType>(styleType));
}

void SettingsWindow::show()
{
    open(QString());
}

void SettingsWindow::show(const QString &defaultModule)
{
    open(defaultModule);
}

void SettingsWindow::open(const QString &defaultModule)
{
    ensureEngine();

    if (m_engine->rootObjects().isEmpty()) {
        m_engine->load(QUrl(QStringLiteral("qrc:/qml/SettingsDialog.qml")));

        if (m_engine->rootObjects().isEmpty()) {
            qCWarning(lcSettingsWindow) << "Failed to load SettingsDialog.qml";
            return;
        }
    }

    // The root object IS the settings window. It lives as long as the engine
    // and is only hidden on close, so it is tracked and connected once.
    if (!m_configWindow) {
        auto *win = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        if (!win) {
            qCWarning(lcSettingsWindow) << "Root object of SettingsDialog.qml is not a QQuickWindow";
            return;
        }
        trackConfigWindow(win);
    }

    if (!defaultModule.isEmpty()) {
        QMetaObject::invokeMethod(m_configWindow, "openModule", Q_ARG(QVariant, defaultModule));
    }

    m_configWindow->show();
    m_configWindow->raise();
    m_configWindow->requestActivate();

    // Emit open exactly once per open/close cycle to avoid double-counting
    // that breaks the dodge interacting refcount.
    if (!m_visible) {
        m_visible = true;
        Q_EMIT visibleChanged(true);
    }
}

void SettingsWindow::ensureEngine()
{
    if (m_engine) {
        return;
    }

    m_engine = new QQmlApplicationEngine(this);
    KLocalization::setupLocalizedContext(m_engine);

    // Expose this object so settings QML can call
    // SettingsWindow.isStyleAvailable() without process-global singleton
    // registration or a per-dock object.
    m_engine->rootContext()->setContextProperty(QStringLiteral("SettingsWindow"), this);
}

void SettingsWindow::trackConfigWindow(QQuickWindow *win)
{
    m_configWindow = win;
    win->setIcon(QGuiApplication::windowIcon());

    connect(win, &QWindow::visibleChanged, this, [this](bool visible) {
        // Only forward close events — open is emitted by open().
        if (visible || !m_visible) {
            return;
        }
        m_visible = false;
        Q_EMIT visibleChanged(false);
    });

    qCDebug(lcSettingsWindow) << "Tracking settings window:" << win;
}

} // namespace krema
