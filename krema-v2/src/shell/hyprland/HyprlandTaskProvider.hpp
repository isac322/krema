#pragma once

#include "../../core/tasks/ITaskProvider.hpp"
#include "krema_shell_hyprland_export.h"

namespace Krema
{

/**
 * @brief Hyprland implementation of ITaskProvider.
 * Communicates with Hyprland via IPC/Sockets.
 */
class KREMA_SHELL_HYPRLAND_EXPORT HyprlandTaskProvider : public ITaskProvider
{
    Q_OBJECT
public:
    explicit HyprlandTaskProvider(QObject *parent = nullptr);
    ~HyprlandTaskProvider() override;

    QList<TaskEntry> tasks() const override;
    void setActiveWorkspace(const QString &workspaceId) override;
    void activateTask(const QString &id) override;
    void launchTask(const QString &id) override;
    void setPinned(const QString &id, bool pinned) override;

private:
    void refreshTasks();

    QList<TaskEntry> m_tasks;
    QString m_activeWorkspaceId;
};

} // namespace Krema
