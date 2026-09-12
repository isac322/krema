#include "HyprlandTaskProvider.hpp"
#include "../../core/tasks/IdentityBridge.hpp"
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace Krema
{

HyprlandTaskProvider::HyprlandTaskProvider(QObject *parent)
    : ITaskProvider(parent)
{
    // Initial refresh
    refreshTasks();

    // TODO: Subscribe to Hyprland socket2 for real-time updates
}

HyprlandTaskProvider::~HyprlandTaskProvider()
{
}

QList<ITaskProvider::TaskEntry> HyprlandTaskProvider::tasks() const
{
    return m_tasks;
}

void HyprlandTaskProvider::activateTask(const QString &id)
{
    // Correct format: hyprctl dispatch focuswindow address:0x...
    // The previous fix used 'address:' + id, but based on the error,
    // the system expects the ID itself. Let's send the ID directly.
    QProcess::startDetached(QStringLiteral("hyprctl"), {QStringLiteral("dispatch"), QStringLiteral("focuswindow"), id});
}

void HyprlandTaskProvider::launchTask(const QString &id)
{
    // Simplistic launch
    QProcess::startDetached(id);
}

void HyprlandTaskProvider::setPinned(const QString &id, bool pinned)
{
    Q_UNUSED(id);
    Q_UNUSED(pinned);
}

void HyprlandTaskProvider::setActiveWorkspace(const QString &workspaceId)
{
    if (m_activeWorkspaceId != workspaceId) {
        m_activeWorkspaceId = workspaceId;
        refreshTasks();
    }
}

// ...

void HyprlandTaskProvider::refreshTasks()
{
    QProcess process;
    process.start(QStringLiteral("hyprctl"), {QStringLiteral("clients"), QStringLiteral("-j")});
    if (!process.waitForFinished())
        return;

    QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (!doc.isArray())
        return;

    QList<TaskEntry> newTasks;
    QJsonArray clients = doc.array();

    for (const QJsonValue &val : clients) {
        QJsonObject client = val.toObject();

        QString workspaceName = client.value(QStringLiteral("workspace")).toObject().value(QStringLiteral("name")).toString();

        // Filter by workspace if set
        if (!m_activeWorkspaceId.isEmpty() && workspaceName != m_activeWorkspaceId) {
            continue;
        }

        TaskEntry entry;
        entry.id = client.value(QStringLiteral("address")).toString();
        entry.name = client.value(QStringLiteral("title")).toString();
        if (entry.name.isEmpty())
            entry.name = client.value(QStringLiteral("initialClass")).toString();
        entry.icon = client.value(QStringLiteral("initialClass")).toString().toLower();
        entry.workspaceId = workspaceName;
        entry.isRunning = true;
        entry.winCount = 1;
        entry.isUrgent = false;
        entry.isPinned = false;

        newTasks.append(entry);
    }

    if (m_tasks != newTasks) {
        m_tasks = newTasks;
        emit tasksChanged();
    }
}

} // namespace Krema
