// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "kdetasksproxymodel.h"
#include <taskmanager/abstracttasksmodel.h>

namespace krema
{

KdeTasksProxyModel::KdeTasksProxyModel(QObject *parent)
    : QIdentityProxyModel(parent)
{
}

KdeTasksProxyModel::~KdeTasksProxyModel() = default;

void KdeTasksProxyModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    if (this->sourceModel()) {
        disconnect(this->sourceModel(), &QAbstractItemModel::dataChanged, this, &KdeTasksProxyModel::onSourceDataChanged);
    }

    QIdentityProxyModel::setSourceModel(sourceModel);

    if (this->sourceModel()) {
        connect(this->sourceModel(), &QAbstractItemModel::dataChanged, this, &KdeTasksProxyModel::onSourceDataChanged);
    }
}

QVariant KdeTasksProxyModel::data(const QModelIndex &proxyIndex, int role) const
{
    if (role == ActiveChildIndexRole) {
        if (!sourceModel() || !proxyIndex.isValid()) {
            return -1;
        }

        QModelIndex srcIndex = mapToSource(proxyIndex);
        int count = sourceModel()->rowCount(srcIndex);

        if (count == 0) {
            if (srcIndex.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
                return 0;
            }
            return -1;
        }

        for (int i = 0; i < count; ++i) {
            QModelIndex child = sourceModel()->index(i, 0, srcIndex);
            if (child.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
                return i;
            }
        }
        return -1;
    }

    return QIdentityProxyModel::data(proxyIndex, role);
}

QHash<int, QByteArray> KdeTasksProxyModel::roleNames() const
{
    QHash<int, QByteArray> roles = QIdentityProxyModel::roleNames();
    roles[ActiveChildIndexRole] = "ActiveChildIndex";
    return roles;
}

void KdeTasksProxyModel::onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
{
    // Always notify the parent that ActiveChildIndex might have changed.
    // The window manager might activate a window and trigger a dataChanged event
    // without explicitly including TaskManager::AbstractTasksModel::IsActive in the roles list.
    if (topLeft.parent().isValid()) {
        QModelIndex parent = topLeft.parent();
        QModelIndex proxyParent = mapFromSource(parent);
        if (proxyParent.isValid()) {
            Q_EMIT dataChanged(proxyParent, proxyParent, {ActiveChildIndexRole});
        }
    } else {
        // If it's a top-level single window changing state
        if (roles.isEmpty() || roles.contains(TaskManager::AbstractTasksModel::IsActive)) {
            QModelIndex proxyTop = mapFromSource(topLeft);
            QModelIndex proxyBottom = mapFromSource(bottomRight);
            if (proxyTop.isValid() && proxyBottom.isValid()) {
                Q_EMIT dataChanged(proxyTop, proxyBottom, {ActiveChildIndexRole});
            }
        }
    }
}

} // namespace krema
