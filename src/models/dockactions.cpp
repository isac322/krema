// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockactions.h"

#include "dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/tasktools.h>

#include <QDateTime>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcModel)

namespace krema
{

DockActions::DockActions(DockModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
}

void DockActions::activate(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    // TaskGroupingProxyModel::requestActivate() ignores group parents, so a
    // click on a grouped app would do nothing. Cycle like the wheel does,
    // which matches Plasma's default grouped-click action ("cycle through
    // grouped tasks"): while a window of the group is active, each click
    // activates the next one in model order, wrapping around. When none is
    // active, the most recently used window of the group is activated.
    if (idx.data(TaskManager::AbstractTasksModel::IsGroupParent).toBool()) {
        cycleWindows(index, true);
        return;
    }

    const bool isLauncher = idx.data(TaskManager::AbstractTasksModel::IsLauncher).toBool();
    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();

    if (isWindow) {
        tasksModel->requestActivate(idx);
    } else if (isLauncher) {
        tasksModel->requestNewInstance(idx);
        Q_EMIT taskLaunching(index);
    }
}

void DockActions::newInstance(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (idx.isValid()) {
        tasksModel->requestNewInstance(idx);
        Q_EMIT taskLaunching(index);
    }
}

void DockActions::closeTask(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (idx.isValid()) {
        tasksModel->requestClose(idx);
    }
}

void DockActions::togglePinned(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const QUrl launcherUrl = idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    if (launcherUrl.isValid()) {
        if (tasksModel->launcherPosition(launcherUrl) != -1) {
            tasksModel->requestRemoveLauncher(launcherUrl);
        } else {
            tasksModel->requestAddLauncher(launcherUrl);
        }
        Q_EMIT pinnedLaunchersChanged();
    }
}

namespace
{

// Child of the group at @p index to enter when none of its windows is active,
// following Plasma's task manager (TaskTools.js groupTopTask): the most
// recently activated window, else the topmost in the stacking order, else the
// first child.
int entryChild(TaskManager::TasksModel *tasksModel, int index, int childCount)
{
    int best = -1;
    QDateTime bestActivated;
    for (int i = 0; i < childCount; ++i) {
        const QDateTime activated = tasksModel->makeModelIndex(index, i).data(TaskManager::AbstractTasksModel::LastActivated).toDateTime();
        if (activated.isValid() && (!bestActivated.isValid() || activated > bestActivated)) {
            bestActivated = activated;
            best = i;
        }
    }
    if (best >= 0) {
        return best;
    }

    int bestStacking = -1;
    for (int i = 0; i < childCount; ++i) {
        bool ok = false;
        const int stacking = tasksModel->makeModelIndex(index, i).data(TaskManager::AbstractTasksModel::StackingOrder).toInt(&ok);
        if (ok && stacking > bestStacking) {
            bestStacking = stacking;
            best = i;
        }
    }
    return best >= 0 ? best : 0;
}

} // namespace

void DockActions::activateOrMinimize(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
    if (!isWindow) {
        activate(index);
        return;
    }

    if (!idx.data(TaskManager::AbstractTasksModel::IsGroupParent).toBool()) {
        const bool isActive = idx.data(TaskManager::AbstractTasksModel::IsActive).toBool();
        const bool isMinimized = idx.data(TaskManager::AbstractTasksModel::IsMinimized).toBool();
        const bool isMinimizable = idx.data(TaskManager::AbstractTasksModel::IsMinimizable).toBool();
        if (isActive && !isMinimized && isMinimizable) {
            tasksModel->requestToggleMinimized(idx);
        } else {
            activate(index);
        }
        return;
    }

    const int childCount = tasksModel->rowCount(idx);
    if (childCount <= 0) {
        return;
    }

    int activeChild = -1;
    for (int i = 0; i < childCount; ++i) {
        const QModelIndex child = tasksModel->makeModelIndex(index, i);
        if (child.isValid() && child.data(TaskManager::AbstractTasksModel::IsWindow).toBool()
            && !child.data(TaskManager::AbstractTasksModel::IsGroupParent).toBool() && child.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            activeChild = i;
            break;
        }
    }

    if (activeChild < 0) {
        const int target = entryChild(tasksModel, index, childCount);
        if (target < 0 || target >= childCount) {
            return;
        }
        const QModelIndex targetIdx = tasksModel->makeModelIndex(index, target);
        if (targetIdx.isValid() && targetIdx.data(TaskManager::AbstractTasksModel::IsWindow).toBool()
            && !targetIdx.data(TaskManager::AbstractTasksModel::IsGroupParent).toBool()) {
            tasksModel->requestActivate(targetIdx);
        }
        return;
    }

    const QModelIndex activeIdx = tasksModel->makeModelIndex(index, activeChild);
    if (!activeIdx.isValid() || !activeIdx.data(TaskManager::AbstractTasksModel::IsWindow).toBool()
        || activeIdx.data(TaskManager::AbstractTasksModel::IsGroupParent).toBool()) {
        return;
    }

    const bool isMinimized = activeIdx.data(TaskManager::AbstractTasksModel::IsMinimized).toBool();
    const bool isMinimizable = activeIdx.data(TaskManager::AbstractTasksModel::IsMinimizable).toBool();
    if (!isMinimized && isMinimizable) {
        tasksModel->requestToggleMinimized(activeIdx);
    } else {
        tasksModel->requestActivate(activeIdx);
    }
}

void DockActions::cycleWindows(int index, bool forward)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
    if (!isWindow) {
        return;
    }

