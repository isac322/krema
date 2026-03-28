// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "settingswindow.h"

#include "dockview.h"
#include "krema.h"

#include <KLocalizedQmlContext>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickView>
#include <QQuickWindow>
#include <QTimer>

Q_LOGGING_CATEGORY(lcSettingsWindow, "krema.settings.window")

namespace krema
{

SettingsWindow::SettingsWindow(KremaSettings *settings, DockView *dockView, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_dockView(dockView)
{
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::show()
{
    ensureEngine();

    // If the ConfigurationView's window is already open, just raise it
    if (m_configWindow) {
        m_configWindow->show();
        m_configWindow->raise();
        m_configWindow->requestActivate();
        return;
    }

    // Load the QML host (invisible ApplicationWindow + ConfigurationView)
    if (m_engine->rootObjects().isEmpty()) {
        m_engine->load(QUrl(QStringLiteral("qrc:/qml/SettingsDialog.qml")));

        if (m_engine->rootObjects().isEmpty()) {
            qCWarning(lcSettingsWindow) << "Failed to load SettingsDialog.qml";
            return;
        }
    }

    // Find the ConfigurationView and call open()
    auto *root = m_engine->rootObjects().first();
    auto *configView = root->findChild<QObject *>(QStringLiteral("configuration"));
    if (configView) {
        QMetaObject::invokeMethod(configView, "open", Q_ARG(QVariant, QVariant()));

        // Find the ConfigWindow created by ConfigurationView.open()
        // It's a top-level window created with createObject(null)
        QTimer::singleShot(0, this, [this]() {
            findAndTrackConfigWindow();
        });
    } else {
        qCWarning(lcSettingsWindow) << "ConfigurationView not found in SettingsDialog.qml";
    }
}

void SettingsWindow::show(const QString &defaultModule)
{
    ensureEngine();

    // If the ConfigurationView's window is already open, just raise it
    if (m_configWindow) {
        m_configWindow->show();
        m_configWindow->raise();
        m_configWindow->requestActivate();
        return;
    }

    if (m_engine->rootObjects().isEmpty()) {
        m_engine->load(QUrl(QStringLiteral("qrc:/qml/SettingsDialog.qml")));

        if (m_engine->rootObjects().isEmpty()) {
            qCWarning(lcSettingsWindow) << "Failed to load SettingsDialog.qml";
            return;
        }
    }

    auto *root = m_engine->rootObjects().first();
    auto *configView = root->findChild<QObject *>(QStringLiteral("configuration"));
    if (configView) {
        QMetaObject::invokeMethod(configView, "open", Q_ARG(QVariant, defaultModule));

        QTimer::singleShot(0, this, [this]() {
            findAndTrackConfigWindow();
        });
    }
}

void SettingsWindow::ensureEngine()
{
    if (m_engine) {
        return;
    }

    m_engine = new QQmlApplicationEngine(this);
    KLocalization::setupLocalizedContext(m_engine);

    // Expose DockView as context property so settings QML can access
    // DockView.isStyleAvailable() etc. without process-global singleton registration.
    m_engine->rootContext()->setContextProperty(QStringLiteral("DockView"), m_dockView);
}

void SettingsWindow::findAndTrackConfigWindow(int attempt)
{
    // ConfigurationView.open() creates a top-level window asynchronously.
    // Find it among all QWindows.
    const auto allWindows = QGuiApplication::allWindows();
    for (auto *win : allWindows) {
        auto *quickWin = qobject_cast<QQuickWindow *>(win);
        if (!quickWin || quickWin == m_configWindow) {
            continue;
        }
        // Skip QQuickView subclasses (e.g. DockView) — ConfigWindow is a plain QQuickWindow
        if (qobject_cast<QQuickView *>(win)) {
            continue;
        }
        // Skip the invisible host window (our root object).
        if (!m_engine->rootObjects().isEmpty() && quickWin == qobject_cast<QQuickWindow *>(m_engine->rootObjects().first())) {
            continue;
        }
        // Found the config window — track it regardless of visibility.
        // ConfigurationView.open() shows the window asynchronously; it may not be visible yet.
        m_configWindow = quickWin;
        quickWin->setIcon(QGuiApplication::windowIcon());

        connect(quickWin, &QWindow::visibleChanged, this, [this](bool visible) {
            // Only forward close events — open is emitted manually below (exactly once)
            // to avoid double-counting that breaks dodge interacting refcount.
            if (!visible) {
                Q_EMIT visibleChanged(false);
                m_configWindow = nullptr;
            }
        });

        // Ensure the window is visible and raised — ConfigurationView.open()
        // creates the window asynchronously, so it may not be shown yet.
        quickWin->show();
        quickWin->raise();
        quickWin->requestActivate();

        Q_EMIT visibleChanged(true);

        qCDebug(lcSettingsWindow) << "Tracking config window:" << quickWin;
        return;
    }

    // ConfigWindow may not exist yet — retry up to 10 times (500ms total)
    constexpr int maxAttempts = 10;
    if (attempt < maxAttempts) {
        QTimer::singleShot(50, this, [this, attempt]() {
            findAndTrackConfigWindow(attempt + 1);
        });
    } else {
        qCWarning(lcSettingsWindow) << "Config window not found after" << maxAttempts << "attempts";
    }
}

} // namespace krema
