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
#include <QQmlEngine>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcSettingsWindow, "krema.settings.window")

namespace krema
{

SettingsWindow::SettingsWindow(KremaSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

SettingsWindow::~SettingsWindow()
{
    // Destroy the settings windows while the engine is intact. ConfigWindow is
    // parentless and JavaScript-owned, so otherwise the engine's teardown sweep
    // destroys it, which crashes on Qt 6.8 / KF 6.13 (Debian 13) when its pages
    // are still being created (#27). Disconnect first so no close handling
    // (lock release, deleteLater) runs from here; deleting also cancels any
    // pending deleteLater() or QML destroy().
    for (const auto &window : std::as_const(m_openedWindows)) {
        if (window) {
            disconnect(window, nullptr, this, nullptr);
            delete window.data();
        }
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

bool SettingsWindow::isStyleAvailable(int styleType) const
{
    return krema::isStyleAvailable(static_cast<BackgroundStyleType>(styleType));
}

void SettingsWindow::show()
{
    open(QVariant());
}

void SettingsWindow::show(const QString &defaultModule)
{
    open(defaultModule);
}

void SettingsWindow::open(const QVariant &defaultModule)
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

    auto *root = m_engine->rootObjects().first();
    auto *configView = root->findChild<QObject *>(QStringLiteral("configuration"));
    if (!configView) {
        qCWarning(lcSettingsWindow) << "ConfigurationView not found in SettingsDialog.qml";
        return;
    }

    // ConfigurationView.configViewItem exists since kirigami-addons 1.8.0.
    // Older versions (Debian 13, Ubuntu 25.04 ship 1.7.0) create the
    // ConfigWindow in open() without keeping a reference to it.
    const bool hasConfigViewItem = configView->metaObject()->indexOfProperty("configViewItem") >= 0;
    const QWindowList windowsBefore = hasConfigViewItem ? QWindowList() : QGuiApplication::allWindows();

    QMetaObject::invokeMethod(configView, "open", Q_ARG(QVariant, defaultModule));

    // Both paths are synchronous: open() creates the window before returning.
    auto *win = hasConfigViewItem ? qvariant_cast<QQuickWindow *>(configView->property("configViewItem")) : windowCreatedSince(windowsBefore);
    if (!win) {
        qCWarning(lcSettingsWindow) << "No settings window to track after ConfigurationView.open()";
        return;
    }

    // kirigami-addons >= 1.8 destroys its window when it closes; older
    // versions never do, so the window found here is deleted on close.
    trackConfigWindow(win, !hasConfigViewItem);
}

QQuickWindow *SettingsWindow::windowCreatedSince(const QWindowList &windowsBefore) const
{
    // The ConfigWindow is the one window of the settings engine that did not
    // exist before open(). Windows of other engines (dock, preview) and the
    // already loaded host window never qualify.
    QQuickWindow *created = nullptr;
    const auto windows = QGuiApplication::allWindows();
    for (auto *window : windows) {
        auto *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (!quickWindow || windowsBefore.contains(window) || qmlEngine(quickWindow) != m_engine) {
            continue;
        }
        if (created) {
            qCWarning(lcSettingsWindow) << "ConfigurationView.open() created more than one window";
            return nullptr;
        }
        created = quickWindow;
    }
    return created;
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

void SettingsWindow::trackConfigWindow(QQuickWindow *win, bool deleteOnClose)
{
    m_configWindow = win;
    win->setObjectName(QStringLiteral("kremaSettingsWindow"));
    m_openedWindows.removeAll(nullptr);
    m_openedWindows.append(win);
    win->setIcon(QGuiApplication::windowIcon());

    connect(win, &QWindow::visibleChanged, this, [this, window = QPointer<QQuickWindow>(win), deleteOnClose](bool visible) {
        // Only forward close events — open is emitted manually below (exactly once)
        // to avoid double-counting that breaks dodge interacting refcount.
        // Only the tracked window releases the lock.
        if (visible || window != m_configWindow) {
            return;
        }
        m_visible = false;
        Q_EMIT visibleChanged(false);
        m_configWindow = nullptr;
        if (deleteOnClose) {
            window->deleteLater();
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
