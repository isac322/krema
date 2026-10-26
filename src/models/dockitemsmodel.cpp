// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockitemsmodel.h"

#include "trashmanager.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <KLocalizedString>

#include <QIcon>
#include <QMetaObject>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace krema
{

namespace
{
const QList<QByteArray> falseTrashRoles{
    QByteArrayLiteral("IsActive"),
    QByteArrayLiteral("IsDemandingAttention"),
    QByteArrayLiteral("IsFullScreen"),
    QByteArrayLiteral("IsGroupParent"),
    QByteArrayLiteral("IsLauncher"),
    QByteArrayLiteral("IsMaximized"),
    QByteArrayLiteral("IsMinimized"),
    QByteArrayLiteral("IsOnAllVirtualDesktops"),
    QByteArrayLiteral("IsStartup"),
    QByteArrayLiteral("IsTask"),
    QByteArrayLiteral("IsWindow"),
    QByteArrayLiteral("HasLauncher"),
};
}

DockItemsModel::DockItemsModel(TaskManager::TasksModel *tasksModel, TrashManager *trashManager, QObject *parent)
    : QAbstractListModel(parent)
    , m_tasksModel(tasksModel)
    , m_trashManager(trashManager)
{
    refreshRoleNames();

    if (m_tasksModel) {
        connect(m_tasksModel, &QAbstractItemModel::rowsInserted, this, &DockItemsModel::scheduleRebuild);
        connect(m_tasksModel, &QAbstractItemModel::rowsRemoved, this, &DockItemsModel::scheduleRebuild);
        connect(m_tasksModel, &QAbstractItemModel::rowsMoved, this, &DockItemsModel::scheduleRebuild);
        connect(m_tasksModel, &QAbstractItemModel::layoutChanged, this, &DockItemsModel::scheduleRebuild);
        connect(m_tasksModel, &QAbstractItemModel::modelReset, this, [this] {
            refreshRoleNames();
            scheduleRebuild();
        });
        connect(m_tasksModel, &TaskManager::TasksModel::launcherListChanged, this, &DockItemsModel::scheduleRebuild);
        connect(m_tasksModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
            if (topLeft.parent().isValid() || bottomRight.parent().isValid()) {
                return;
            }
            if (roles.isEmpty() || roles.contains(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon)
                || roles.contains(TaskManager::AbstractTasksModel::LauncherUrl) || roles.contains(TaskManager::AbstractTasksModel::HasLauncher)
                || roles.contains(TaskManager::AbstractTasksModel::IsGroupParent)) {
                scheduleRebuild();
            }
            const int first = std::max(0, topLeft.row());
            const int last = std::min(bottomRight.row(), m_tasksModel->rowCount() - 1);
            for (int sourceRow = first; sourceRow <= last; ++sourceRow) {
                const int visualRow = visualIndex(sourceRow);
                if (visualRow >= 0) {
                    Q_EMIT dataChanged(index(visualRow), index(visualRow), roles);
                }
            }
        });
    }

    if (m_trashManager) {
        connect(m_trashManager, &TrashManager::itemCountChanged, this, &DockItemsModel::emitTrashDataChanged);
        connect(m_trashManager, &TrashManager::emptyChanged, this, &DockItemsModel::emitTrashDataChanged);
        connect(m_trashManager, &TrashManager::levelChanged, this, &DockItemsModel::emitTrashDataChanged);
        connect(m_trashManager, &TrashManager::iconNameChanged, this, &DockItemsModel::emitTrashDataChanged);
    }

    rebuild();
}

int DockItemsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_visualRows.size();
}

QVariant DockItemsModel::data(const QModelIndex &modelIndex, int role) const
{
    if (!modelIndex.isValid() || modelIndex.row() < 0 || modelIndex.row() >= m_visualRows.size()) {
        return {};
    }

    const int sourceRow = m_visualRows.at(modelIndex.row());
    if (sourceRow >= 0) {
        if (role == ItemKindRole) {
            return Task;
        }
        if (role == SourceIndexRole) {
            return sourceRow;
        }
        return m_tasksModel->data(m_tasksModel->index(sourceRow, 0), role);
    }

    if (role == ItemKindRole) {
        return Trash;
    }
    if (role == SourceIndexRole) {
        return -1;
    }
    if (role == Qt::DisplayRole) {
        return i18n("Trash");
    }
    if (role == Qt::DecorationRole) {
        const QString name = m_trashManager ? m_trashManager->iconName() : QStringLiteral("user-trash");
        return QIcon::fromTheme(name.isEmpty() ? QStringLiteral("user-trash") : name);
    }

    const QByteArray roleName = m_roleNames.value(role);
    if (falseTrashRoles.contains(roleName)) {
        return false;
    }
    if (roleName == QByteArrayLiteral("ChildCount")) {
        return 0;
    }
    if (roleName == QByteArrayLiteral("WinIdList") || roleName == QByteArrayLiteral("VirtualDesktops")) {
        return QVariantList{};
    }

    return {};
}

