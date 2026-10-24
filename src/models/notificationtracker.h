// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QMap>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <memory>

namespace NotificationManager
{
class Settings;
}

namespace krema
{

/**
 * Aggregates per-app notification state from multiple sources:
 *   1. D-Bus notification watcher — unread notification count per desktop entry
 *   2. SNI (StatusNotifierItem) D-Bus — NeedsAttention boolean per app
 *
 * Uses KDE's RegisterWatcher D-Bus protocol to receive all notifications
 * forwarded by the notification server (plasmashell).
 *
 * QML accesses state via Q_INVOKABLE methods. The `revision` property provides
 * reactive dependency: QML bindings that read `revision` are re-evaluated
 * whenever notification state changes.
 */
class NotificationTracker : public QAbstractListModel
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationWatcher")

    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool dndActive READ dndActive NOTIFY dndActiveChanged)

public:
    enum NotificationRole {
        IdRole = Qt::UserRole + 1,
        DesktopEntryRole,
        AppNameRole,
        AppIconRole,
        SummaryRole,
        BodyRole,
        ActionsRole,
        HintsRole,
        TimeoutRole,
        ReplacesIdRole,
        UrgencyRole,
        ActiveRole,
        ReadRole,
        DismissedRole,
        CloseReasonRole,
        BroadcastReasonRole,
        CreatedAtRole,
        UpdatedAtRole,
    };
    Q_ENUM(NotificationRole)

    explicit NotificationTracker(QObject *parent = nullptr);
    ~NotificationTracker() override;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int revision() const;
    [[nodiscard]] bool dndActive() const;

    /// Return a snapshot of retained notifications, optionally filtered by app and active state.
    [[nodiscard]] Q_INVOKABLE QVariantList notifications(const QString &desktopEntry = {}, bool activeOnly = false) const;

    /// Mark a notification read locally; this does not update the originating application.
    [[nodiscard]] Q_INVOKABLE bool markRead(uint id);

    /// Mark a notification dismissed locally; this does not close it at the server.
    [[nodiscard]] Q_INVOKABLE bool markDismissed(uint id);

    /// Invoke an action through KDE's notification manager.
    [[nodiscard]] Q_INVOKABLE bool invokeAction(uint id, const QString &action);

    /// Return the number of unread notifications for the given desktop entry (e.g. "slack").
    [[nodiscard]] Q_INVOKABLE int unreadCount(const QString &desktopEntry) const;

    /// Return true if the SNI item matching @p appId has NeedsAttention status.
    [[nodiscard]] Q_INVOKABLE bool sniNeedsAttention(const QString &appId) const;
    [[nodiscard]] Q_INVOKABLE QString sniTitle(const QString &appId) const;
    [[nodiscard]] Q_INVOKABLE QString sniAttentionIconName(const QString &appId) const;
    [[nodiscard]] Q_INVOKABLE QString sniOverlayIconName(const QString &appId) const;
    [[nodiscard]] Q_INVOKABLE QString sniToolTipTitle(const QString &appId) const;
    [[nodiscard]] Q_INVOKABLE QString sniToolTipSubTitle(const QString &appId) const;
    /// Clear the local unread contribution for the given app without closing
    /// its server notification.
    Q_INVOKABLE void clearUnreadNotifications(const QString &appId);

    /// Dump all tracked state to the console (debug tool).
    Q_INVOKABLE void dumpState() const;

Q_SIGNALS:
    void revisionChanged();
    void dndActiveChanged();

public Q_SLOTS:
    // D-Bus notification watcher slots — called by the notification server.
    // Must be public for QDBusConnection::registerObject to export them.
    // Watcher Notify has notification ID as first param (unlike standard Notify which returns it)
    Q_SCRIPTABLE void Notify(uint id,
                             const QString &appName,
                             uint replacesId,
                             const QString &appIcon,
                             const QString &summary,
                             const QString &body,
                             const QStringList &actions,
                             const QVariantMap &hints,
                             int expireTimeout);
    Q_SCRIPTABLE void CloseNotification(uint id);
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);

