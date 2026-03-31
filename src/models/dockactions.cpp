// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockactions.h"

#include "dockmodel.h"

#include <QLoggingCategory>
#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

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
    auto *source = m_model->tasksSource();
    if (index < 0 || index >= source->rowCount()) {
        return;
    }

    const bool isLauncher = source->data(index, TaskManager::AbstractTasksModel::IsLauncher).toBool();
    const bool isWindow = source->data(index, TaskManager::AbstractTasksModel::IsWindow).toBool();

    if (isWindow) {
        source->requestActivate(index);
    } else if (isLauncher) {
        source->requestNewInstance(index);
        Q_EMIT taskLaunching(index);
    }
}

void DockActions::newInstance(int index)
{
    auto *source = m_model->tasksSource();
    if (index < 0 || index >= source->rowCount()) {
        return;
    }

    source->requestNewInstance(index);
    Q_EMIT taskLaunching(index);
}

void DockActions::closeTask(int index)
{
    auto *source = m_model->tasksSource();
    if (index < 0 || index >= source->rowCount()) {
        return;
    }
    source->requestClose(index);
}

void DockActions::togglePinned(int index)
{
    auto *source = m_model->tasksSource();
    if (index < 0 || index >= source->rowCount()) {
        return;
    }

    const QUrl launcherUrl = source->data(index, TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    if (launcherUrl.isValid()) {
        const bool isPinned = source->launcherList().contains(launcherUrl.toString());
        const bool ok = isPinned ? source->requestRemoveLauncher(launcherUrl) : source->requestAddLauncher(launcherUrl);
        if (ok) {
            Q_EMIT pinnedLaunchersChanged();
        }
    }
}

void DockActions::cycleWindows(int index, bool forward)
{
    auto *source = m_model->tasksSource();
    if (index < 0 || index >= source->rowCount()) {
        return;
    }

    const bool isWindow = source->data(index, TaskManager::AbstractTasksModel::IsWindow).toBool();
    if (!isWindow) {
        return;
    }

    const int count = source->childCount(index);
    if (count <= 1) {
        // Single window or non-grouped: just activate/focus it
        source->requestActivate(index);
        return;
    }

    // Find the currently active child window
    int activeChild = -1;
    for (int i = 0; i < count; ++i) {
        if (source->childData(index, i, TaskManager::AbstractTasksModel::IsActive).toBool()) {
            activeChild = i;
            break;
        }
    }

    // Cycle to next/previous child
    int target = 0;
    if (activeChild >= 0) {
        target = forward ? (activeChild + 1) % count : (activeChild - 1 + count) % count;
    }

    source->requestActivateChild(index, target);
}

bool DockActions::moveTask(int fromIndex, int toIndex)
{
    auto *source = m_model->tasksSource();
    if (fromIndex == toIndex) {
        return false;
    }
    if (fromIndex < 0 || fromIndex >= source->rowCount()) {
        return false;
    }
    if (toIndex < 0 || toIndex >= source->rowCount()) {
        return false;
    }

    const bool ok = source->move(fromIndex, toIndex);
    if (ok) {
        source->syncLaunchers();
        Q_EMIT pinnedLaunchersChanged();
    }
    return ok;
}

bool DockActions::addLauncher(const QUrl &url)
{
    if (!url.isValid() || !m_model->isDesktopFile(url)) {
        return false;
    }

    const bool ok = m_model->tasksSource()->requestAddLauncher(url);
    if (ok) {
        Q_EMIT pinnedLaunchersChanged();
    }
    return ok;
}

bool DockActions::removeLauncher(int index)
{
    auto *source = m_model->tasksSource();
    if (index < 0 || index >= source->rowCount()) {
        return false;
    }

    const QUrl url = source->data(index, TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    if (!url.isValid()) {
        return false;
    }

    const bool ok = source->requestRemoveLauncher(url);
    if (ok) {
        Q_EMIT pinnedLaunchersChanged();
    }
    return ok;
}

void DockActions::openUrlsWithTask(int index, const QList<QUrl> &urls)
{
    auto *source = m_model->tasksSource();
    if (index < 0 || index >= source->rowCount() || urls.isEmpty()) {
        return;
    }

    source->requestOpenUrls(index, urls);
}

} // namespace krema