QHash<int, QByteArray> DockItemsModel::roleNames() const
{
    return m_roleNames;
}

int DockItemsModel::taskIndex(int visualIndex) const
{
    const int sourceRow = sourceRowAt(visualIndex);
    return sourceRow >= 0 ? sourceRow : -1;
}

bool DockItemsModel::isTrash(int visualIndex) const
{
    return sourceRowAt(visualIndex) < 0 && visualIndex >= 0 && visualIndex < m_visualRows.size();
}

int DockItemsModel::trashIndex() const
{
    return m_visualRows.indexOf(-1);
}

int DockItemsModel::visualIndex(int taskIndex) const
{
    if (taskIndex < 0) {
        return -1;
    }
    return m_visualRows.indexOf(taskIndex);
}

int DockItemsModel::trashSlot() const
{
    return m_trashSlot;
}

void DockItemsModel::setTrashSlot(int slot)
{
    int normalized = std::max(-1, slot);
    if (normalized >= 0 && m_tasksModel) {
        normalized = std::min(normalized, static_cast<int>(m_tasksModel->launcherList().size()));
    }
    if (m_trashSlot == normalized) {
        return;
    }
    m_trashSlot = normalized;
    scheduleRebuild();
}

void DockItemsModel::scheduleRebuild()
{
    if (m_rebuildPending) {
        return;
    }
    m_rebuildPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_rebuildPending = false;
            rebuild();
        },
        Qt::QueuedConnection);
}

void DockItemsModel::rebuild()
{
    QVector<int> rows;
    const int sourceCount = m_tasksModel ? m_tasksModel->rowCount() : 0;
    rows.reserve(sourceCount + 1);

    if (m_tasksModel) {
        int pinnedCount = 0;
        for (int sourceRow = 0; sourceRow < sourceCount; ++sourceRow) {
            if (isPinnedSourceRow(sourceRow)) {
                ++pinnedCount;
            }
        }

        const int target = m_trashSlot < 0 ? pinnedCount : std::min(m_trashSlot, pinnedCount);
        int seenPinned = 0;
        bool inserted = false;
        // With no pinned rows there is no pinned-coordinate boundary before
        // the task rows; keep the source rows unchanged and append Trash.
        if (pinnedCount > 0 && target == 0) {
            rows.append(-1);
            inserted = true;
        }

        for (int sourceRow = 0; sourceRow < sourceCount; ++sourceRow) {
            rows.append(sourceRow);
            if (isPinnedSourceRow(sourceRow)) {
                ++seenPinned;
                if (!inserted && seenPinned >= target) {
                    rows.append(-1);
                    inserted = true;
                }
            }
        }

        if (!inserted) {
            rows.append(-1);
        }
    } else {
        rows.append(-1);
    }

    if (rows == m_visualRows) {
        // Equal row numbers do not guarantee the same source task (a launcher
        // can become a window in place). Refresh task ranges without targeting
        // the Trash row, while avoiding an unnecessary model reset.
        int taskRangeStart = -1;
        for (int visualRow = 0; visualRow <= m_visualRows.size(); ++visualRow) {
            const bool isTask = visualRow < m_visualRows.size() && m_visualRows.at(visualRow) >= 0;
            if (isTask) {
                if (taskRangeStart < 0) {
                    taskRangeStart = visualRow;
                }
                continue;
            }
            if (taskRangeStart >= 0) {
                Q_EMIT dataChanged(index(taskRangeStart), index(visualRow - 1), {});
                taskRangeStart = -1;
            }
        }
        return;
    }

    beginResetModel();
    m_visualRows = std::move(rows);
    endResetModel();
}

void DockItemsModel::refreshRoleNames()
{
    m_roleNames = m_tasksModel ? m_tasksModel->roleNames() : QHash<int, QByteArray>{};
    m_roleNames.insert(ItemKindRole, QByteArrayLiteral("ItemKind"));
    m_roleNames.insert(SourceIndexRole, QByteArrayLiteral("SourceIndex"));
}

void DockItemsModel::emitTrashDataChanged()
{
    const int row = trashIndex();
    if (row < 0) {
        return;
    }
    QList<int> roles = m_roleNames.keys();
    roles << Qt::DisplayRole << Qt::DecorationRole;
    Q_EMIT dataChanged(index(row), index(row), roles);
}

bool DockItemsModel::isPinnedSourceRow(int sourceRow) const
{
    if (!m_tasksModel) {
        return false;
    }
    const QModelIndex sourceIndex = m_tasksModel->index(sourceRow, 0);
    if (!sourceIndex.isValid()) {
        return false;
    }
    const QUrl launcherUrl = sourceIndex.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    return launcherUrl.isValid() && m_tasksModel->launcherPosition(launcherUrl) != -1;
}

int DockItemsModel::sourceRowAt(int visualIndex) const
{
    if (visualIndex < 0 || visualIndex >= m_visualRows.size()) {
        return -1;
    }
    return m_visualRows.at(visualIndex);
}

} // namespace krema
