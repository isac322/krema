// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockcontextmenu.h"

#include "dockactions.h"
#include "dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <KLocalizedString>

#include <QApplication>
#include <QCursor>
#include <QMenu>

namespace krema
{

DockContextMenu::DockContextMenu(DockModel *model, DockActions *actions, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_actions(actions)
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

    // Pin / Unpin
    if (pinned) {
        menu->addAction(i18nc("@action:inmenu", "Unpin from Dock"), this, [this, index]() {
            m_actions->togglePinned(index);
        });
    } else {
        menu->addAction(i18nc("@action:inmenu", "Pin to Dock"), this, [this, index]() {
            m_actions->togglePinned(index);
        });
    }

    // New Instance
    menu->addAction(i18nc("@action:inmenu", "New Instance"), this, [this, index]() {
        m_actions->newInstance(index);
    });

    // Close (only for running windows)
    if (isWindow) {
        menu->addSeparator();
        menu->addAction(i18nc("@action:inmenu", "Close"), this, [this, index]() {
            m_actions->closeTask(index);
        });
    }

    // Settings
    menu->addSeparator();
    menu->addAction(i18nc("@action:inmenu", "Settings..."), this, [this]() {
        Q_EMIT settingsRequested();
    });

    // Standard KDE actions
    menu->addSeparator();
    menu->addAction(i18nc("@action:inmenu", "About Krema"), this, [this]() {
        Q_EMIT aboutRequested();
    });
    menu->addAction(i18nc("@action:inmenu", "Quit"), qApp, &QApplication::quit);

    // Track menu visibility for interaction lock (dock stays visible while menu is open)
    Q_EMIT visibleChanged(true);
    connect(menu, &QMenu::aboutToHide, this, [this]() {
        Q_EMIT visibleChanged(false);
    });

    menu->popup(QCursor::pos());
}

} // namespace krema
