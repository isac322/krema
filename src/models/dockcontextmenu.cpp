// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockcontextmenu.h"

#include "desktopactions.h"
#include "dockactions.h"
#include "dockmodel.h"
#include "notificationtracker.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <KLocalizedString>

#include <QApplication>
#include <QCursor>
#include <QIcon>
#include <QMenu>

namespace krema
{

DockContextMenu::DockContextMenu(DockModel *model, DockActions *actions, NotificationTracker *tracker, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_actions(actions)
    , m_tracker(tracker)
{
}

void DockContextMenu::showForTask(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
    const QString name = idx.data(Qt::DisplayRole).toString();

    const bool pinned = m_model->isPinned(index);

    auto *menu = new QMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // App name header (disabled, bold)
    QAction *header = menu->addAction(name);
    header->setEnabled(false);
    QFont boldFont = header->font();
    boldFont.setBold(true);
    header->setFont(boldFont);

    menu->addSeparator();

    // Desktop entry actions (e.g. "New Private Window"), placed like Plasma's
    // task manager. Group parents report their first window's launcher URL.
    // Each action is captured by value together with its KService, so it
    // launches the right application even if task rows change while the menu
    // is open.
    const QList<KServiceAction> appActions = desktopActions(idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl());
    if (!appActions.isEmpty()) {
        for (const KServiceAction &action : appActions) {
            if (action.isSeparator()) {
                menu->addSeparator();
                continue;
            }
            // Desktop files use plain text; '&' is not a mnemonic marker.
            QString text = action.text();
            text.replace(QLatin1Char('&'), QLatin1String("&&"));
            menu->addAction(QIcon::fromTheme(action.icon()), text, this, [action]() {
                launchDesktopAction(action);
            });
        }
        menu->addSeparator();
    }

    // Pin / Unpin
    if (pinned) {
        menu->addAction(QIcon::fromTheme(QStringLiteral("window-unpin")), i18nc("@action:inmenu", "Unpin from Dock"), this, [this, index]() {
            m_actions->togglePinned(index);
        });
    } else {
        menu->addAction(QIcon::fromTheme(QStringLiteral("window-pin")), i18nc("@action:inmenu", "Pin to Dock"), this, [this, index]() {
            m_actions->togglePinned(index);
        });
    }

    // New Instance
    menu->addAction(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:inmenu", "New Instance"), this, [this, index]() {
        m_actions->newInstance(index);
    });

    // Clear Notifications (only when badges are present)
    if (m_tracker != nullptr) {
        const QString appId = m_model->appId(index);
        if (!appId.isEmpty() && m_tracker->unreadCount(appId) > 0) {
            menu->addAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18nc("@action:inmenu", "Clear Notifications"), this, [this, appId]() {
                m_tracker->clearUnreadNotifications(appId);
            });
        }
    }

    // Close (only for running windows)
    if (isWindow) {
        menu->addSeparator();
        menu->addAction(QIcon::fromTheme(QStringLiteral("window-close")), i18nc("@action:inmenu", "Close"), this, [this, index]() {
            m_actions->closeTask(index);
        });
    }

    menu->addSeparator();
    addSettingsActions(menu);
    popup(menu);
}

void DockContextMenu::showForDock()
{
    auto *menu = new QMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    addSettingsActions(menu);
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18nc("@action:inmenu", "Quit Krema"), qApp, &QApplication::quit);
    popup(menu);
}

void DockContextMenu::addSettingsActions(QMenu *menu)
{
    menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action:inmenu", "Settings..."), this, [this]() {
        Q_EMIT settingsRequested();
    });

    // Standard KDE actions
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("help-about")), i18nc("@action:inmenu", "About Krema"), this, [this]() {
        Q_EMIT aboutRequested();
    });
}

void DockContextMenu::popup(QMenu *menu)
{
    // Open before popup(): the popup grab makes QtWayland send the dock a
    // synthetic leave, which QML must already see as part of the menu hold.
    // Also drives the interaction lock (dock stays visible while menu is open).
    setOpen(true);
    connect(menu, &QMenu::aboutToHide, this, [this]() {
        setOpen(false);
    });

    menu->popup(QCursor::pos());
}

bool DockContextMenu::isOpen() const
{
    return m_open;
}

void DockContextMenu::setOpen(bool open)
{
    if (m_open == open) {
        return;
    }
    m_open = open;
    Q_EMIT openChanged();
}

} // namespace krema
