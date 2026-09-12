#include "KWinTaskProvider.hpp"
#include "../../core/tasks/IdentityBridge.hpp"
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/activityinfo.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/virtualdesktopinfo.h>

namespace Krema
{

extern bool g_debugGeom;

KWinTaskProvider::KWinTaskProvider(QObject *parent)
    : ITaskProvider(parent)
    , m_tasksModel(new TaskManager::TasksModel(this))
    , m_virtualDesktopInfo(std::make_unique<TaskManager::VirtualDesktopInfo>(this))
    , m_activityInfo(std::make_unique<TaskManager::ActivityInfo>(this))
{
    // CRITICAL: Manual lifecycle for Wayland source activation
    m_tasksModel->classBegin();

    m_tasksModel->setGroupMode(TaskManager::TasksModel::GroupApplications);
    m_tasksModel->setSortMode(TaskManager::TasksModel::SortManual);

    // Force discovery by disabling all filters initially
    m_tasksModel->setFilterByVirtualDesktop(false);
    m_tasksModel->setFilterByActivity(false);
    m_tasksModel->setFilterByScreen(false);
    m_tasksModel->setFilterHidden(false);

    // Reference Project Insight: Dock-style behavior flags
    m_tasksModel->setHideActivatedLaunchers(true);
    m_tasksModel->setSeparateLaunchers(true);
    m_tasksModel->setLaunchInPlace(true);
    m_tasksModel->setGroupInline(false);

    // Bind VirtualDesktopInfo and ActivityInfo BEFORE componentComplete
    m_tasksModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
    m_tasksModel->setActivity(m_activityInfo->currentActivity());

    if (auto *screen = QGuiApplication::primaryScreen()) {
        m_tasksModel->setScreenGeometry(screen->geometry());
    }

    // M5.1: Default launchers for "Fully Built" feel
    m_tasksModel->setLauncherList({QStringLiteral("applications:org.kde.dolphin.desktop"),
                                   QStringLiteral("applications:org.kde.konsole.desktop"),
                                   QStringLiteral("applications:systemsettings.desktop")});

    m_tasksModel->componentComplete();

    connect(m_tasksModel, &QAbstractItemModel::dataChanged, this, &KWinTaskProvider::onModelDataChanged);
    connect(m_tasksModel, &QAbstractItemModel::rowsInserted, this, &KWinTaskProvider::onModelDataChanged);
    connect(m_tasksModel, &QAbstractItemModel::rowsRemoved, this, &KWinTaskProvider::onModelDataChanged);
    connect(m_tasksModel, &QAbstractItemModel::modelReset, this, &KWinTaskProvider::onModelDataChanged);

    // Initial check
    QTimer::singleShot(1000, this, [this]() {
        if (g_debugGeom) {
            qDebug() << "[TASK PROVIDER] Startup Check. Row Count:" << m_tasksModel->rowCount();
        }
        if (m_tasksModel->rowCount() == 0) {
            // Re-trigger model to poke KWin
            m_tasksModel->setVirtualDesktop(QString());
            emit tasksChanged();
        }
    });
}

KWinTaskProvider::~KWinTaskProvider()
{
}

QList<ITaskProvider::TaskEntry> KWinTaskProvider::tasks() const
{
    QList<TaskEntry> taskList;

    for (int i = 0; i < m_tasksModel->rowCount(); ++i) {
        QModelIndex index = m_tasksModel->index(i, 0);

        TaskEntry entry;
        QString rawId = m_tasksModel->data(index, TaskManager::AbstractTasksModel::AppId).toString();
        QUrl launcherUrl = m_tasksModel->data(index, TaskManager::AbstractTasksModel::LauncherUrl).toUrl();
        entry.launcherUrl = launcherUrl.toString();

        // THE IDENTITY BRIDGE (Rule 9: Porting Armor)
        // Normalize the ID to prevent Ghost Icons (launcher vs running app).
        entry.id = IdentityBridge::normalizeAppId(rawId);

        // Unique ID Fallback: Use LauncherUrl if AppId is still empty
        if (entry.id.isEmpty()) {
            entry.id = IdentityBridge::normalizeAppId(entry.launcherUrl);
        }

        entry.name = m_tasksModel->data(index, TaskManager::AbstractTasksModel::AppName).toString();

        // Better Icon Resolution using the Armor candidates
        entry.icon = IdentityBridge::resolveBestIcon(rawId, launcherUrl, entry.name);

        entry.isRunning = m_tasksModel->data(index, TaskManager::AbstractTasksModel::IsWindow).toBool();
        entry.winCount = m_tasksModel->data(index, TaskManager::AbstractTasksModel::ChildCount).toUInt();
        entry.isUrgent = m_tasksModel->data(index, TaskManager::AbstractTasksModel::IsDemandingAttention).toBool();
        entry.isPinned = m_tasksModel->data(index, TaskManager::AbstractTasksModel::IsLauncher).toBool();

        if (g_debugGeom) {
            qDebug() << "[TASK] Normalized ID:" << entry.id << "| Original:" << rawId << "| Icon:" << entry.icon;
        }

        taskList.append(entry);
    }

    return taskList;
}

void KWinTaskProvider::activateTask(const QString &id)
{
    for (int i = 0; i < m_tasksModel->rowCount(); ++i) {
        QModelIndex index = m_tasksModel->index(i, 0);
        QString appId = m_tasksModel->data(index, TaskManager::AbstractTasksModel::AppId).toString();
        QString normalizedId = IdentityBridge::normalizeAppId(appId);
        QString launcherUrl = m_tasksModel->data(index, TaskManager::AbstractTasksModel::LauncherUrl).toUrl().toString();

        if (normalizedId == id || appId == id || (appId.isEmpty() && launcherUrl == id)) {
            m_tasksModel->requestActivate(index);
            return;
        }
    }
}

void KWinTaskProvider::launchTask(const QString &id)
{
    for (int i = 0; i < m_tasksModel->rowCount(); ++i) {
        QModelIndex index = m_tasksModel->index(i, 0);
        QString appId = m_tasksModel->data(index, TaskManager::AbstractTasksModel::AppId).toString();
        QString normalizedId = IdentityBridge::normalizeAppId(appId);
        QString launcherUrl = m_tasksModel->data(index, TaskManager::AbstractTasksModel::LauncherUrl).toUrl().toString();

        if (normalizedId == id || appId == id || (appId.isEmpty() && launcherUrl == id)) {
            m_tasksModel->requestNewInstance(index);
            return;
        }
    }
}

void KWinTaskProvider::setPinned(const QString &id, bool pinned)
{
    Q_UNUSED(id);
    Q_UNUSED(pinned);
}

void KWinTaskProvider::setActiveWorkspace(const QString &workspaceId)
{
    if (m_activeWorkspaceId != workspaceId) {
        m_activeWorkspaceId = workspaceId;

        // Filter by the specific desktop
        m_tasksModel->setFilterByVirtualDesktop(!workspaceId.isEmpty());
        m_tasksModel->setVirtualDesktop(workspaceId);

        emit tasksChanged();
    }
}

void KWinTaskProvider::onModelDataChanged()
{
    emit tasksChanged();
}

} // namespace Krema
