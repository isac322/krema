// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QTimer>
#include <QStringList>
#include <QVariantMap>

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
class NotificationTracker : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationWatcher")

    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool dndActive READ dndActive NOTIFY dndActiveChanged)

public:
    explicit NotificationTracker(QObject *parent = nullptr);
    ~NotificationTracker() override;

    [[nodiscard]] int revision() const;
    [[nodiscard]] bool dndActive() const;
    /// Return whether badges are enabled for @p appId (global and per-app settings).
    [[nodiscard]] Q_INVOKABLE bool badgesEnabled(const QString &appId) const;

    /// Return the Slack SNI chat state: 0 = none, 1 = unread, 2 = mention.
    [[nodiscard]] Q_INVOKABLE int sniChatUnreadState(const QString &appId) const;
    /// Return Slack's positive mention count reported by its tray tooltip.
    [[nodiscard]] Q_INVOKABLE int sniChatMentionCount(const QString &appId) const;

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
    void onSniNewIcon(const QDBusMessage &message);
    void onSniNewIconThemePath(const QDBusMessage &message);
    void processSniIconRefreshes();
    void onSniPropertiesChanged(const QString &interfaceName,
                                const QVariantMap &changedProperties,
                                const QStringList &invalidatedProperties,
                                const QDBusMessage &message);
    void onSniServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void onSniWatcherServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);

private:
    struct SniEntry;
    void registerNotificationWatcher();
    void connectNotificationClosedMatch();
    void setupNotificationWatcher();
    void setupSniWatcher();
    void bumpRevision();
    bool removeActiveNotification(uint id);
    void updateDndState();
    void queryAllSniProperties(const QString &key, quint64 generation, const QString &service, const QString &objectPath);
    void querySniProperty(const QString &key,
                          quint64 generation,
                          const QString &service,
                          const QString &objectPath,
                          const QString &property,
                          const QString &interfaceName = {},
                          bool iconQuery = false);
    void scheduleSniIconRefresh(const QString &key, const QString &interfaceName);
    void querySniIconProperties(const QString &key,
                                quint64 generation,
                                const QString &service,
                                const QString &objectPath,
                                const QString &interfaceName);
    void updateSniIconImage(SniEntry &entry);
    void updateSniChatState(SniEntry &entry);
    void ensureSniMetadataQueried(const QString &key, SniEntry &entry);
    void clearSniItems();
    [[nodiscard]] const SniEntry *sniEntryForApp(const QString &appId) const;

    // Notification settings (badges enabled, blacklist)
    std::unique_ptr<NotificationManager::Settings> m_notifSettings;

    // Active notification ID -> canonical desktop entry (for reverse lookup).
    QHash<uint, QString> m_notifIdToEntry;
    // Canonical desktop entry -> set of active notification IDs.
    QHash<QString, QSet<uint>> m_unreadNotifs;

    // SNI tracking: "service|objectPath" -> item metadata
    struct SniEntry {
        QString id; // Application identifier from SNI Id property
        QString status; // "Passive", "Active", "NeedsAttention"
        QString title;
        QString attentionIconName;
        QString overlayIconName;
        QString toolTipTitle;
        QString toolTipSubTitle;
        QImage iconImage;
        QString iconName;
        QString iconThemePath;
        QImage iconPixmapImage;
        int chatUnreadState = 0;
        int chatMentionCount = 0;
        bool iconQueryInFlight = false;
        bool iconQueryPending = false;
        QString iconQueryInterface;
        int iconQueryOutstanding = 0;
        QImage iconQueryPreviousImage;
        int iconQueryPreviousChatState = 0;
        int iconQueryPreviousMentionCount = 0;
        quint64 generation = 0;
    };
    QHash<QString, SniEntry> m_sniItems;

    QDBusServiceWatcher m_notificationServiceWatcher;
    QDBusServiceWatcher m_sniWatcherServiceWatcher;
    QDBusServiceWatcher m_sniServiceWatcher;
    QTimer m_dndExpiryTimer;
    bool m_sniIconRefreshQueued = false;
    quint64 m_notificationOwnerGeneration = 0;
    quint64 m_sniWatcherGeneration = 0;
    quint64 m_sniItemGeneration = 0;
    bool m_notificationObjectRegistered = false;
    bool m_notificationClosedMatchConnected = false;
    bool m_notificationRegisterPending = false;
    bool m_sniWatcherRegisteredMatchConnected = false;
    bool m_sniWatcherUnregisteredMatchConnected = false;
    bool m_sniStatusMatchConnected = false;
    bool m_sniTitleMatchConnected = false;
    bool m_sniAttentionIconMatchConnected = false;
    bool m_sniOverlayIconMatchConnected = false;
    bool m_sniToolTipMatchConnected = false;
    bool m_sniIconMatchConnected = false;
    bool m_sniIconThemePathMatchConnected = false;
    bool m_sniFreedesktopIconMatchConnected = false;
    bool m_sniFreedesktopIconThemePathMatchConnected = false;
    bool m_sniPropertiesChangedMatchConnected = false;
    bool m_dndActive = false;
};

} // namespace krema
