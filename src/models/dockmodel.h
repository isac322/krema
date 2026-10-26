// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QUrl>

#include "dockitemsmodel.h"
#include "trashmanager.h"

#include <taskmanager/activityinfo.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/virtualdesktopinfo.h>

#include <memory>

namespace krema
{

/**
 * Central model for the dock's task manager section.
 *
 * Wraps libtaskmanager's TasksModel and configures it for dock usage:
 * grouped by application, with pinned launchers + running windows merged.
 * Exposes the model to QML and provides action methods.
 */
class DockModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(TaskManager::TasksModel *tasksModel READ tasksModel CONSTANT)
    Q_PROPERTY(DockItemsModel *items READ items CONSTANT)
    Q_PROPERTY(TrashManager *trashManager READ trashManager CONSTANT)
    Q_PROPERTY(QStringList pinnedLaunchers READ pinnedLaunchers WRITE setPinnedLaunchers NOTIFY pinnedLaunchersChanged)
    Q_PROPERTY(int pinnedTaskCount READ pinnedTaskCount NOTIFY pinnedTaskCountChanged)
    Q_PROPERTY(int trashSlot READ trashSlot WRITE setTrashSlot NOTIFY trashSlotChanged)
    Q_PROPERTY(int virtualDesktopMode READ virtualDesktopMode WRITE setVirtualDesktopMode NOTIFY virtualDesktopModeChanged)
    Q_PROPERTY(QVariant currentDesktop READ currentDesktop NOTIFY currentDesktopChanged)

public:
    explicit DockModel(QObject *parent = nullptr);
    ~DockModel() override;

    [[nodiscard]] TaskManager::TasksModel *tasksModel() const;
    [[nodiscard]] DockItemsModel *items() const;
    [[nodiscard]] TrashManager *trashManager() const;
    [[nodiscard]] TaskManager::VirtualDesktopInfo *virtualDesktopInfo() const;
    [[nodiscard]] TaskManager::ActivityInfo *activityInfo() const;

    [[nodiscard]] QStringList pinnedLaunchers() const;
    void setPinnedLaunchers(const QStringList &launchers);

    /// Keep pinned applications (including their windows) before unpinned tasks.
    [[nodiscard]] bool separateLaunchers() const;
    void setSeparateLaunchers(bool separate);
    [[nodiscard]] int pinnedTaskCount() const;

    /// Move source TasksModel rows. Callers holding visual rows must map them
    /// with taskIndex() before calling this source-only method.
    bool moveTask(int fromIndex, int toIndex);

    /// Virtual desktop display mode: 0=ShowAll, 1=DimOtherDesktops, 2=CurrentOnly
    [[nodiscard]] int virtualDesktopMode() const;
    void setVirtualDesktopMode(int mode);

    /// Identity of the virtual desktop the dock is currently showing.
    [[nodiscard]] QVariant currentDesktop() const;

    [[nodiscard]] int trashSlot() const;
    void setTrashSlot(int slot);

    /// QML-facing row conversion: visual dock row to source task row.
    /// Returns -1 for Trash or an invalid visual row.
    Q_INVOKABLE int taskIndex(int visualIndex) const;
    Q_INVOKABLE bool isTrash(int visualIndex) const;
    Q_INVOKABLE int trashIndex() const;
    /// Source-only conversion: source task row to visual dock row.
    Q_INVOKABLE int visualIndex(int taskIndex) const;

    /// The following index-taking methods accept visual dock rows from QML.
    /// They map to TasksModel rows internally and reject the Trash row.
    /// Check task-only C++ callers use taskModelIndex() or taskIndex() before
    /// accessing TasksModel directly.
    /// Check if the task at @p index is on the current virtual desktop.
    Q_INVOKABLE bool isOnCurrentDesktop(int index) const;

    /// Return the theme name or opaque raw source key for the task icon at @p index.
    Q_INVOKABLE QString iconName(int index) const;

    /// Return the launcher URL for the task at @p index.
    Q_INVOKABLE QUrl launcherUrl(int index) const;

    /// Check whether the given URL refers to a .desktop file or applications: scheme.
    Q_INVOKABLE bool isDesktopFile(const QUrl &url) const;

    /// Return true if the task at @p index is a pinned launcher.
    Q_INVOKABLE bool isPinned(int index) const;

    /// Return window IDs (UUIDs on Wayland) for the task at @p index.
    Q_INVOKABLE QVariantList windowIds(int index) const;

    /// Return the number of child windows for the task at @p index.
    Q_INVOKABLE int childCount(int index) const;

    /// Return a source QModelIndex for the visual task row at @p index.
    Q_INVOKABLE QModelIndex taskModelIndex(int index) const;

    /// Publish the delegate geometry for the visual task row at @p index.
    Q_INVOKABLE void publishDelegateGeometry(int index, QObject *delegate);

    /// Return the TaskManager/KService desktop-entry or executable identity for
    /// the task at @p index (not necessarily the raw Wayland app_id).
    Q_INVOKABLE QString appId(int index) const;

Q_SIGNALS:
    void pinnedLaunchersChanged();
    void pinnedTaskCountChanged();
    void trashSlotChanged();
    void virtualDesktopModeChanged();
    void currentDesktopChanged();

private:
    /// Identity of a row in the last settled dock order. A pinned row is
    /// matched by launcher (its launcher or task row), any other row by a
    /// shared window ID, or by AppId when it has no window.
    struct SettledRow {
        QUrl launcherUrl;
        QStringList windowIds;
        QString appId;
    };

    /// A current row, resolved once per launcher-slot pass.
    struct CurrentRow {
        int launcherPosition = -1;
        bool isLauncher = false;
        QStringList windowIds;
        QString appId;
    };

    void scheduleTaskPartition();
    void reconcileTaskPartition();
    bool placeStartedTasksInLauncherSlots();
    [[nodiscard]] static int findRow(const QList<CurrentRow> &rows, const SettledRow &settled, int settledLauncherPosition, int excludedRow);
    void snapshotSettledRows();

    bool m_separateLaunchers = false;
    bool m_partitionPending = false;
    bool m_reconcilingPartition = false;
    int m_pinnedTaskCount = 0;
    int m_trashSlot = -1;
    int m_virtualDesktopMode = 0;
    QList<SettledRow> m_settledRows;
    std::unique_ptr<TaskManager::TasksModel> m_tasksModel;
    std::unique_ptr<TrashManager> m_trashManager;
    std::unique_ptr<DockItemsModel> m_itemsModel;
    std::shared_ptr<TaskManager::VirtualDesktopInfo> m_virtualDesktopInfo;
    std::shared_ptr<TaskManager::ActivityInfo> m_activityInfo;
};

} // namespace krema
