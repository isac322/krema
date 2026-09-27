// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

// Test doubles for the C++-backed QML modules Krema's QML imports.
//
// Only types that cannot be expressed in plain QML live here:
//  - org.kde.taskmanager AbstractTasksModel (QML needs its role *enum*),
//  - a QAbstractItemModel standing in for DockModel.tasksModel
//    (Repeater model + index()/data()/makeModelIndex()),
//  - org.kde.pipewire PipeWireSourceItem (a QQuickItem).
// The com.bhyoo.krema singletons are plain QML files under mocks/.

#include <QQuickItem>
#include <QStandardItemModel>
#include <QVariantMap>

namespace KremaQmlTest
{

// Mirrors the role names of TaskManager::AbstractTasksModel that Krema's QML
// reads, plus a few mock-only roles (IconName, IsOnCurrentDesktop) that back
// the per-index DockModel lookups. Values are internal to the test double.
class MockAbstractTasksModel : public QObject
{
    Q_OBJECT
public:
    enum AdditionalRoles {
        AppId = Qt::UserRole + 1,
        IsWindow,
        IsStartup,
        IsLauncher,
        IsActive,
        IsMinimized,
        IsDemandingAttention,
        ChildCount,
        WinIdList,
        LauncherUrl,
        IconName,
        IsOnCurrentDesktop,
    };
    Q_ENUM(AdditionalRoles)
};

// Stand-in for TaskManager::TasksModel as exposed via DockModel.tasksModel.
// Rows are populated from QML with role-name keyed maps, e.g.
//   tasksModel.addTask({ display: "Dolphin", IsWindow: true, ChildCount: 2 })
class MockTasksModel : public QStandardItemModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activateRequests READ activateRequests NOTIFY requestsChanged)
    Q_PROPERTY(int closeRequests READ closeRequests NOTIFY requestsChanged)
    Q_PROPERTY(int lastRequestRow READ lastRequestRow NOTIFY requestsChanged)
    Q_PROPERTY(int lastRequestChild READ lastRequestChild NOTIFY requestsChanged)

public:
    explicit MockTasksModel(QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int activateRequests() const;
    int closeRequests() const;
    int lastRequestRow() const;
    int lastRequestChild() const;

    Q_INVOKABLE int addTask(const QVariantMap &roles);
    Q_INVOKABLE int addChildTask(int parentRow, const QVariantMap &roles);
    Q_INVOKABLE void setTaskData(int row, const QString &roleName, const QVariant &value);
    Q_INVOKABLE void removeTask(int row);
    Q_INVOKABLE void reset();
    // Role lookup by name, used by the QML DockModel mock.
    Q_INVOKABLE QVariant get(int row, const QString &roleName) const;

    // TaskManager::TasksModel API used by Krema's QML.
    Q_INVOKABLE QModelIndex makeModelIndex(int row, int childRow = -1) const;
    Q_INVOKABLE void requestActivate(const QModelIndex &index);
    Q_INVOKABLE void requestClose(const QModelIndex &index);

Q_SIGNALS:
    void countChanged();
    void requestsChanged();

private:
    int roleForName(const QString &name) const;
    void applyRoles(QStandardItem *item, const QVariantMap &roles) const;
    void recordRequest(const QModelIndex &index);

    QHash<int, QByteArray> m_roleNames;
    int m_activateRequests = 0;
    int m_closeRequests = 0;
    int m_lastRequestRow = -1;
    int m_lastRequestChild = -1;
};

class MockScreencastingRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString uuid READ uuid WRITE setUuid NOTIFY uuidChanged)
    Q_PROPERTY(quint32 nodeId READ nodeId NOTIFY nodeIdChanged)
public:
    using QObject::QObject;
    QString uuid() const
    {
        return m_uuid;
    }
    void setUuid(const QString &uuid);
    quint32 nodeId() const
    {
        return 0;
    }
Q_SIGNALS:
    void uuidChanged();
    void nodeIdChanged();

private:
    QString m_uuid;
};

class MockPipeWireSourceItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(quint32 nodeId MEMBER m_nodeId NOTIFY nodeIdChanged)
    Q_PROPERTY(bool allowDmaBuf MEMBER m_allowDmaBuf NOTIFY allowDmaBufChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
public:
    using QQuickItem::QQuickItem;
    bool ready() const
    {
        return false;
    }
    int state() const
    {
        return 0;
    }
Q_SIGNALS:
    void nodeIdChanged();
    void allowDmaBufChanged();
    void readyChanged();
    void stateChanged();

private:
    quint32 m_nodeId = 0;
    bool m_allowDmaBuf = false;
};

void registerMockTypes();

} // namespace KremaQmlTest
