// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockactions.h"

#include "dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/tasktools.h>

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

    // Find the currently active child window
    int activeChild = -1;
    for (int i = 0; i < childCount; ++i) {
        const QModelIndex child = tasksModel->makeModelIndex(index, i);
        if (child.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            activeChild = i;
            break;
        }
    }

    // Cycle to next/previous child
    int target;
    if (activeChild < 0) {
        target = 0;
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
