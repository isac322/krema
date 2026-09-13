// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockcontextmenu.h"

#include "dockactions.h"
#include "dockmodel.h"
#include "notificationtracker.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <KLocalizedString>

#include <QApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusInterface>
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
    const QString appId = m_model->appId(index);

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

    // --- Pin / Unpin ---
    if (pinned) {
        menu->addAction(QIcon::fromTheme(QStringLiteral("edit-clear-list")), i18nc("@action:inmenu", "Unpin from Dock"), this, [this, index]() {
            m_actions->togglePinned(index);
        });
    } else {
        menu->addAction(QIcon::fromTheme(QStringLiteral("window-pin")), i18nc("@action:inmenu", "Pin to Dock"), this, [this, index]() {
            m_actions->togglePinned(index);
        });
    }

    // --- New Instance ---
    menu->addAction(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:inmenu", "New Instance"), this, [this, index]() {
        m_actions->newInstance(index);
    });

    // --- Clear Notifications (only when badges are present) ---
    if (m_tracker != nullptr) {
        if (!appId.isEmpty() && m_tracker->unreadCount(appId) > 0) {
            menu->addAction(QIcon::fromTheme(QStringLiteral("notifications-clear")), i18nc("@action:inmenu", "Clear Notifications"), this, [this, appId]() {
                m_tracker->clearUnreadNotifications(appId);
            });
        }
    }

    // --- Window actions (running windows only) ---
    if (isWindow) {
        menu->addSeparator();

        // Minimize / Restore
        const bool minimized = idx.data(TaskManager::AbstractTasksModel::IsMinimized).toBool();
        if (minimized) {
            menu->addAction(QIcon::fromTheme(QStringLiteral("window-restore")), i18nc("@action:inmenu", "Restore"), tasksModel, [tasksModel, idx]() {
                tasksModel->requestToggleMinimized(idx);
                tasksModel->requestActivate(idx);
            });
        } else {
            menu->addAction(QIcon::fromTheme(QStringLiteral("window-minimize")), i18nc("@action:inmenu", "Minimize"), tasksModel, [tasksModel, idx]() {
                tasksModel->requestToggleMinimized(idx);
            });
        }

        // Maximize / Unmaximize
        if (idx.data(TaskManager::AbstractTasksModel::IsMaximized).toBool()) {
            menu->addAction(QIcon::fromTheme(QStringLiteral("window-unmaximize")), i18nc("@action:inmenu", "Unmaximize"), tasksModel, [tasksModel, idx]() {
                tasksModel->requestToggleMaximized(idx);
            });
        } else {
            menu->addAction(QIcon::fromTheme(QStringLiteral("window-maximize")), i18nc("@action:inmenu", "Maximize"), tasksModel, [tasksModel, idx]() {
                tasksModel->requestToggleMaximized(idx);
            });
        }

        // Keep Above / Below
        if (idx.data(TaskManager::AbstractTasksModel::IsKeepAbove).toBool()) {
            menu->addAction(QIcon::fromTheme(QStringLiteral("window-unkeep-above")), i18nc("@action:inmenu", "No Keep Above"), tasksModel, [tasksModel, idx]() {
                tasksModel->requestToggleKeepAbove(idx);
            });
        } else {
            menu->addAction(QIcon::fromTheme(QStringLiteral("window-keep-above")),
                            i18nc("@action:inmenu", "Keep Above Others"),
                            tasksModel,
                            [tasksModel, idx]() {
                                tasksModel->requestToggleKeepAbove(idx);
                            });
        }

        if (idx.data(TaskManager::AbstractTasksModel::IsKeepBelow).toBool()) {
            menu->addAction(QIcon::fromTheme(QStringLiteral("window-unkeep-below")), i18nc("@action:inmenu", "No Keep Below"), tasksModel, [tasksModel, idx]() {
                tasksModel->requestToggleKeepBelow(idx);
            });
        } else {
            menu->addAction(QIcon::fromTheme(QStringLiteral("window-keep-below")),
                            i18nc("@action:inmenu", "Keep Below Others"),
                            tasksModel,
                            [tasksModel, idx]() {
                                tasksModel->requestToggleKeepBelow(idx);
                            });
        }

        // Fullscreen
        if (idx.data(TaskManager::AbstractTasksModel::IsFullScreenable).toBool()) {
            if (idx.data(TaskManager::AbstractTasksModel::IsFullScreen).toBool()) {
                menu->addAction(QIcon::fromTheme(QStringLiteral("view-restore")), i18nc("@action:inmenu", "Exit Fullscreen"), tasksModel, [tasksModel, idx]() {
                    tasksModel->requestToggleFullScreen(idx);
                });
            } else {
                menu->addAction(QIcon::fromTheme(QStringLiteral("view-fullscreen")), i18nc("@action:inmenu", "Fullscreen"), tasksModel, [tasksModel, idx]() {
                    tasksModel->requestToggleFullScreen(idx);
                });
            }
        }

        // Show on All Desktops / Move to Current Desktop
        if (idx.data(TaskManager::AbstractTasksModel::IsVirtualDesktopsChangeable).toBool()) {
            if (idx.data(TaskManager::AbstractTasksModel::IsOnAllVirtualDesktops).toBool()) {
                menu->addAction(QIcon::fromTheme(QStringLiteral("virtual-desktops")),
                                i18nc("@action:inmenu", "Move to Current Desktop"),
                                tasksModel,
                                [tasksModel, idx]() {
                                    // Empty list = current desktop (see requestVirtualDesktops docs)
                                    tasksModel->requestVirtualDesktops(idx, {});
                                });
            } else {
                menu->addAction(QIcon::fromTheme(QStringLiteral("virtual-desktops")),
                                i18nc("@action:inmenu", "Show on All Desktops"),
                                tasksModel,
                                [tasksModel, idx]() {
                                    tasksModel->requestVirtualDesktops(idx, {});
                                });
            }
        }

        // --- Waydroid: Stop Container (replaces the misleading "Quit") ---
        // Waydroid's task entry is the container UI; "closing" it does not stop
        // the container. Offer the real action instead.
        if (appId.contains(QLatin1String("waydroid"), Qt::CaseInsensitive)) {
            menu->addSeparator();
            menu->addAction(QIcon::fromTheme(QStringLiteral("process-stop")), i18nc("@action:inmenu", "Stop Waydroid"), this, []() {
                auto *session = new QDBusInterface(QStringLiteral("org.waydroid"),
                                                   QStringLiteral("/Session"),
                                                   QStringLiteral("org.waydroid.Session"),
                                                   QDBusConnection::sessionBus());
                session->setTimeout(2000);
                session->call(QStringLiteral("unregister_app"));
                session->deleteLater();

                auto *platform = new QDBusInterface(QStringLiteral("org.waydroid"),
                                                    QStringLiteral("/Platform"),
                                                    QStringLiteral("org.waydroid.Platform"),
                                                    QDBusConnection::sessionBus());
                platform->setTimeout(2000);
                platform->call(QStringLiteral("stop"));
                platform->deleteLater();
            });
        }

        // --- Close (only for running windows) ---
        menu->addSeparator();
        menu->addAction(QIcon::fromTheme(QStringLiteral("window-close")), i18nc("@action:inmenu", "Close"), this, [this, index]() {
            m_actions->closeTask(index);
        });
    }

    // --- Dock-level actions ---
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action:inmenu", "Dock Settings..."), this, [this]() {
        Q_EMIT settingsRequested();
    });
    menu->addAction(QIcon::fromTheme(QStringLiteral("help-about")), i18nc("@action:inmenu", "About Krema"), this, [this]() {
        Q_EMIT aboutRequested();
    });

    // Track menu visibility for interaction lock (dock stays visible while menu is open)
    Q_EMIT visibleChanged(true);
    connect(menu, &QMenu::aboutToHide, this, [this]() {
        Q_EMIT visibleChanged(false);
    });

    menu->popup(QCursor::pos());
}

} // namespace krema
