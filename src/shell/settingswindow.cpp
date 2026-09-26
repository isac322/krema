// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "settingswindow.h"

#include "krema.h"
#include "style/backgroundstyle.h"

#include <KLocalizedQmlContext>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcSettingsWindow, "krema.settings.window")

namespace krema
{

SettingsWindow::SettingsWindow(KremaSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

SettingsWindow::~SettingsWindow() = default;

bool SettingsWindow::isVisible() const
{
    return m_visible;
}

bool SettingsWindow::isStyleAvailable(int styleType) const
{
    return krema::isStyleAvailable(static_cast<BackgroundStyleType>(styleType));
}

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
        trackConfigWindow(configView);
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
        trackConfigWindow(configView);
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

void SettingsWindow::trackConfigWindow(QObject *configView)
{
    // configViewItem is set synchronously by ConfigurationView.open() — no polling needed.
    auto *win = qvariant_cast<QQuickWindow *>(configView->property("configViewItem"));
    if (!win) {
        qCWarning(lcSettingsWindow) << "configViewItem is null after open()";
        return;
    }

    m_configWindow = win;
    win->setIcon(QGuiApplication::windowIcon());

    connect(win, &QWindow::visibleChanged, this, [this](bool visible) {
        // Only forward close events — open is emitted manually below (exactly once)
        // to avoid double-counting that breaks dodge interacting refcount.
        if (!visible) {
            m_visible = false;
            Q_EMIT visibleChanged(false);
            m_configWindow = nullptr;
        }
    });

    win->show();
    win->raise();
    win->requestActivate();

    m_visible = true;
    Q_EMIT visibleChanged(true);

    qCDebug(lcSettingsWindow) << "Tracking config window:" << win;
}

} // namespace krema
