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

namespace
{
// DockActions is called with visual DockItemsModel rows from QML. Convert
// exactly once before using the task-only TasksModel API.
QModelIndex sourceIndexForVisual(DockModel *model, int visualIndex);
int entryChild(TaskManager::TasksModel *tasksModel, int sourceIndex, int childCount);
}

void DockActions::activate(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = sourceIndexForVisual(m_model, index);
    if (!idx.isValid()) {
        if (m_model->isTrash(index)) {
            m_model->trashManager()->openTrash();
        }
        return;
    }

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
    const QModelIndex idx = sourceIndexForVisual(m_model, index);
    if (idx.isValid()) {
        tasksModel->requestNewInstance(idx);
        Q_EMIT taskLaunching(index);
    }
}

void DockActions::closeTask(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = sourceIndexForVisual(m_model, index);
    if (idx.isValid()) {
        tasksModel->requestClose(idx);
    }
}

void DockActions::togglePinned(int index)
{
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = sourceIndexForVisual(m_model, index);
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

QModelIndex sourceIndexForVisual(DockModel *model, int visualIndex)
{
    const int sourceIndex = model->taskIndex(visualIndex);
    return sourceIndex >= 0 ? model->tasksModel()->index(sourceIndex, 0) : QModelIndex{};
}

// Child of the source task at @p sourceIndex to enter when none of its
// windows is active, following Plasma's task manager ordering.
int entryChild(TaskManager::TasksModel *tasksModel, int sourceIndex, int childCount)
{
    int best = -1;
    QDateTime bestActivated;
    for (int i = 0; i < childCount; ++i) {
        const QDateTime activated = tasksModel->makeModelIndex(sourceIndex, i).data(TaskManager::AbstractTasksModel::LastActivated).toDateTime();
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
        const int stacking = tasksModel->makeModelIndex(sourceIndex, i).data(TaskManager::AbstractTasksModel::StackingOrder).toInt(&ok);
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
    const int sourceIndex = m_model->taskIndex(index);
    const QModelIndex idx = sourceIndexForVisual(m_model, index);
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
        const QModelIndex child = tasksModel->makeModelIndex(sourceIndex, i);
        if (child.isValid() && child.data(TaskManager::AbstractTasksModel::IsWindow).toBool()
            && !child.data(TaskManager::AbstractTasksModel::IsGroupParent).toBool() && child.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            activeChild = i;
            break;
        }
    }

    if (activeChild < 0) {
        const int target = entryChild(tasksModel, sourceIndex, childCount);
        if (target < 0 || target >= childCount) {
            return;
        }
        const QModelIndex targetIdx = tasksModel->makeModelIndex(sourceIndex, target);
        if (targetIdx.isValid() && targetIdx.data(TaskManager::AbstractTasksModel::IsWindow).toBool()
            && !targetIdx.data(TaskManager::AbstractTasksModel::IsGroupParent).toBool()) {
            tasksModel->requestActivate(targetIdx);
        }
        return;
    }

    const QModelIndex activeIdx = tasksModel->makeModelIndex(sourceIndex, activeChild);
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
    const int sourceIndex = m_model->taskIndex(index);
    const QModelIndex idx = sourceIndexForVisual(m_model, index);
    if (!idx.isValid() || !idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool()) {
        return;
    }

    const int childCount = tasksModel->rowCount(idx);
    if (childCount <= 1) {
        tasksModel->requestActivate(idx);
        return;
    }

    int activeChild = -1;
    for (int i = 0; i < childCount; ++i) {
        const QModelIndex child = tasksModel->makeModelIndex(sourceIndex, i);
        if (child.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            activeChild = i;
            break;
        }
    }

    const int target = activeChild < 0 ? entryChild(tasksModel, sourceIndex, childCount)
                                      : (forward ? (activeChild + 1) % childCount : (activeChild - 1 + childCount) % childCount);
    const QModelIndex targetIdx = tasksModel->makeModelIndex(sourceIndex, target);
    if (targetIdx.isValid()) {
        tasksModel->requestActivate(targetIdx);
    }
}

bool DockActions::moveTask(int fromIndex, int toIndex)
{
    auto *tasksModel = m_model->tasksModel();
    const int fromSource = m_model->taskIndex(fromIndex);
    const int toSource = m_model->taskIndex(toIndex);
    if (fromSource < 0 || toSource < 0 || fromSource == toSource) {
        return false;
    }

    int targetSource = toSource;
    if (m_model->separateLaunchers()) {
        const auto pinnedAt = [tasksModel](int sourceIndex) {
            const QModelIndex idx = tasksModel->index(sourceIndex, 0);
            const QUrl url = idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
            return url.isValid() && tasksModel->launcherPosition(url) != -1;
        };
        const bool pinned = pinnedAt(fromSource);
        const int step = targetSource > fromSource ? -1 : 1;
        while (targetSource != fromSource && pinnedAt(targetSource) != pinned) {
            targetSource += step;
        }
        if (fromSource == targetSource) {
            return false;
        }
    }

    const bool ok = m_model->moveTask(fromSource, targetSource);
    if (ok) {
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
    const QModelIndex idx = sourceIndexForVisual(m_model, index);
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
    if (m_model->isTrash(index)) {
        if (!urls.isEmpty()) {
            m_model->trashManager()->trashUrls(urls);
        }
        return;
    }

    const QModelIndex idx = sourceIndexForVisual(m_model, index);
    if (!idx.isValid() || urls.isEmpty()) {
        return;
    }
    tasksModel->requestOpenUrls(idx, urls);
}

} // namespace krema
