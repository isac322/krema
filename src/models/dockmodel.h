// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QUrl>

#include <memory>

namespace TaskManager
{
class ActivityInfo;
class TasksModel;
class VirtualDesktopInfo;
}

namespace krema
{

class HyprlandTasksModel;
class KdeTasksProxyModel;
class BaseIsland;

/**
 * Central model for the dock's task manager section.
 *
 * Provides a platform-agnostic interface for the dock's task list.
 * Internally delegates to either libtaskmanager (KDE) or HyprlandTasksModel.
 */
class DockModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *tasksModel READ tasksModel CONSTANT)
    Q_PROPERTY(QStringList pinnedLaunchers READ pinnedLaunchers WRITE setPinnedLaunchers NOTIFY pinnedLaunchersChanged)
    Q_PROPERTY(int virtualDesktopMode READ virtualDesktopMode WRITE setVirtualDesktopMode NOTIFY virtualDesktopModeChanged)
    Q_PROPERTY(QVariant currentDesktop READ currentDesktop NOTIFY currentDesktopChanged)
    Q_PROPERTY(QVariantList islandsVariant READ islandsVariant NOTIFY islandsChanged)

public:
    Q_INVOKABLE QString iconName(int index) const;

    explicit DockModel(QObject *parent = nullptr);
    ~DockModel() override;

    [[nodiscard]] QAbstractItemModel *tasksModel() const;
    [[nodiscard]] TaskManager::VirtualDesktopInfo *virtualDesktopInfo() const;
    [[nodiscard]] TaskManager::ActivityInfo *activityInfo() const;

    [[nodiscard]] QStringList pinnedLaunchers() const;
    void setPinnedLaunchers(const QStringList &launchers);

    /// Virtual desktop display mode: 0=ShowAll, 1=DimOtherDesktops, 2=CurrentOnly
    [[nodiscard]] int virtualDesktopMode() const;
    void setVirtualDesktopMode(int mode);

    [[nodiscard]] QVariant currentDesktop() const;
    [[nodiscard]] QVariantList islandsVariant() const;

    /// Check if the task at @p index is on the current virtual desktop.
    Q_INVOKABLE bool isOnCurrentDesktop(int index) const;

    /// Return the raw icon data (QIcon) for the task at @p index.
    Q_INVOKABLE QVariant iconData(int index) const;

    /// Return the launcher URL for the task at @p index.
    Q_INVOKABLE QUrl launcherUrl(int index) const;

    /// Check whether the given URL refers to a .desktop file or applications: scheme.
    Q_INVOKABLE bool isDesktopFile(const QUrl &url) const;

    /// Return true if the task at @p index is a pinned launcher.
    Q_INVOKABLE bool isPinned(int index) const;

    /// Return the index of the last pinned/launcher item.
    Q_INVOKABLE int pinnedBoundaryIndex() const;

    /// Return window IDs (UUIDs on Wayland / Addresses on Hyprland) for the task at @p index.
    Q_INVOKABLE QVariantList windowIds(int index) const;

    /// Return the number of child windows for the task at @p index.
    Q_INVOKABLE int childCount(int index) const;

    /// Return a QModelIndex for use as DelegateModel.rootIndex.
    Q_INVOKABLE QModelIndex taskModelIndex(int index) const;

    /// Return the desktop entry name (AppId) for the task at @p index.
    Q_INVOKABLE QString appId(int index) const;

    // Internal helpers for DockActions
    [[nodiscard]] TaskManager::TasksModel *kdeTasksModel() const;
    [[nodiscard]] HyprlandTasksModel *hyprTasksModel() const;
    [[nodiscard]] bool isHyprland() const;

Q_SIGNALS:
    void pinnedLaunchersChanged();
    void virtualDesktopModeChanged();
    void currentDesktopChanged();
    void islandsChanged();

private:
    bool m_isHyprland = false;
    int m_virtualDesktopMode = 0;
    std::unique_ptr<TaskManager::TasksModel> m_kdeTasksModel;
    std::unique_ptr<KdeTasksProxyModel> m_kdeTasksProxyModel;
    std::unique_ptr<HyprlandTasksModel> m_hyprTasksModel;
    std::shared_ptr<TaskManager::VirtualDesktopInfo> m_virtualDesktopInfo;
    std::shared_ptr<TaskManager::ActivityInfo> m_activityInfo;
    std::unique_ptr<BaseIsland> m_appIsland;
};

} // namespace krema
