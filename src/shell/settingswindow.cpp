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
#include <QQmlComponent>
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
    // Destroy the open window and any closed window still awaiting deferred
    // deletion (#27) here, while the engine and the "SettingsWindow" context
    // property are intact. Disconnect first so no close handling runs from
    // here.
    auto windows = std::exchange(m_closedWindows, {});
    windows.append(m_configWindow);
    m_configWindow = nullptr;
    for (const auto &win : std::as_const(windows)) {
        if (win) {
            disconnect(win, nullptr, this, nullptr);
            delete win.data();
        }
    }

    // Delete the engine while the "SettingsWindow" context property still
    // resolves to this object. m_engine is only a QObject child, so a default
    // destructor would destroy it after this object's QML bindings are gone
    // and teardown would log TypeError (e.g. "isStyleAvailable of null").
    delete m_component;
    m_component = nullptr;
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
    if (m_configWindow) {
        // Already open: switch to the requested page and raise it.
        if (!defaultModule.isEmpty()) {
            QMetaObject::invokeMethod(m_configWindow, "openModule", Q_ARG(QVariant, defaultModule));
        }
    } else {
        m_configWindow = createWindow(defaultModule);
        if (!m_configWindow) {
            return;
        }
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

QQuickWindow *SettingsWindow::createWindow(const QString &defaultModule)
{
    if (!m_engine) {
        m_engine = new QQmlEngine(this);
        KLocalization::setupLocalizedContext(m_engine);

        // Expose this object so settings QML can call
        // SettingsWindow.isStyleAvailable() without process-global singleton
        // registration or a per-dock object.
        m_engine->rootContext()->setContextProperty(QStringLiteral("SettingsWindow"), this);

        m_component = new QQmlComponent(m_engine, QUrl(QStringLiteral("qrc:/qml/SettingsDialog.qml")), QQmlComponent::PreferSynchronous, m_engine);
    }

    if (!m_component->isReady()) {
        qCWarning(lcSettingsWindow) << "Failed to load SettingsDialog.qml:" << m_component->errorString();
        return nullptr;
    }

    QObject *object = m_component->createWithInitialProperties({{QStringLiteral("defaultModule"), defaultModule}});
    if (!object) {
        qCWarning(lcSettingsWindow) << "Failed to create SettingsDialog.qml:" << m_component->errorString();
        return nullptr;
    }
    auto *win = qobject_cast<QQuickWindow *>(object);
    if (!win) {
        qCWarning(lcSettingsWindow) << "Root object of SettingsDialog.qml is not a QQuickWindow";
        delete object;
        return nullptr;
    }

    // This object owns the window: it is destroyed on close and at teardown,
    // never by the QML garbage collector.
    QQmlEngine::setObjectOwnership(win, QQmlEngine::CppOwnership);
    win->setIcon(QGuiApplication::windowIcon());

    connect(win, &QWindow::visibleChanged, this, [this, win](bool visible) {
        // Only forward close events — open is emitted by open().
        if (!visible) {
            onWindowHidden(win);
        }
    });

    qCDebug(lcSettingsWindow) << "Created settings window:" << win;
    return win;
}

void SettingsWindow::onWindowHidden(QQuickWindow *win)
{
    if (win != m_configWindow) {
        return;
    }

    // A closed window is destroyed rather than kept hidden; the next open
    // creates a fresh one.
    disconnect(win, nullptr, this, nullptr);
    m_configWindow = nullptr;
    m_closedWindows.removeIf([](const QPointer<QQuickWindow> &closed) {
        return closed.isNull();
    });
    m_closedWindows.append(win);
    win->deleteLater();

    if (m_visible) {
        m_visible = false;
        Q_EMIT visibleChanged(false);
    }
}

} // namespace krema
