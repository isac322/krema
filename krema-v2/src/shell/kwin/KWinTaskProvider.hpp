#pragma once

#include "../../core/tasks/ITaskProvider.hpp"
#include "krema_shell_kwin_export.h"
#include <QPointer>
#include <memory>

namespace TaskManager
{
class TasksModel;
class VirtualDesktopInfo;
class ActivityInfo;
}

namespace Krema
{

/**
 * @brief KWin/Plasma implementation of ITaskProvider using LibTaskManager.
 */
class KREMA_SHELL_KWIN_EXPORT KWinTaskProvider : public ITaskProvider
{
    Q_OBJECT
public:
    explicit KWinTaskProvider(QObject *parent = nullptr);
    ~KWinTaskProvider() override;

    QList<TaskEntry> tasks() const override;
    void setActiveWorkspace(const QString &workspaceId) override;
    void activateTask(const QString &id) override;
    void launchTask(const QString &id) override;
    void setPinned(const QString &id, bool pinned) override;

private:
    void onModelDataChanged();

    TaskManager::TasksModel *m_tasksModel;
    std::unique_ptr<TaskManager::VirtualDesktopInfo> m_virtualDesktopInfo;
    std::unique_ptr<TaskManager::ActivityInfo> m_activityInfo;
    QString m_activeWorkspaceId;
};
} // namespace Krema
