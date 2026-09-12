// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockactions.h"

#include "dockmodel.h"
#include "hyprlandtasksmodel.h"
#include "utils/identitymanager.h"

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
    if (m_model->isHyprland()) {
        m_model->hyprTasksModel()->requestActivate(index);
    } else {
        auto *tasksModel = m_model->kdeTasksModel();
        const QModelIndex idx = tasksModel->index(index, 0);
        if (!idx.isValid())
            return;

        const bool isLauncher = idx.data(TaskManager::AbstractTasksModel::IsLauncher).toBool();
        const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();

        if (isWindow) {
            tasksModel->requestActivate(idx);
        } else if (isLauncher) {
            tasksModel->requestNewInstance(idx);
            Q_EMIT taskLaunching(index);
        }
    }
}

void DockActions::newInstance(int index)
{
    if (m_model->isHyprland()) {
        m_model->hyprTasksModel()->requestNewInstance(index);
        Q_EMIT taskLaunching(index);
    } else {
        auto *tasksModel = m_model->kdeTasksModel();
        const QModelIndex idx = tasksModel->index(index, 0);
        if (idx.isValid()) {
            tasksModel->requestNewInstance(idx);
            Q_EMIT taskLaunching(index);
        }
    }
}

void DockActions::closeTask(int index)
{
    if (m_model->isHyprland()) {
        m_model->hyprTasksModel()->requestClose(index);
    } else {
        auto *tasksModel = m_model->kdeTasksModel();
        const QModelIndex idx = tasksModel->index(index, 0);
        if (idx.isValid()) {
            tasksModel->requestClose(idx);
        }
    }
}

void DockActions::togglePinned(int index)
{
    if (m_model->isHyprland()) {
        const QUrl url = m_model->launcherUrl(index);
        if (url.isValid()) {
            if (m_model->pinnedLaunchers().contains(url.toString())) {
                removeLauncher(index);
            } else {
                addLauncher(url);
            }
        }
    } else {
        auto *tasksModel = m_model->kdeTasksModel();
        const QModelIndex idx = tasksModel->index(index, 0);
        if (!idx.isValid())
            return;

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
}

void DockActions::cycleWindows(int index, bool forward)
{
    if (m_model->isHyprland()) {
        m_model->hyprTasksModel()->requestCycle(index, forward);
    } else {
        auto *tasksModel = m_model->kdeTasksModel();
        const QModelIndex idx = tasksModel->index(index, 0);
        if (!idx.isValid())
            return;

        const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
        if (!isWindow)
            return;

        const int childCount = tasksModel->rowCount(idx);
        if (childCount <= 1) {
            tasksModel->requestActivate(idx);
            return;
        }

        int activeChild = -1;
        for (int i = 0; i < childCount; ++i) {
            const QModelIndex child = tasksModel->makeModelIndex(index, i);
            if (child.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
                activeChild = i;
                break;
            }
        }

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
}

bool DockActions::moveTask(int fromIndex, int toIndex)
{
    if (m_model->isHyprland()) {
        auto list = m_model->pinnedLaunchers();
        if (fromIndex >= 0 && fromIndex < list.size() && toIndex >= 0 && toIndex < list.size()) {
            if (fromIndex == toIndex)
                return false;
            QString item = list.takeAt(fromIndex);
            list.insert(toIndex, item);
            m_model->setPinnedLaunchers(list);
            Q_EMIT pinnedLaunchersChanged();
            return true;
        }
        return false;
    }

    auto *tasksModel = m_model->kdeTasksModel();
    if (fromIndex == toIndex)
        return false;
    if (fromIndex < 0 || fromIndex >= tasksModel->rowCount())
        return false;
    if (toIndex < 0 || toIndex >= tasksModel->rowCount())
        return false;

    const bool ok = tasksModel->move(fromIndex, toIndex);
    if (ok) {
        tasksModel->syncLaunchers();
        Q_EMIT pinnedLaunchersChanged();
    }
    return ok;
}

bool DockActions::addLauncher(const QUrl &url)
{
    if (!url.isValid())
        return false;

    if (m_model->isHyprland()) {
        QUrl canonical = IdentityManager::canonicalLauncherUrl(url);
        auto list = m_model->pinnedLaunchers();
        if (!list.contains(canonical.toString())) {
            list.append(canonical.toString());
            m_model->setPinnedLaunchers(list);
            Q_EMIT pinnedLaunchersChanged();
            return true;
        }
        return false;
    }

    const auto appData = TaskManager::appDataFromUrl(url);
    if (appData.id.isEmpty())
        return false;

    const QUrl resolvedUrl = appData.url.isValid() ? appData.url : url;
    const bool ok = m_model->kdeTasksModel()->requestAddLauncher(resolvedUrl);
    if (ok) {
        Q_EMIT pinnedLaunchersChanged();
    }
    return ok;
}

bool DockActions::removeLauncher(int index)
{
    if (m_model->isHyprland()) {
        auto list = m_model->pinnedLaunchers();
        if (index >= 0 && index < list.size()) {
            list.removeAt(index);
            m_model->setPinnedLaunchers(list);
            Q_EMIT pinnedLaunchersChanged();
            return true;
        }
        return false;
    }

    auto *tasksModel = m_model->kdeTasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (!idx.isValid())
        return false;

    const QUrl url = idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    if (!url.isValid())
        return false;

    const bool ok = tasksModel->requestRemoveLauncher(url);
    if (ok) {
        Q_EMIT pinnedLaunchersChanged();
    }
    return ok;
}

void DockActions::openUrlsWithTask(int index, const QList<QUrl> &urls)
{
    if (m_model->isHyprland()) {
        // TODO
    } else {
        auto *tasksModel = m_model->kdeTasksModel();
        const QModelIndex idx = tasksModel->index(index, 0);
        if (!idx.isValid() || urls.isEmpty())
            return;
        tasksModel->requestOpenUrls(idx, urls);
    }
}

} // namespace krema
