// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariant>
#include <QVariantMap>

namespace krema::test
{

class NotificationServerPeer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationManager")

public:
    explicit NotificationServerPeer(const QDBusConnection &bus = QDBusConnection::sessionBus(), QObject *parent = nullptr);
    ~NotificationServerPeer() override;

    bool start(bool replaceExisting = false);
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
    explicit StatusNotifierWatcherPeer(const QDBusConnection &bus = QDBusConnection::sessionBus(), QObject *parent = nullptr);
    ~StatusNotifierWatcherPeer() override;

    bool start(bool replaceExisting = false);
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
    Q_PROPERTY(QString Title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString AttentionIconName READ attentionIconName WRITE setAttentionIconName NOTIFY attentionIconNameChanged)
    Q_PROPERTY(QString OverlayIconName READ overlayIconName WRITE setOverlayIconName NOTIFY overlayIconNameChanged)
    Q_PROPERTY(QVariant ToolTip READ toolTip WRITE setToolTip NOTIFY toolTipChanged)
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
    QString title() const;
    QString attentionIconName() const;
    QString overlayIconName() const;
    QVariant toolTip() const;
    void setId(const QString &id);
    void setStatus(const QString &status);
    void setTitle(const QString &title);
    void setAttentionIconName(const QString &name);
    void setOverlayIconName(const QString &name);
    void setToolTip(const QVariant &toolTip);

Q_SIGNALS:
    void NewStatus(const QString &status);
    void NewTitle();
    void NewAttentionIcon();
    void NewOverlayIcon();
    void NewToolTip();
    void idChanged();
    void statusChanged();
    void titleChanged();
    void attentionIconNameChanged();
    void overlayIconNameChanged();
    void toolTipChanged();

private:
    QDBusConnection m_bus;
    QString m_serviceName;
    QString m_objectPath;
    QString m_id;
    QString m_status = QStringLiteral("Passive");
    QString m_title;
    QString m_attentionIconName;
    QString m_overlayIconName;
    QVariant m_toolTip;
    bool m_serviceRegistered = false;
    bool m_objectRegistered = false;
};

} // namespace krema::test
