// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "notificationtracker.h"

#include <settings.h>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcNotif, "krema.notifications")

namespace krema
{

// Strip ".desktop" suffix if present.
// TasksModel's AppId includes ".desktop" (e.g. "org.kde.konsole.desktop"),
// while notification desktop-entry hints do not (e.g. "org.kde.konsole").
static QString stripDesktopSuffix(const QString &id)
{
    static const QLatin1String suffix(".desktop");
    if (id.endsWith(suffix)) {
        return id.left(id.size() - suffix.size());
    }
    return id;
}

// Extract the last dot-separated segment of a desktop entry ID.
// e.g. "org.kde.konsole" → "konsole", "konsole" → "konsole"
static QString lastSegment(const QString &id)
{
    const int dot = id.lastIndexOf(QLatin1Char('.'));
    return (dot >= 0) ? id.mid(dot + 1) : id;
}

// Check if two desktop entry IDs match, considering reverse-domain naming.
// Strips ".desktop" suffix before comparison.
// e.g. "konsole" matches "org.kde.konsole" and "org.kde.konsole.desktop"
static bool desktopEntryMatches(const QString &a, const QString &b)
{
    const QString na = stripDesktopSuffix(a);
    const QString nb = stripDesktopSuffix(b);
    if (na == nb) {
        return true;
    }
    return lastSegment(na) == lastSegment(nb);
}

NotificationTracker::NotificationTracker(QObject *parent)
    : QObject(parent)
    , m_sniServiceWatcher(QString(), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForUnregistration)
{
    // Settings — must load() explicitly (constructor doesn't auto-load)
    m_notifSettings = std::make_unique<NotificationManager::Settings>(this);
    m_notifSettings->load();

    qCInfo(lcNotif) << "NotificationTracker init: badgesInTaskManager=" << (m_notifSettings ? m_notifSettings->badgesInTaskManager() : true)
                    << "blacklist=" << (m_notifSettings ? m_notifSettings->badgeBlacklistedApplications() : QStringList());

    // DND state tracking
    connect(m_notifSettings.get(), &NotificationManager::Settings::settingsChanged, this, &NotificationTracker::updateDndState);
    connect(m_notifSettings.get(), &NotificationManager::Settings::notificationsInhibitedByApplicationChanged, this, &NotificationTracker::updateDndState);

    setupNotificationWatcher();
    setupSniWatcher();
    updateDndState();
}

NotificationTracker::~NotificationTracker()
{
    // Unregister watcher from notification server
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                      QStringLiteral("/org/freedesktop/Notifications"),
                                                      QStringLiteral("org.kde.NotificationManager"),
                                                      QStringLiteral("UnRegisterWatcher"));
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
}

int NotificationTracker::revision() const
{
    return m_revision;
}

bool NotificationTracker::dndActive() const
{
    return m_dndActive;
}

void NotificationTracker::updateDndState()
{
    bool active = false;
    if (m_notifSettings) {
        active = m_notifSettings->notificationsInhibitedByApplication();
        if (!active) {
            const auto until = m_notifSettings->notificationsInhibitedUntil();
            if (until.isValid() && until > QDateTime::currentDateTime()) {
                active = true;
                // Schedule re-check when DND period expires
                const int msUntil = static_cast<int>(QDateTime::currentDateTime().msecsTo(until));
                if (msUntil > 0) {
                    QTimer::singleShot(msUntil + 100, this, &NotificationTracker::updateDndState);
                }
            }
        }
    }
    if (m_dndActive != active) {
        m_dndActive = active;
        qCInfo(lcNotif) << "DND state changed:" << active;
        Q_EMIT dndActiveChanged();
    }
}

