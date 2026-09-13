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

    const bool isLauncher = idx.data(TaskManager::AbstractTasksModel::IsLauncher).toBool();
    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();

    if (isWindow) {
        // Classic dock toggle behavior (mirrors Plasma's TaskTools.activateTask):
        // - Minimized window → unminimize and focus it.
        // - Active single window → minimize it.
        // - Grouped task (multiple windows) → cycle to the next instance.
        // - Otherwise → focus it.
        const bool isMinimized = idx.data(TaskManager::AbstractTasksModel::IsMinimized).toBool();
        const bool isActive = idx.data(TaskManager::AbstractTasksModel::IsActive).toBool();
        const bool isGroupParent = idx.data(TaskManager::AbstractTasksModel::IsGroupParent).toBool();

        if (isGroupParent && tasksModel->rowCount(idx) > 1) {
            // Grouped task: requestActivate on the group parent is a no-op when
            // one of its windows is already active — cycle instances instead.
            cycleWindows(index, true);
        } else if (isMinimized) {
            tasksModel->requestToggleMinimized(idx);
            tasksModel->requestActivate(idx);
        } else if (isActive && !isGroupParent) {
            tasksModel->requestToggleMinimized(idx);
        } else {
            tasksModel->requestActivate(idx);
        }
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
        if (tasksModel->launcherList().contains(launcherUrl.toString())) {
            tasksModel->requestRemoveLauncher(launcherUrl);
        } else {
            tasksModel->requestAddLauncher(launcherUrl);
        }
        Q_EMIT pinnedLaunchersChanged();
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

    // Find the currently active child window and the most recently used one.
    int activeChild = -1;
    int mruChild = 0;
    QDateTime mruTime;
    for (int i = 0; i < childCount; ++i) {
        const QModelIndex child = tasksModel->makeModelIndex(index, i);
        if (child.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            activeChild = i;
        }
        const QDateTime lastActivated = child.data(TaskManager::AbstractTasksModel::LastActivated).toDateTime();
        if (lastActivated.isValid() && (!mruTime.isValid() || lastActivated > mruTime)) {
            mruTime = lastActivated;
            mruChild = i;
        }
    }

    // Cycle to next/previous child. When no child is active (e.g. all
    // minimized or another app focused), start from the most recently
    // used instance instead of an arbitrary first row.
    int target;
    if (activeChild >= 0) {
        target = forward ? (activeChild + 1) % childCount : (activeChild - 1 + childCount) % childCount;
    } else {
        target = forward ? mruChild : (mruChild - 1 + childCount) % childCount;
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
