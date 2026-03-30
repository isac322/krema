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
#include <QQuickWindow>

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
    showImpl(QString());
}

void SettingsWindow::show(const QString &defaultModule)
{
    showImpl(defaultModule);
}

void SettingsWindow::showImpl(const QString &defaultModule)
{
    ensureEngine();

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

    // The root object IS the settings window (no hidden host, no ConfigurationView)
    auto *win = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    if (!win) {
        qCWarning(lcSettingsWindow) << "Root object is not a QQuickWindow";
        return;
    }

    // Set default module before showing
    if (!defaultModule.isEmpty()) {
        win->setProperty("defaultModule", defaultModule);
    }

    m_configWindow = win;
    win->setIcon(QGuiApplication::windowIcon());

    connect(win, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) {
            Q_EMIT visibleChanged(false);
            m_configWindow = nullptr;
        }
    });

    win->show();
    win->raise();
    win->requestActivate();

    Q_EMIT visibleChanged(true);

    qCDebug(lcSettingsWindow) << "Tracking settings window:" << win;
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

} // namespace krema
