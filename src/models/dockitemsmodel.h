// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

namespace TaskManager
{
class TasksModel;
}

namespace krema
{

class TrashManager;

class DockItemsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum ItemKind {
        Task = 0,
        Trash = 1,
    };
    Q_ENUM(ItemKind)

    static constexpr int ItemKindRole = Qt::UserRole + 0x1000;
    static constexpr int SourceIndexRole = Qt::UserRole + 0x1001;

    explicit DockItemsModel(TaskManager::TasksModel *tasksModel, TrashManager *trashManager, QObject *parent = nullptr);
    ~DockItemsModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Convert a visual dock row to its top-level TasksModel row.
    /// Returns -1 for Trash or an invalid visual row.
    [[nodiscard]] int taskIndex(int visualIndex) const;
    [[nodiscard]] bool isTrash(int visualIndex) const;
    [[nodiscard]] int trashIndex() const;
    /// Convert a top-level TasksModel row to its visual dock row.
    /// This method accepts source coordinates, never visual coordinates.
    [[nodiscard]] int visualIndex(int taskIndex) const;

    [[nodiscard]] int trashSlot() const;
    void setTrashSlot(int slot);

private:
    void scheduleRebuild();
    void rebuild();
    void refreshRoleNames();
    void emitTrashDataChanged();
    [[nodiscard]] bool isPinnedSourceRow(int sourceRow) const;
    [[nodiscard]] int sourceRowAt(int visualIndex) const;

    TaskManager::TasksModel *m_tasksModel = nullptr;
    TrashManager *m_trashManager = nullptr;
    QHash<int, QByteArray> m_roleNames;
    QVector<int> m_visualRows;
    int m_trashSlot = -1;
    bool m_rebuildPending = false;
};

} // namespace krema
