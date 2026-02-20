// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "settingswindow.h"

#include "config/docksettings.h"

#include <KLocalizedContext>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcSettingsWindow, "krema.settings.window")

namespace krema
{

SettingsWindow::SettingsWindow(DockSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::show()
{
    ensureEngine();

    // If the window already exists, just raise it
    const auto rootObjects = m_engine->rootObjects();
    if (!rootObjects.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(rootObjects.first());
        if (window) {
            window->show();
            window->raise();
            window->requestActivate();
            Q_EMIT visibleChanged(true);
            return;
        }
    }

    // Load the QML (first time)
    m_engine->load(QUrl(QStringLiteral("qrc:/qml/SettingsDialog.qml")));

    if (m_engine->rootObjects().isEmpty()) {
        qCWarning(lcSettingsWindow) << "Failed to load SettingsDialog.qml";
        return;
    }

    // Set the window icon to the application icon
    auto *window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    if (window) {
        window->setIcon(QGuiApplication::windowIcon());

        // Track window visibility for interaction lock
        connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
            if (!visible) {
                Q_EMIT this->visibleChanged(false);
            }
        });
    }

    Q_EMIT visibleChanged(true);
}

void SettingsWindow::ensureEngine()
{
    if (m_engine) {
        return;
    }

    m_engine = new QQmlApplicationEngine(this);
    m_engine->rootContext()->setContextObject(new KLocalizedContext(m_engine));
    m_engine->rootContext()->setContextProperty(QStringLiteral("dockSettings"), m_settings);
}

} // namespace krema
