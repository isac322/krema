// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

namespace krema::test
{

class NotificationServerPeer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationManager")

public:
    explicit NotificationServerPeer(QObject *parent = nullptr);
    ~NotificationServerPeer() override;

    bool start();
    void stop();
    bool isRegistered() const;
    int registerWatcherCalls() const;
    int unregisterWatcherCalls() const;

    bool sendNotify(const QString &appName,
                    uint id,
                    uint replacesId = 0,
                    const QString &desktopEntry = {},
                    const QString &summary = QStringLiteral("Fixture summary"),
                    const QString &body = QStringLiteral("Fixture body"),
                    const QStringList &actions = {},
                    int timeout = -1,
                    uint urgency = 0) const;
    bool sendClose(uint id) const;
    bool broadcastClose(uint id, uint reason) const;

public Q_SLOTS:
    Q_SCRIPTABLE void RegisterWatcher();
    Q_SCRIPTABLE void UnRegisterWatcher();

private:
    QDBusConnection m_bus;
    bool m_serviceRegistered = false;
    bool m_objectRegistered = false;
    int m_registerWatcherCalls = 0;
    int m_unregisterWatcherCalls = 0;
};

class StatusNotifierWatcherPeer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems NOTIFY registeredItemsChanged)

public:
    explicit StatusNotifierWatcherPeer(QObject *parent = nullptr);
    ~StatusNotifierWatcherPeer() override;

    bool start();
    void stop();
    bool isRegistered() const;
    void setRegisteredItems(const QStringList &items);
    void addItem(const QString &item);
    void removeItem(const QString &item);
    QStringList registeredItems() const;

Q_SIGNALS:
    void StatusNotifierItemRegistered(const QString &item);
    void StatusNotifierItemUnregistered(const QString &item);
    void registeredItemsChanged();

private:
    QDBusConnection m_bus;
    QStringList m_registeredItems;
    bool m_serviceRegistered = false;
    bool m_objectRegistered = false;
};

class StatusNotifierItemPeer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString Status READ status WRITE setStatus NOTIFY statusChanged)

public:
    explicit StatusNotifierItemPeer(QString serviceName, QString objectPath = QStringLiteral("/StatusNotifierItem"), QObject *parent = nullptr);
    ~StatusNotifierItemPeer() override;

    bool start();
    void stop();
    bool isRegistered() const;
    QString serviceName() const;
    QString objectPath() const;
    QString itemAddress() const;
    QString id() const;
    QString status() const;
    void setId(const QString &id);
    void setStatus(const QString &status);

Q_SIGNALS:
    void NewStatus(const QString &status);
    void idChanged();
    void statusChanged();

private:
    QDBusConnection m_bus;
    QString m_serviceName;
    QString m_objectPath;
    QString m_id;
    QString m_status = QStringLiteral("Passive");
    bool m_serviceRegistered = false;
    bool m_objectRegistered = false;
};

} // namespace krema::test
