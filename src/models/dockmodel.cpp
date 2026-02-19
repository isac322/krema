// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <QIcon>
#include <QUrl>

namespace krema
{

DockModel::DockModel(QObject *parent)
    : QObject(parent)
    , m_tasksModel(std::make_unique<TaskManager::TasksModel>(this))
{
    // TasksModel implements QQmlParserStatus. When created from C++ (not QML),
    // classBegin/componentComplete are not called by the QML engine.
    // We must call them manually to trigger internal model initialization.
    m_tasksModel->classBegin();

    // Configure for dock-style behavior:
    // - Group windows by application (pinned + running merged)
    // - Manual sort for drag reordering
    // - Hide activated launchers (avoid duplicates: pinned + running)
    m_tasksModel->setGroupMode(TaskManager::TasksModel::GroupApplications);
    m_tasksModel->setSortMode(TaskManager::TasksModel::SortManual);
    m_tasksModel->setHideActivatedLaunchers(true);
    m_tasksModel->setSeparateLaunchers(false);
    m_tasksModel->setLaunchInPlace(true);
    m_tasksModel->setGroupInline(false);
    m_tasksModel->setTaskReorderingEnabled(true);

    // Activate internal source models (launcher model, window model, etc.)
    m_tasksModel->componentComplete();
}

DockModel::~DockModel() = default;

TaskManager::TasksModel *DockModel::tasksModel() const
{
    return m_tasksModel.get();
}

QStringList DockModel::pinnedLaunchers() const
{
    return m_tasksModel->launcherList();
}

void DockModel::setPinnedLaunchers(const QStringList &launchers)
{
    m_tasksModel->setLauncherList(launchers);
    Q_EMIT pinnedLaunchersChanged();
}

QString DockModel::iconName(int index) const
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }

    const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
    return icon.name();
}

void DockModel::activate(int index)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const bool isLauncher = idx.data(TaskManager::AbstractTasksModel::IsLauncher).toBool();
    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();

    if (isWindow) {
        m_tasksModel->requestActivate(idx);
    } else if (isLauncher) {
        m_tasksModel->requestNewInstance(idx);
    }
}

void DockModel::newInstance(int index)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (idx.isValid()) {
        m_tasksModel->requestNewInstance(idx);
    }
}

void DockModel::closeTask(int index)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (idx.isValid()) {
        m_tasksModel->requestClose(idx);
    }
}

void DockModel::togglePinned(int index)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const QUrl launcherUrl = idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    if (launcherUrl.isValid()) {
        if (m_tasksModel->launcherList().contains(launcherUrl.toString())) {
            m_tasksModel->requestRemoveLauncher(launcherUrl);
        } else {
            m_tasksModel->requestAddLauncher(launcherUrl);
        }
        Q_EMIT pinnedLaunchersChanged();
    }
}

void DockModel::activateNextTask()
{
    const QModelIndex active = m_tasksModel->activeTask();
    int startIndex = active.isValid() ? active.row() : -1;
    int next = findAdjacentRunningTask(startIndex, true);
    if (next >= 0) {
        m_tasksModel->requestActivate(m_tasksModel->index(next, 0));
    }
}

void DockModel::activatePreviousTask()
{
    const QModelIndex active = m_tasksModel->activeTask();
    int startIndex = active.isValid() ? active.row() : m_tasksModel->rowCount();
    int prev = findAdjacentRunningTask(startIndex, false);
    if (prev >= 0) {
        m_tasksModel->requestActivate(m_tasksModel->index(prev, 0));
    }
}

int DockModel::findAdjacentRunningTask(int fromIndex, bool forward) const
{
    const int count = m_tasksModel->rowCount();
    if (count == 0) {
        return -1;
    }

    for (int i = 1; i <= count; ++i) {
        int candidate = forward ? (fromIndex + i) % count : (fromIndex - i + count) % count;
        const QModelIndex idx = m_tasksModel->index(candidate, 0);
        const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
        if (isWindow) {
            return candidate;
        }
    }

    return -1;
}

} // namespace krema
