#pragma once

#include "krema_core_export.h"
#include <QObject>
#include <QStringList>
#include <QUrl>

namespace Krema
{

/**
 * @brief Abstract interface for application task management.
 * Provides the list of running and pinned applications to the dock.
 */
class KREMA_CORE_EXPORT ITaskProvider : public QObject
{
    Q_OBJECT
public:
    struct TaskEntry {
        QString id; ///< Unique ID (e.g., desktop file name)
        QString name; ///< Display name
        QString icon; ///< Icon name or path
        QString launcherUrl; ///< URL for pinning/launching
        QString workspaceId; ///< ID of the workspace this task belongs to
        bool isRunning; ///< True if there are open windows
        uint32_t winCount; ///< Number of open windows
        bool isUrgent; ///< True if any window needs attention
        bool isPinned; ///< True if the app is pinned to the dock

        bool operator==(const TaskEntry &other) const
        {
            return id == other.id && name == other.name && icon == other.icon && launcherUrl == other.launcherUrl && workspaceId == other.workspaceId
                && isRunning == other.isRunning && winCount == other.winCount && isUrgent == other.isUrgent && isPinned == other.isPinned;
        }
    };

    explicit ITaskProvider(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    virtual ~ITaskProvider() = default;

    /**
     * @brief Set the currently active workspace to filter tasks by.
     */
    virtual void setActiveWorkspace(const QString &workspaceId) = 0;

    /**
     * @brief Get the current list of tasks for the active workspace.
     */
    virtual QList<TaskEntry> tasks() const = 0;

    /**
     * @brief Request to activate (focus) a specific task.
     */
    virtual void activateTask(const QString &id) = 0;

    /**
     * @brief Request to launch a new instance of a task.
     */
    virtual void launchTask(const QString &id) = 0;

    /**
     * @brief Pin or unpin an application.
     */
    virtual void setPinned(const QString &id, bool pinned) = 0;

signals:
    /**
     * @brief Emitted whenever the task list changes (app opened, closed, pinned).
     */
    void tasksChanged();
};

} // namespace Krema