int NotificationTracker::unreadCount(const QString &desktopEntry) const
{
    if (desktopEntry.isEmpty()) {
        return 0;
    }

    if (m_notifSettings && !m_notifSettings->badgesInTaskManager()) {
        return 0;
    }

    if (m_notifSettings && m_notifSettings->badgeBlacklistedApplications().contains(desktopEntry, Qt::CaseInsensitive)) {
        qCInfo(lcNotif) << "  unreadCount(" << desktopEntry << ") = 0 (blacklisted)";
        return 0;
    }

    const QString normalized = stripDesktopSuffix(desktopEntry.toLower());

    // Exact match (after stripping .desktop suffix from both sides)
    auto it = m_unreadNotifs.find(normalized);
    if (it != m_unreadNotifs.end()) {
        qCInfo(lcNotif) << "  unreadCount(" << desktopEntry << ") =" << it->size() << "(exact match, normalized=" << normalized << ")";
        return it->size();
    }

    // Suffix match: "org.kde.konsole" ↔ "konsole"
    for (auto it2 = m_unreadNotifs.cbegin(); it2 != m_unreadNotifs.cend(); ++it2) {
        if (desktopEntryMatches(it2.key(), normalized)) {
            qCInfo(lcNotif) << "  unreadCount(" << desktopEntry << ") =" << it2->size() << "(suffix match: stored=" << it2.key() << " query=" << normalized
                            << ")";
            return it2->size();
        }
    }

    // No match — only log when there are notifications to match against
    if (!m_unreadNotifs.isEmpty()) {
        qCDebug(lcNotif) << "  unreadCount(" << desktopEntry << ") = 0, no match. stored keys:" << m_unreadNotifs.keys();
    }
    return 0;
}

bool NotificationTracker::sniNeedsAttention(const QString &appId) const
{
    if (appId.isEmpty()) {
        return false;
    }

    const QString normalized = stripDesktopSuffix(appId.toLower());
    for (const auto &entry : m_sniItems) {
        if (desktopEntryMatches(entry.id.toLower(), normalized) && entry.status == QLatin1String("NeedsAttention")) {
            qCInfo(lcNotif) << "  sniNeedsAttention(" << appId << ") = true (sniId=" << entry.id << ")";
            return true;
        }
    }
    return false;
}

void NotificationTracker::clearUnreadNotifications(const QString &appId)
{
    if (appId.isEmpty()) {
        return;
    }

    const QString normalized = stripDesktopSuffix(appId.toLower());
    bool changed = false;

    // Find and remove all notification entries matching this app
    for (auto it = m_unreadNotifs.begin(); it != m_unreadNotifs.end();) {
        if (desktopEntryMatches(it.key(), normalized)) {
            qCInfo(lcNotif) << "clearUnreadNotifications: clearing" << it->size() << "notifications for" << it.key() << "(triggered by SNI" << appId << ")";
            // Remove reverse-lookup entries
            for (uint notifId : *it) {
                m_notifIdToEntry.remove(notifId);
            }
            it = m_unreadNotifs.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (changed) {
        qCInfo(lcNotif) << "clearUnreadNotifications: cleared for" << appId;
    }
}

void NotificationTracker::dumpState() const
{
    qCInfo(lcNotif) << "=== Krema Notification State ===";

    // Notification watcher data
    const bool badgesEnabled = m_notifSettings ? m_notifSettings->badgesInTaskManager() : true;
    qCInfo(lcNotif).noquote()
        << QStringLiteral("[Notification Watcher] (tracked IDs: %1, badgesEnabled: %2)").arg(m_notifIdToEntry.size()).arg(badgesEnabled ? "true" : "false");

    for (auto it = m_unreadNotifs.cbegin(); it != m_unreadNotifs.cend(); ++it) {
        qCInfo(lcNotif).noquote() << QStringLiteral("  %1 : %2 unread (IDs: %3)").arg(it.key()).arg(it->size()).arg([&]() {
            QStringList ids;
            for (uint id : *it) {
                ids << QString::number(id);
            }
            return ids.join(QStringLiteral(", "));
        }());
    }
    if (m_unreadNotifs.isEmpty()) {
        qCInfo(lcNotif).noquote() << QStringLiteral("  (none)");
    }

    // SNI items
    qCInfo(lcNotif).noquote() << QStringLiteral("[SNI Items] (tracked: %1)").arg(m_sniItems.size());
    for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
        QString line = QStringLiteral("  %1 (%2)").arg(it.value().id, it.value().status);
        if (it.value().status == QLatin1String("NeedsAttention")) {
            line += QStringLiteral(" <- ATTENTION");
        }
        qCInfo(lcNotif).noquote() << line;
    }

    // Blacklisted apps
    if (m_notifSettings) {
        const auto blacklist = m_notifSettings->badgeBlacklistedApplications();
        if (!blacklist.isEmpty()) {
            qCInfo(lcNotif).noquote() << QStringLiteral("[Blacklisted] %1").arg(blacklist.join(QStringLiteral(", ")));
        }
    }
}

// --- D-Bus notification watcher ---

void NotificationTracker::setupNotificationWatcher()
{
    auto bus = QDBusConnection::sessionBus();

    // Register our object at /NotificationWatcher with org.kde.NotificationWatcher interface.
    // The notification server (plasmashell) hardcodes this path/interface when calling watchers.
    // Do NOT use /org/freedesktop/Notifications or org.freedesktop.Notifications here.
    bool registered =
        bus.registerObject(QStringLiteral("/NotificationWatcher"), QStringLiteral("org.kde.NotificationWatcher"), this, QDBusConnection::ExportScriptableSlots);
    if (!registered) {
        qCWarning(lcNotif) << "Failed to register D-Bus notification watcher object:" << bus.lastError().message();
        return;
    }

    qCInfo(lcNotif) << "D-Bus watcher object registered at /NotificationWatcher (interface=org.kde.NotificationWatcher)";

    // Subscribe to NotificationClosed broadcast signal.
    // This is a broadcast signal from org.freedesktop.Notifications, NOT a directed call
    // to our watcher object. Must be subscribed separately.
    bus.connect(QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("/org/freedesktop/Notifications"),
                QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("NotificationClosed"),
                this,
                SLOT(NotificationClosed(uint, uint)));

    // Tell the notification server we want to watch all notifications
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                      QStringLiteral("/org/freedesktop/Notifications"),
                                                      QStringLiteral("org.kde.NotificationManager"),
                                                      QStringLiteral("RegisterWatcher"));
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCWarning(lcNotif) << "RegisterWatcher FAILED:" << reply.error().message();
        } else {
            qCInfo(lcNotif) << "RegisterWatcher succeeded — now receiving notifications";
        }
    });
}