    const int childCount = tasksModel->rowCount(idx);
    if (childCount <= 1) {
        // Single window or non-grouped: just activate/focus it
        tasksModel->requestActivate(idx);
        return;
    }

    // Find the currently active child window
    int activeChild = -1;
    for (int i = 0; i < childCount; ++i) {
        const QModelIndex child = tasksModel->makeModelIndex(index, i);
        if (child.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            activeChild = i;
            break;
        }
    }

    // Cycle to next/previous child. Entering a group whose windows are all
    // inactive lands on the window the user left, in either wheel direction;
    // only moves within the group follow the direction.
    int target;
    if (activeChild < 0) {
        target = entryChild(tasksModel, index, childCount);
    } else {
        target = forward ? (activeChild + 1) % childCount : (activeChild - 1 + childCount) % childCount;
    }

    const QModelIndex targetIdx = tasksModel->makeModelIndex(index, target);
    if (targetIdx.isValid()) {
        tasksModel->requestActivate(targetIdx);
    }
}

bool DockActions::moveTask(int fromIndex, int toIndex)
{
    auto *tasksModel = m_model->tasksModel();
    if (fromIndex == toIndex) {
        return false;
    }
    if (fromIndex < 0 || fromIndex >= tasksModel->rowCount()) {
        return false;
    }
    if (toIndex < 0 || toIndex >= tasksModel->rowCount()) {
        return false;
    }

    if (m_model->separateLaunchers()) {
        // Clamp a cross-zone drop to the nearest task in the source zone.
        // Query membership rather than a cached boundary so a pending
        // pin/unpin reconciliation cannot accidentally cross the boundary.
        const bool pinned = m_model->isPinned(fromIndex);
        const int step = toIndex > fromIndex ? -1 : 1;
        while (toIndex != fromIndex && m_model->isPinned(toIndex) != pinned) {
            toIndex += step;
        }
        if (fromIndex == toIndex) {
            return false;
        }
    }

    const bool ok = tasksModel->move(fromIndex, toIndex);
    if (ok) {
        tasksModel->syncLaunchers();
        Q_EMIT pinnedLaunchersChanged();
    }
    return ok;
}

bool DockActions::addLauncher(const QUrl &url)
{
    if (!url.isValid()) {
        return false;
    }

    // Validate that the URL resolves to a real application
    const auto appData = TaskManager::appDataFromUrl(url);
    if (appData.id.isEmpty()) {
        return false;
    }

    // Use the resolved URL (handles preferred:// and other special schemes)
    const QUrl resolvedUrl = appData.url.isValid() ? appData.url : url;
    const bool ok = m_model->tasksModel()->requestAddLauncher(resolvedUrl);
    if (ok) {
        Q_EMIT pinnedLaunchersChanged();
    }
    return ok;
}

bool DockActions::removeLauncher(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return false;
    }

    const QUrl url = idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    if (!url.isValid()) {
        return false;
    }

    const bool ok = tasksModel->requestRemoveLauncher(url);
    if (ok) {
        Q_EMIT pinnedLaunchersChanged();
    }
    return ok;
}

void DockActions::openUrlsWithTask(int index, const QList<QUrl> &urls)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (!idx.isValid() || urls.isEmpty()) {
        return;
    }
    tasksModel->requestOpenUrls(idx, urls);
}

} // namespace krema
