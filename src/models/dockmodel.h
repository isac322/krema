// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QUrl>

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
    Q_PROPERTY(QStringList pinnedLaunchers READ pinnedLaunchers WRITE setPinnedLaunchers NOTIFY pinnedLaunchersChanged)

public:
    explicit DockModel(QObject *parent = nullptr);
    ~DockModel() override;

    [[nodiscard]] TaskManager::TasksModel *tasksModel() const;
    [[nodiscard]] TaskManager::VirtualDesktopInfo *virtualDesktopInfo() const;
    [[nodiscard]] TaskManager::ActivityInfo *activityInfo() const;

    [[nodiscard]] QStringList pinnedLaunchers() const;
    void setPinnedLaunchers(const QStringList &launchers);

    /// Return the icon theme name for the task at @p index.
    Q_INVOKABLE QString iconName(int index) const;

    /// Activate the task at @p index. If it's a launcher, launch it.
    Q_INVOKABLE void activate(int index);

    /// Request a new instance of the app at @p index.
    Q_INVOKABLE void newInstance(int index);

    /// Close all windows for the task at @p index.
    Q_INVOKABLE void closeTask(int index);

    /// Toggle pinned state for the task at @p index.
    Q_INVOKABLE void togglePinned(int index);

    /// Show the native context menu for the task at @p index.
    Q_INVOKABLE void showContextMenu(int index);

    /// Cycle through child windows of the grouped task at @p index.
    /// When hovering an app icon with multiple windows, wheel scrolls
    /// switch focus between that app's windows (not between different apps).
    Q_INVOKABLE void cycleWindows(int index, bool forward);

    // --- Drag and Drop ---

    /// Move the task at @p fromIndex to @p toIndex. Returns true on success.
    /// Calls syncLaunchers() internally to persist the new order.
    Q_INVOKABLE bool moveTask(int fromIndex, int toIndex);

    /// Add a pinned launcher from a URL (.desktop file or applications: URL).
    Q_INVOKABLE bool addLauncher(const QUrl &url);

    /// Open the given URLs with the application at @p index.
    Q_INVOKABLE void openUrlsWithTask(int index, const QList<QUrl> &urls);

    /// Return the launcher URL for the task at @p index.
    Q_INVOKABLE QUrl launcherUrl(int index) const;

    /// Check whether the given URL refers to a .desktop file or applications: scheme.
    Q_INVOKABLE bool isDesktopFile(const QUrl &url) const;

    /// Return true if the task at @p index is a pinned launcher.
    Q_INVOKABLE bool isPinned(int index) const;

    /// Remove the pinned launcher at @p index. Returns true on success.
    Q_INVOKABLE bool removeLauncher(int index);

    // --- Window Preview ---

    /// Return window IDs (UUIDs on Wayland) for the task at @p index.
    Q_INVOKABLE QVariantList windowIds(int index) const;

    /// Return the number of child windows for the task at @p index.
    Q_INVOKABLE int childCount(int index) const;

    /// Return a QModelIndex for use as DelegateModel.rootIndex.
    Q_INVOKABLE QModelIndex taskModelIndex(int index) const;

Q_SIGNALS:
    void pinnedLaunchersChanged();
    void taskLaunching(int index);
    void settingsRequested();
    void aboutRequested();
    void contextMenuVisibleChanged(bool visible);

private:
    std::unique_ptr<TaskManager::TasksModel> m_tasksModel;
    std::shared_ptr<TaskManager::VirtualDesktopInfo> m_virtualDesktopInfo;
    std::shared_ptr<TaskManager::ActivityInfo> m_activityInfo;
};

} // namespace krema