void NotificationTracker::Notify(uint id,
                                 const QString &appName,
                                 uint replacesId,
                                 const QString & /*appIcon*/,
                                 const QString &summary,
                                 const QString & /*body*/,
                                 const QStringList & /*actions*/,
                                 const QVariantMap &hints,
                                 int /*expireTimeout*/)
{
    // Extract desktop entry from hints (primary) or fall back to appName,
    // then normalize to full storageId via KService (e.g. "konsole" → "org.kde.konsole")
    QString rawEntry = hints.value(QStringLiteral("desktop-entry")).toString();
    if (rawEntry.isEmpty()) {
        rawEntry = appName;
    }
    const QString desktopEntry = rawEntry.toLower();

    qCInfo(lcNotif) << ">>> Notify: id=" << id << "appName=" << appName << "desktop-entry hint=" << hints.value(QStringLiteral("desktop-entry")).toString()
                    << "resolved=" << desktopEntry << "replacesId=" << replacesId << "summary=" << summary;

    // If replacing, remove old entry first
    if (replacesId > 0) {
        if (auto it = m_notifIdToEntry.find(replacesId); it != m_notifIdToEntry.end()) {
            const QString &oldEntry = it.value();
            if (auto setIt = m_unreadNotifs.find(oldEntry); setIt != m_unreadNotifs.end()) {
                setIt->remove(replacesId);
                if (setIt->isEmpty()) {
                    m_unreadNotifs.erase(setIt);
                }
            }
            m_notifIdToEntry.erase(it);
            qCInfo(lcNotif) << "    replaced old notif id=" << replacesId << "entry=" << oldEntry;
        }
    }

    m_notifIdToEntry[id] = desktopEntry;
    m_unreadNotifs[desktopEntry].insert(id);

    qCInfo(lcNotif) << "    stored: unread[" << desktopEntry << "] =" << m_unreadNotifs[desktopEntry].size();

    bumpRevision();
}

