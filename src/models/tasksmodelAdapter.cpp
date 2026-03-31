// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "tasksmodelAdapter.h"

#include <taskmanager/tasksmodel.h>

#include <QModelIndex>

namespace krema
{

TasksModelAdapter::TasksModelAdapter(TaskManager::TasksModel *model)
    : m_model(model)
{
    Q_ASSERT(m_model);
}

int TasksModelAdapter::rowCount() const
{
    return m_model->rowCount();
}

int TasksModelAdapter::childCount(int row) const
{
    const QModelIndex idx = m_model->index(row, 0);
    if (!idx.isValid()) {
        return 0;
    }
    return m_model->rowCount(idx);
}

QVariant TasksModelAdapter::data(int row, int role) const
{
    const QModelIndex idx = m_model->index(row, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(role);
}

QVariant TasksModelAdapter::childData(int row, int childRow, int role) const
{
    const QModelIndex idx = m_model->makeModelIndex(row, childRow);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(role);
}

QStringList TasksModelAdapter::launcherList() const
{
    return m_model->launcherList();
}

bool TasksModelAdapter::requestAddLauncher(const QUrl &url)
{
    return m_model->requestAddLauncher(url);
}

bool TasksModelAdapter::requestRemoveLauncher(const QUrl &url)
{
    return m_model->requestRemoveLauncher(url);
}

void TasksModelAdapter::requestActivate(int row)
{
    const QModelIndex idx = m_model->index(row, 0);
    if (idx.isValid()) {
        m_model->requestActivate(idx);
    }
}

void TasksModelAdapter::requestActivateChild(int row, int childRow)
{
    const QModelIndex idx = m_model->makeModelIndex(row, childRow);
    if (idx.isValid()) {
        m_model->requestActivate(idx);
    }
}

void TasksModelAdapter::requestNewInstance(int row)
{
    const QModelIndex idx = m_model->index(row, 0);
    if (idx.isValid()) {
        m_model->requestNewInstance(idx);
    }
}

void TasksModelAdapter::requestClose(int row)
{
    const QModelIndex idx = m_model->index(row, 0);
    if (idx.isValid()) {
        m_model->requestClose(idx);
    }
}

void TasksModelAdapter::requestOpenUrls(int row, const QList<QUrl> &urls)
{
    const QModelIndex idx = m_model->index(row, 0);
    if (idx.isValid() && !urls.isEmpty()) {
        m_model->requestOpenUrls(idx, urls);
    }
}

bool TasksModelAdapter::move(int fromRow, int toRow)
{
    return m_model->move(fromRow, toRow);
}

void TasksModelAdapter::syncLaunchers()
{
    m_model->syncLaunchers();
}

} // namespace krema
