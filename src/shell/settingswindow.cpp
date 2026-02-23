// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "settingswindow.h"

#include "krema.h"

#include <KLocalizedQmlContext>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

Q_LOGGING_CATEGORY(lcSettingsWindow, "krema.settings.window")

namespace krema
{

SettingsWindow::SettingsWindow(KremaSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
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
        QMetaObject::invokeMethod(configView, "open");

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
}

void SettingsWindow::findAndTrackConfigWindow()
{
    // ConfigurationView.open() creates a top-level window.
    // Find it among all QWindows.
    const auto allWindows = QGuiApplication::allWindows();
    for (auto *win : allWindows) {
        auto *quickWin = qobject_cast<QQuickWindow *>(win);
        if (!quickWin || quickWin == m_configWindow) {
            continue;
        }
        // The config window's objectName or title helps identify it.
        // ConfigWindow sets title from ConfigurationView.title (defaults to "Settings").
        // Skip the invisible host window (our root object).
        if (!m_engine->rootObjects().isEmpty() && quickWin == qobject_cast<QQuickWindow *>(m_engine->rootObjects().first())) {
            continue;
        }
        // Found a new window — track it
        if (quickWin->isVisible()) {
            m_configWindow = quickWin;
            quickWin->setIcon(QGuiApplication::windowIcon());

            connect(quickWin, &QWindow::visibleChanged, this, [this](bool visible) {
                if (!visible) {
                    m_configWindow = nullptr;
                    Q_EMIT visibleChanged(false);
                }
            });

            Q_EMIT visibleChanged(true);
            qCDebug(lcSettingsWindow) << "Tracking config window:" << quickWin;
            return;
        }
    }

    qCDebug(lcSettingsWindow) << "Config window not found yet";
}

} // namespace krema