void NotificationTracker::CloseNotification(uint id)
{
    auto it = m_notifIdToEntry.find(id);
    if (it == m_notifIdToEntry.end()) {
        return;
    }

    const QString entry = it.value();
    m_notifIdToEntry.erase(it);

    if (auto setIt = m_unreadNotifs.find(entry); setIt != m_unreadNotifs.end()) {
        setIt->remove(id);
        if (setIt->isEmpty()) {
            m_unreadNotifs.erase(setIt);
        }
    }

    bumpRevision();

    qCInfo(lcNotif) << "<<< CloseNotification: id=" << id << "entry=" << entry;
}

void NotificationTracker::NotificationClosed(uint id, uint /*reason*/)
{
    auto it = m_notifIdToEntry.find(id);
    if (it == m_notifIdToEntry.end()) {
        return;
    }

    const QString &entry = it.value();

    if (auto setIt = m_unreadNotifs.find(entry); setIt != m_unreadNotifs.end()) {
        setIt->remove(id);
        if (setIt->isEmpty()) {
            m_unreadNotifs.erase(setIt);
        }
    }

    m_notifIdToEntry.erase(it);
    bumpRevision();

    qCInfo(lcNotif) << "<<< NotificationClosed (broadcast): id=" << id << "entry=" << entry;
}

// --- SNI watcher ---

void NotificationTracker::setupSniWatcher()
{
    auto bus = QDBusConnection::sessionBus();

    bus.connect(QStringLiteral("org.kde.StatusNotifierWatcher"),
                QStringLiteral("/StatusNotifierWatcher"),
                QStringLiteral("org.kde.StatusNotifierWatcher"),
                QStringLiteral("StatusNotifierItemRegistered"),
                this,
                SLOT(onSniRegistered(QString)));

    bus.connect(QStringLiteral("org.kde.StatusNotifierWatcher"),
                QStringLiteral("/StatusNotifierWatcher"),
                QStringLiteral("org.kde.StatusNotifierWatcher"),
                QStringLiteral("StatusNotifierItemUnregistered"),
                this,
                SLOT(onSniUnregistered(QString)));

    // Listen to ALL NewStatus signals from any StatusNotifierItem.
    bus.connect(QString(),
                QString(),
                QStringLiteral("org.kde.StatusNotifierItem"),
                QStringLiteral("NewStatus"),
                this,
                SLOT(onSniNewStatus(QString, QDBusMessage)));

    connect(&m_sniServiceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &NotificationTracker::onSniServiceUnregistered);

    qCInfo(lcNotif) << "SNI watcher signals subscribed";

    queryRegisteredSniItems();
}

void NotificationTracker::bumpRevision()
{
    ++m_revision;

    // Build state summary for debugging
    QStringList summary;
    for (auto it = m_unreadNotifs.cbegin(); it != m_unreadNotifs.cend(); ++it) {
        summary << QStringLiteral("%1=%2").arg(it.key()).arg(it->size());
    }
    int sniAttentionCount = 0;
    for (const auto &sni : m_sniItems) {
        if (sni.status == QLatin1String("NeedsAttention"))
            ++sniAttentionCount;
    }
    const bool badgesEnabled = m_notifSettings ? m_notifSettings->badgesInTaskManager() : true;
    qCInfo(lcNotif) << "bumpRevision:" << m_revision << "| unread:" << (summary.isEmpty() ? QStringLiteral("(none)") : summary.join(QStringLiteral(", ")))
                    << "| sniAttention:" << sniAttentionCount << "| badgesEnabled:" << badgesEnabled;

    Q_EMIT revisionChanged();
}

void NotificationTracker::queryRegisteredSniItems()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.StatusNotifierWatcher"),
                                                      QStringLiteral("/StatusNotifierWatcher"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    msg << QStringLiteral("org.kde.StatusNotifierWatcher") << QStringLiteral("RegisteredStatusNotifierItems");

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError()) {
            qCWarning(lcNotif) << "Failed to query SNI items:" << reply.error().message();
            return;
        }

        const QStringList items = reply.value().variant().toStringList();
        qCInfo(lcNotif) << "Registered SNI items at startup:" << items.size() << items;

        for (const QString &item : items) {
            onSniRegistered(item);
        }
    });
}

