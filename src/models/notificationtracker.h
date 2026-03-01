// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QObject>
#include <QSet>

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

public:
    explicit NotificationTracker(QObject *parent = nullptr);
    ~NotificationTracker() override;

    [[nodiscard]] int revision() const;

    /// Return the number of unread notifications for the given desktop entry (e.g. "slack").
    [[nodiscard]] Q_INVOKABLE int unreadCount(const QString &desktopEntry) const;

    /// Return true if the SNI item matching @p appId has NeedsAttention status.
    [[nodiscard]] Q_INVOKABLE bool sniNeedsAttention(const QString &appId) const;

    /// Dump all tracked state to the console (debug tool).
    Q_INVOKABLE void dumpState() const;

Q_SIGNALS:
    void revisionChanged();

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
    // SNI D-Bus signal handlers
    void onSniRegistered(const QString &item);
    void onSniUnregistered(const QString &item);
    void onSniNewStatus(const QString &status, const QDBusMessage &message);
    void onSniServiceUnregistered(const QString &service);

private:
    void setupNotificationWatcher();
    void setupSniWatcher();
    void bumpRevision();

    void queryRegisteredSniItems();
    void trackSniItem(const QString &service, const QString &objectPath);

    // Notification settings (badges enabled, blacklist)
    std::unique_ptr<NotificationManager::Settings> m_notifSettings;

    // Per-app unread notification tracking
    // notificationId -> desktopEntry (for reverse lookup on close)
    QHash<uint, QString> m_notifIdToEntry;
    // desktopEntry -> set of active notification IDs
    QHash<QString, QSet<uint>> m_unreadNotifs;

    // SNI tracking: "busName/objectPath" -> {id, status}
    struct SniEntry {
        QString id; // Application identifier from SNI Id property
        QString status; // "Passive", "Active", "NeedsAttention"
    };
    QHash<QString, SniEntry> m_sniItems;

    QDBusServiceWatcher m_sniServiceWatcher;

    int m_revision = 0;
};

} // namespace krema
