// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>

#include <taskmanager/tasksmodel.h>

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

    /// Activate the next running task (for mouse wheel cycling).
    Q_INVOKABLE void activateNextTask();

    /// Activate the previous running task (for mouse wheel cycling).
    Q_INVOKABLE void activatePreviousTask();

Q_SIGNALS:
    void pinnedLaunchersChanged();

private:
    /// Find the next/previous running (non-launcher-only) task index.
    int findAdjacentRunningTask(int fromIndex, bool forward) const;

    std::unique_ptr<TaskManager::TasksModel> m_tasksModel;
};

} // namespace krema