private Q_SLOTS:
    void onNotificationServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    // SNI signal handlers
    void onSniRegistered(const QString &item);
    void onSniUnregistered(const QString &item);
    void onSniNewStatus(const QString &status, const QDBusMessage &message);
    void onSniNewTitle(const QDBusMessage &message);
    void onSniNewAttentionIcon(const QDBusMessage &message);
    void onSniNewOverlayIcon(const QDBusMessage &message);
    void onSniNewToolTip(const QDBusMessage &message);
    void onSniServiceUnregistered(const QString &service);
    void onSniWatcherServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);

private:
    struct SniEntry;
    struct NotificationRecord;
    void registerNotificationWatcher();
    void connectNotificationClosedMatch();
    void setupNotificationWatcher();
    void setupSniWatcher();
    void bumpRevision();
    void pruneInactiveNotificationRecords();
    void markNotificationInactive(uint id);
    void removeInactiveNotification(uint id);
    void resetNotificationModel();
    QVariantMap notificationMap(const NotificationRecord &record) const;
    void updateDndState();
    void queryRegisteredSniItems();
    void trackSniItem(const QString &service, const QString &objectPath);
    QString sniKeyForMessage(const QDBusMessage &message) const;
    void querySniProperty(const QString &key, quint64 generation, const QString &service, const QString &objectPath, const QString &property);
    void clearSniItems();
    [[nodiscard]] const SniEntry *sniEntryForApp(const QString &appId) const;

    // Notification settings (badges enabled, blacklist)
    std::unique_ptr<NotificationManager::Settings> m_notifSettings;

    enum class CloseReason : quint8 {
        Active,
        Replaced,
        Directed,
        BroadcastExpired,
        BroadcastDismissed,
        BroadcastClosedByCall,
        BroadcastUndefined,
    };

    // Retain the full watcher payload and lifecycle for each server ID. Closed
    // records remain available internally until the server reuses their ID.
    struct NotificationRecord {
        uint serverId = 0;
        QString desktopEntry;
        QString appName;
        QString appIcon;
        QString summary;
        QString body;
        QStringList actions;
        QVariantMap hints;
        int timeout = 0;
        uint replacesId = 0;
        quint8 urgency = 0;
        QDateTime createdAt;
        QDateTime updatedAt;
        CloseReason closeReason = CloseReason::Active;
        uint broadcastReason = 0;
        bool read = false;
        bool dismissed = false;
    };

    QVector<uint> m_modelIds;

    // Active notification ID -> canonical desktop entry (for reverse lookup).
    QHash<uint, QString> m_notifIdToEntry;
    // Canonical desktop entry -> set of active notification IDs.
    QHash<QString, QSet<uint>> m_unreadNotifs;
    QHash<uint, NotificationRecord> m_notificationRecords;
    QMap<QPair<QDateTime, uint>, uint> m_inactiveRecordOrder;
    QHash<uint, QPair<QDateTime, uint>> m_inactiveRecordKeys;

    // SNI tracking: "service|objectPath" -> item metadata
    struct SniEntry {
        QString id; // Application identifier from SNI Id property
        QString status; // "Passive", "Active", "NeedsAttention"
        QString title;
        QString attentionIconName;
        QString overlayIconName;
        QString toolTipTitle;
        QString toolTipSubTitle;
        quint64 generation = 0;
    };
    QHash<QString, SniEntry> m_sniItems;

    QDBusServiceWatcher m_notificationServiceWatcher;
    QDBusServiceWatcher m_sniWatcherServiceWatcher;
    QDBusServiceWatcher m_sniServiceWatcher;
    QTimer m_dndExpiryTimer;

    quint64 m_notificationOwnerGeneration = 0;
    quint64 m_sniWatcherGeneration = 0;
    quint64 m_sniItemGeneration = 0;
    bool m_notificationObjectRegistered = false;
    bool m_notificationClosedMatchConnected = false;
    bool m_notificationRegisterPending = false;
    bool m_notificationWatcherRegistered = false;
    bool m_sniWatcherRegisteredMatchConnected = false;
    bool m_sniWatcherUnregisteredMatchConnected = false;
    bool m_sniStatusMatchConnected = false;
    bool m_sniTitleMatchConnected = false;
    bool m_sniAttentionIconMatchConnected = false;
    bool m_sniOverlayIconMatchConnected = false;
    bool m_sniToolTipMatchConnected = false;
    int m_revision = 0;
    bool m_dndActive = false;
};

} // namespace krema
