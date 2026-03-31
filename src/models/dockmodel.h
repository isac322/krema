// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <memory>

#include <QModelIndex>
#include <QObject>
#include <QUrl>

#include <taskmanager/tasksmodel.h>

#include "itaskssource.h"

namespace TaskManager
{
class ActivityInfo;
class VirtualDesktopInfo;
}

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
    Q_PROPERTY(QStringList pinnedLaunchers READ pinnedLaunchers WRITE setPinnedLaunchers NOTIFY pinnedLaunchersChanged)
    Q_PROPERTY(int virtualDesktopMode READ virtualDesktopMode WRITE setVirtualDesktopMode NOTIFY virtualDesktopModeChanged)
    Q_PROPERTY(QVariant currentDesktop READ currentDesktop NOTIFY currentDesktopChanged)

public:
    explicit DockModel(TaskManager::TasksModel *tasksModel,
                       TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                       TaskManager::ActivityInfo *activityInfo,
                       QObject *parent = nullptr);
    ~DockModel() override;

    [[nodiscard]] TaskManager::TasksModel *tasksModel() const;
    [[nodiscard]] ITasksSource *tasksSource() const;

    /**
     * Replace the internal ITasksSource.
     *
     * Intended for unit tests that inject a MockTasksSource.
     * Production code should not call this after construction.
     */
    void setTasksSource(std::unique_ptr<ITasksSource> source);
    [[nodiscard]] TaskManager::VirtualDesktopInfo *virtualDesktopInfo() const;
    [[nodiscard]] TaskManager::ActivityInfo *activityInfo() const;

    [[nodiscard]] QStringList pinnedLaunchers() const;
    void setPinnedLaunchers(const QStringList &launchers);

    /// Virtual desktop display mode: 0=ShowAll, 1=DimOtherDesktops, 2=CurrentOnly
    [[nodiscard]] int virtualDesktopMode() const;
    void setVirtualDesktopMode(int mode);

    [[nodiscard]] QVariant currentDesktop() const;

    /// Check if the task at @p index is on the current virtual desktop.
    /// Returns true for launchers, tasks on all desktops, and tasks on current desktop.
    Q_INVOKABLE bool isOnCurrentDesktop(int index) const;

    /// Return the icon theme name for the task at @p index.
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

    /// Return a QModelIndex for use as DelegateModel.rootIndex.
    Q_INVOKABLE QModelIndex taskModelIndex(int index) const;

    /// Return the desktop entry name (AppId) for the task at @p index.
    Q_INVOKABLE QString appId(int index) const;

Q_SIGNALS:
    void pinnedLaunchersChanged();
    void virtualDesktopModeChanged();
    void currentDesktopChanged();

private:
    void configureTasksModel();

    int m_virtualDesktopMode = 0;
    TaskManager::TasksModel *m_tasksModel = nullptr;
    TaskManager::VirtualDesktopInfo *m_virtualDesktopInfo = nullptr;
    TaskManager::ActivityInfo *m_activityInfo = nullptr;
    std::unique_ptr<ITasksSource> m_tasksSource;
};

} // namespace krema
