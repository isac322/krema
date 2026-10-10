// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "qmltestmocks.h"

#include "utils/zoomlayoutvariant.h"

#include <QMetaEnum>
#include <QQmlEngine>

namespace KremaQmlTest
{

MockTasksModel::MockTasksModel(QObject *parent)
    : QStandardItemModel(parent)
{
    m_roleNames.insert(Qt::DisplayRole, QByteArrayLiteral("display"));
    const QMetaEnum roles = QMetaEnum::fromType<MockAbstractTasksModel::AdditionalRoles>();
    for (int i = 0; i < roles.keyCount(); ++i) {
        m_roleNames.insert(roles.value(i), QByteArray(roles.key(i)));
    }
    connect(this, &QAbstractItemModel::rowsInserted, this, &MockTasksModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &MockTasksModel::countChanged);
}

QHash<int, QByteArray> MockTasksModel::roleNames() const
{
    return m_roleNames;
}

int MockTasksModel::count() const
{
    return rowCount();
}

int MockTasksModel::activateRequests() const
{
    return m_activateRequests;
}

int MockTasksModel::closeRequests() const
{
    return m_closeRequests;
}

int MockTasksModel::lastRequestRow() const
{
    return m_lastRequestRow;
}

int MockTasksModel::lastRequestChild() const
{
    return m_lastRequestChild;
}

int MockTasksModel::roleForName(const QString &name) const
{
    const QByteArray key = name.toUtf8();
    for (auto it = m_roleNames.cbegin(); it != m_roleNames.cend(); ++it) {
        if (it.value() == key) {
            return it.key();
        }
    }
    qWarning("MockTasksModel: unknown role name '%s'", key.constData());
    return -1;
}

void MockTasksModel::applyRoles(QStandardItem *item, const QVariantMap &roles) const
{
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        const int role = roleForName(it.key());
        if (role >= 0) {
            item->setData(it.value(), role);
        }
    }
}

int MockTasksModel::addTask(const QVariantMap &roles)
{
    auto *item = new QStandardItem;
    applyRoles(item, roles);
    appendRow(item);
    return item->row();
}

int MockTasksModel::addChildTask(int parentRow, const QVariantMap &roles)
{
    QStandardItem *parentItem = item(parentRow);
    if (!parentItem) {
        qWarning("MockTasksModel: addChildTask on missing row %d", parentRow);
        return -1;
    }
    auto *child = new QStandardItem;
    applyRoles(child, roles);
    parentItem->appendRow(child);
    return child->row();
}

void MockTasksModel::removeChildTask(int parentRow, int childRow)
{
    QStandardItem *parentItem = item(parentRow);
    if (!parentItem) {
        qWarning("MockTasksModel: removeChildTask on missing row %d", parentRow);
        return;
    }
    parentItem->removeRow(childRow);
}

void MockTasksModel::setTaskData(int row, const QString &roleName, const QVariant &value)
{
    const int role = roleForName(roleName);
    if (role >= 0) {
        setData(index(row, 0), value, role);
    }
}

void MockTasksModel::removeTask(int row)
{
    removeRow(row);
}

void MockTasksModel::reset()
{
    removeRows(0, rowCount());
    m_activateRequests = 0;
    m_closeRequests = 0;
    m_lastRequestRow = -1;
    m_lastRequestChild = -1;
    Q_EMIT requestsChanged();
}

QVariant MockTasksModel::get(int row, const QString &roleName) const
{
    const int role = roleForName(roleName);
    return role >= 0 ? data(index(row, 0), role) : QVariant();
}

QModelIndex MockTasksModel::makeModelIndex(int row, int childRow) const
{
    const QModelIndex parentIndex = index(row, 0);
    return childRow < 0 ? parentIndex : index(childRow, 0, parentIndex);
}

void MockTasksModel::recordRequest(const QModelIndex &index)
{
    if (index.parent().isValid()) {
        m_lastRequestRow = index.parent().row();
        m_lastRequestChild = index.row();
    } else {
        m_lastRequestRow = index.row();
        m_lastRequestChild = -1;
    }
}

void MockTasksModel::requestActivate(const QModelIndex &index)
{
    ++m_activateRequests;
    recordRequest(index);
    Q_EMIT requestsChanged();
}

void MockTasksModel::requestClose(const QModelIndex &index)
{
    ++m_closeRequests;
    recordRequest(index);
    Q_EMIT requestsChanged();
}

void MockScreencastingRequest::setUuid(const QString &uuid)
{
    if (m_uuid != uuid) {
        m_uuid = uuid;
        Q_EMIT uuidChanged();
    }
}

QVariantMap ZoomLayoutEngine::zoomLayout(int count,
                                         qreal restStart,
                                         qreal iconSize,
                                         qreal spacing,
                                         int boundary,
                                         qreal boundaryGap,
                                         qreal restBackgroundStart,
                                         qreal restBackgroundEnd,
                                         qreal maxZoomFactor,
                                         int style,
                                         bool active,
                                         qreal cursor,
                                         qreal minEdge,
                                         qreal maxEdge) const
{
    // The production conversion, as DockView::zoomLayout uses it.
    return krema::zoomLayoutVariant(count,
                                    restStart,
                                    iconSize,
                                    spacing,
                                    boundary,
                                    boundaryGap,
                                    restBackgroundStart,
                                    restBackgroundEnd,
                                    maxZoomFactor,
                                    style,
                                    active,
                                    cursor,
                                    minEdge,
                                    maxEdge);
}

void registerMockTypes()
{
    // The mocks/ import path carries a type-less qmldir for these URIs, which
    // shadows the real (installed) plugins so only these registrations are used.
    qmlRegisterUncreatableType<MockAbstractTasksModel>("org.kde.taskmanager", 0, 1, "AbstractTasksModel", QStringLiteral("Role enum holder"));
    qmlRegisterType<MockScreencastingRequest>("org.kde.taskmanager", 0, 1, "ScreencastingRequest");
    qmlRegisterType<MockPipeWireSourceItem>("org.kde.pipewire", 0, 1, "PipeWireSourceItem");
    qmlRegisterType<MockTasksModel>("krema.test", 1, 0, "MockTasksModel");
    qmlRegisterSingletonType<ZoomLayoutEngine>("krema.test", 1, 0, "ZoomLayoutEngine", [](QQmlEngine *, QJSEngine *) -> QObject * {
        return new ZoomLayoutEngine;
    });
}

} // namespace KremaQmlTest