void NotificationTracker::trackSniItem(const QString &service, const QString &objectPath)
{
    const QString key = service + objectPath;

    if (m_sniItems.contains(key)) {
        return;
    }

    m_sniServiceWatcher.addWatchedService(service);
    m_sniItems[key] = SniEntry{.id = QString(), .status = QStringLiteral("Passive")};

    auto bus = QDBusConnection::sessionBus();

    // Query Id property
    QDBusMessage idMsg = QDBusMessage::createMethodCall(service, objectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    idMsg << QStringLiteral("org.kde.StatusNotifierItem") << QStringLiteral("Id");

    auto *idWatcher = new QDBusPendingCallWatcher(bus.asyncCall(idMsg), this);
    connect(idWatcher, &QDBusPendingCallWatcher::finished, this, [this, key](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError()) {
            qCInfo(lcNotif) << "SNI Id query failed for" << key << ":" << reply.error().message();
            return;
        }
        if (auto it = m_sniItems.find(key); it != m_sniItems.end()) {
            it->id = reply.value().variant().toString();
            qCInfo(lcNotif) << "SNI tracked:" << key << "id=" << it->id;
        }
    });

    // Query Status property
    QDBusMessage statusMsg = QDBusMessage::createMethodCall(service, objectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    statusMsg << QStringLiteral("org.kde.StatusNotifierItem") << QStringLiteral("Status");

    auto *statusWatcher = new QDBusPendingCallWatcher(bus.asyncCall(statusMsg), this);
    connect(statusWatcher, &QDBusPendingCallWatcher::finished, this, [this, key](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError()) {
            qCInfo(lcNotif) << "SNI Status query failed for" << key << ":" << reply.error().message();
            return;
        }
        if (auto it = m_sniItems.find(key); it != m_sniItems.end()) {
            const QString newStatus = reply.value().variant().toString();
            if (it->status != newStatus) {
                qCInfo(lcNotif) << "SNI initial status:" << key << it->status << "->" << newStatus;
                it->status = newStatus;
                bumpRevision();
            }
        }
    });
}

// --- SNI D-Bus slot handlers ---

void NotificationTracker::onSniRegistered(const QString &item)
{
    qCInfo(lcNotif) << "SNI registered:" << item;

    const int slashIdx = item.indexOf(QLatin1Char('/'));
    QString service, objectPath;
    if (slashIdx > 0) {
        service = item.left(slashIdx);
        objectPath = item.mid(slashIdx);
    } else {
        service = item;
        objectPath = QStringLiteral("/StatusNotifierItem");
    }
    trackSniItem(service, objectPath);
}

void NotificationTracker::onSniUnregistered(const QString &item)
{
    const int slashIdx = item.indexOf(QLatin1Char('/'));
    QString key;
    if (slashIdx > 0) {
        key = item;
    } else {
        key = item + QStringLiteral("/StatusNotifierItem");
    }

    if (m_sniItems.remove(key) > 0) {
        bumpRevision();
        qCInfo(lcNotif) << "SNI unregistered:" << key;
    }
}

void NotificationTracker::onSniNewStatus(const QString &status, const QDBusMessage &message)
{
    const QString key = message.service() + message.path();

    if (auto it = m_sniItems.find(key); it != m_sniItems.end()) {
        if (it->status != status) {
            const QString oldStatus = it->status;
            qCInfo(lcNotif) << ">>> SNI status changed:" << it->id << "(" << key << ")" << oldStatus << "->" << status;
            it->status = status;

            // When transitioning away from NeedsAttention, the user has attended
            // to the app (e.g. opened Slack). Clear notification watcher counts
            // for this app so badge count reflects the acknowledged state.
            if (oldStatus == QLatin1String("NeedsAttention") && status != QLatin1String("NeedsAttention") && !it->id.isEmpty()) {
                clearUnreadNotifications(it->id);
            }

            bumpRevision();
        }
    } else {
        qCInfo(lcNotif) << "SNI NewStatus from unknown item:" << key << "status=" << status;
    }
}

void NotificationTracker::onSniServiceUnregistered(const QString &service)
{
    bool changed = false;
    for (auto it = m_sniItems.begin(); it != m_sniItems.end();) {
        if (it.key().startsWith(service)) {
            qCInfo(lcNotif) << "SNI service gone:" << it.key() << "id=" << it->id;
            it = m_sniItems.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        bumpRevision();
    }
}

} // namespace krema
