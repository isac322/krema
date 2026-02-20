// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>

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

Q_SIGNALS:
    void pinnedLaunchersChanged();
    void taskLaunching(int index);

private:
    std::unique_ptr<TaskManager::TasksModel> m_tasksModel;
    std::shared_ptr<TaskManager::VirtualDesktopInfo> m_virtualDesktopInfo;
    std::shared_ptr<TaskManager::ActivityInfo> m_activityInfo;
};

} // namespace krema
