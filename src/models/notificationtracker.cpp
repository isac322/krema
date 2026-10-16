// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "notificationtracker.h"

#include <settings.h>

#include <KService>
#include <algorithm>
#include <utility>

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
namespace
{
constexpr qsizetype kMaxInactiveNotificationRecords = 100;
}

// Strip ".desktop" suffix if present.
static QString stripDesktopSuffix(const QString &id)
{
    static const QLatin1String suffix(".desktop");
    if (id.endsWith(suffix)) {
        return id.left(id.size() - suffix.size());
    }
    return id;
}

// Canonicalize the identity used by notification storage, queries, and
// blacklist checks. KService resolves aliases to the installed storage ID;
// unresolved hints/app names deliberately fall back to their lower-case,
// suffix-stripped value rather than using fuzzy last-segment matching.
static QString canonicalDesktopId(const QString &rawId)
{
    QString id = stripDesktopSuffix(rawId.trimmed().toLower());
    if (id.isEmpty()) {
        return {};
    }

    KService::Ptr service = KService::serviceByStorageId(id);
    if (!service) {
        service = KService::serviceByStorageId(id + QStringLiteral(".desktop"));
    }
    if (!service) {
        service = KService::serviceByDesktopName(id);
    }
    if (service) {
        id = stripDesktopSuffix(service->storageId().toLower());
    }
    return id;
}

NotificationTracker::NotificationTracker(QObject *parent)
    : QAbstractListModel(parent)
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

int NotificationTracker::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_modelIds.size();
}

QVariant NotificationTracker::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_modelIds.size()) {
        return {};
    }
    const auto recordIt = m_notificationRecords.constFind(m_modelIds.at(index.row()));
    if (recordIt == m_notificationRecords.cend()) {
        return {};
    }
    const auto &record = recordIt.value();
    switch (role) {
    case IdRole:
        return record.serverId;
    case DesktopEntryRole:
        return record.desktopEntry;
    case AppNameRole:
        return record.appName;
    case AppIconRole:
        return record.appIcon;
    case SummaryRole:
        return record.summary;
    case BodyRole:
        return record.body;
    case ActionsRole:
        return record.actions;
    case HintsRole:
        return record.hints;
    case TimeoutRole:
        return record.timeout;
    case ReplacesIdRole:
        return record.replacesId;
    case UrgencyRole:
        return record.urgency;
    case ActiveRole:
        return m_notifIdToEntry.contains(record.serverId);
    case ReadRole:
        return record.read;
    case DismissedRole:
        return record.dismissed;
    case CloseReasonRole:
        switch (record.closeReason) {
        case CloseReason::Active:
            return QStringLiteral("active");
        case CloseReason::Replaced:
            return QStringLiteral("replaced");
        case CloseReason::Directed:
            return QStringLiteral("directed");
        case CloseReason::Cleared:
            return QStringLiteral("cleared");
        case CloseReason::BroadcastExpired:
            return QStringLiteral("broadcast-expired");
        case CloseReason::BroadcastDismissed:
            return QStringLiteral("broadcast-dismissed");
        case CloseReason::BroadcastClosedByCall:
            return QStringLiteral("broadcast-closed-by-call");
        case CloseReason::BroadcastUndefined:
            return QStringLiteral("broadcast-undefined");
        }
        break;
    case BroadcastReasonRole:
        return record.broadcastReason;
    case CreatedAtRole:
        return record.createdAt;
    case UpdatedAtRole:
        return record.updatedAt;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> NotificationTracker::roleNames() const
{
    return {
        {IdRole, "id"},
        {DesktopEntryRole, "desktopEntry"},
        {AppNameRole, "appName"},
        {AppIconRole, "appIcon"},
        {SummaryRole, "summary"},
        {BodyRole, "body"},
        {ActionsRole, "actions"},
        {HintsRole, "hints"},
        {TimeoutRole, "timeout"},
        {ReplacesIdRole, "replacesId"},
        {UrgencyRole, "urgency"},
        {ActiveRole, "active"},
        {ReadRole, "read"},
        {DismissedRole, "dismissed"},
        {CloseReasonRole, "closeReason"},
        {BroadcastReasonRole, "broadcastReason"},
        {CreatedAtRole, "createdAt"},
        {UpdatedAtRole, "updatedAt"},
    };
}

int NotificationTracker::revision() const
{
    return m_revision;
}

bool NotificationTracker::dndActive() const
{
    return m_dndActive;
}

QVariantList NotificationTracker::notifications(const QString &desktopEntry, bool activeOnly) const
{
    const QString normalized = desktopEntry.isEmpty() ? QString() : canonicalDesktopId(desktopEntry);
    QVariantList result;
    for (uint id : m_modelIds) {
        const auto it = m_notificationRecords.constFind(id);
        if (it == m_notificationRecords.cend()) {
            continue;
        }
        if (!normalized.isEmpty() && it->desktopEntry != normalized) {
            continue;
        }
        if (activeOnly && !m_notifIdToEntry.contains(id)) {
            continue;
        }
        result.append(notificationMap(it.value()));
    }
    return result;
}

bool NotificationTracker::markRead(uint id)
{
    auto it = m_notificationRecords.find(id);
    if (it == m_notificationRecords.end() || it->read) {
        return false;
    }
    it->read = true;
    if (auto entryIt = m_notifIdToEntry.find(id); entryIt != m_notifIdToEntry.end()) {
        if (auto unreadIt = m_unreadNotifs.find(entryIt.value()); unreadIt != m_unreadNotifs.end()) {
            unreadIt->remove(id);
            if (unreadIt->isEmpty()) {
                m_unreadNotifs.erase(unreadIt);
            }
        }
    }
    it->updatedAt = QDateTime::currentDateTimeUtc();
    bumpRevision();
    return true;
}

bool NotificationTracker::markDismissed(uint id)
{
    auto it = m_notificationRecords.find(id);
    if (it == m_notificationRecords.end() || it->dismissed) {
        return false;
    }
    it->dismissed = true;
    if (auto entryIt = m_notifIdToEntry.find(id); entryIt != m_notifIdToEntry.end()) {
        if (auto unreadIt = m_unreadNotifs.find(entryIt.value()); unreadIt != m_unreadNotifs.end()) {
            unreadIt->remove(id);
            if (unreadIt->isEmpty()) {
                m_unreadNotifs.erase(unreadIt);
            }
        }
    }
    it->updatedAt = QDateTime::currentDateTimeUtc();
    bumpRevision();
    return true;
}

bool NotificationTracker::invokeAction(uint id, const QString &action)
{
    const auto it = m_notificationRecords.constFind(id);
    if (it == m_notificationRecords.cend() || action.isEmpty()) {
        return false;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                          QStringLiteral("/org/freedesktop/Notifications"),
                                                          QStringLiteral("org.kde.NotificationManager"),
                                                          QStringLiteral("InvokeAction"));
    message << id << action;
    return QDBusConnection::sessionBus().asyncCall(message).isValid();
}

QVariantMap NotificationTracker::notificationMap(const NotificationRecord &record) const
{
    QVariantMap result;
    result.insert(QStringLiteral("id"), record.serverId);
    result.insert(QStringLiteral("desktopEntry"), record.desktopEntry);
    result.insert(QStringLiteral("appName"), record.appName);
    result.insert(QStringLiteral("appIcon"), record.appIcon);
    result.insert(QStringLiteral("summary"), record.summary);
    result.insert(QStringLiteral("body"), record.body);
    result.insert(QStringLiteral("actions"), record.actions);
    result.insert(QStringLiteral("hints"), record.hints);
    result.insert(QStringLiteral("timeout"), record.timeout);
    result.insert(QStringLiteral("replacesId"), record.replacesId);
    result.insert(QStringLiteral("urgency"), record.urgency);
    result.insert(QStringLiteral("active"), m_notifIdToEntry.contains(record.serverId));
    result.insert(QStringLiteral("read"), record.read);
    result.insert(QStringLiteral("dismissed"), record.dismissed);
    result.insert(QStringLiteral("broadcastReason"), record.broadcastReason);
    result.insert(QStringLiteral("createdAt"), record.createdAt);
    result.insert(QStringLiteral("updatedAt"), record.updatedAt);
    QString closeReason;
    switch (record.closeReason) {
    case CloseReason::Active:
        closeReason = QStringLiteral("active");
        break;
    case CloseReason::Replaced:
        closeReason = QStringLiteral("replaced");
        break;
    case CloseReason::Directed:
        closeReason = QStringLiteral("directed");
        break;
    case CloseReason::Cleared:
        closeReason = QStringLiteral("cleared");
        break;
    case CloseReason::BroadcastExpired:
        closeReason = QStringLiteral("broadcast-expired");
        break;
    case CloseReason::BroadcastDismissed:
        closeReason = QStringLiteral("broadcast-dismissed");
        break;
    case CloseReason::BroadcastClosedByCall:
        closeReason = QStringLiteral("broadcast-closed-by-call");
        break;
    case CloseReason::BroadcastUndefined:
        closeReason = QStringLiteral("broadcast-undefined");
        break;
    }
    result.insert(QStringLiteral("closeReason"), closeReason);
    return result;
}

void NotificationTracker::removeInactiveNotification(uint id)
{
    const auto keyIt = m_inactiveRecordKeys.find(id);
    if (keyIt == m_inactiveRecordKeys.end()) {
        return;
    }
    m_inactiveRecordOrder.remove(keyIt.value());
    m_inactiveRecordKeys.erase(keyIt);
}

void NotificationTracker::markNotificationInactive(uint id)
{
    removeInactiveNotification(id);
    const auto recordIt = m_notificationRecords.constFind(id);
    if (recordIt == m_notificationRecords.cend()) {
        return;
    }
    const auto key = qMakePair(recordIt->updatedAt, id);
    m_inactiveRecordOrder.insert(key, id);
    m_inactiveRecordKeys.insert(id, key);
}

void NotificationTracker::pruneInactiveNotificationRecords()
{
    while (m_inactiveRecordOrder.size() > kMaxInactiveNotificationRecords) {
        const auto oldest = m_inactiveRecordOrder.cbegin();
        const uint id = oldest.value();
        m_inactiveRecordKeys.remove(id);
        m_inactiveRecordOrder.erase(oldest);
        m_notificationRecords.remove(id);
    }
}

void NotificationTracker::resetNotificationModel()
{
    QVector<uint> ids;
    ids.reserve(m_notificationRecords.size());
    for (auto it = m_notificationRecords.cbegin(); it != m_notificationRecords.cend(); ++it) {
        if (it->closeReason != CloseReason::Replaced) {
            ids.append(it.key());
        }
    }
    std::sort(ids.begin(), ids.end());
    beginResetModel();
    m_modelIds = std::move(ids);
    endResetModel();
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

    const QString normalized = canonicalDesktopId(desktopEntry);
    if (normalized.isEmpty()) {
        return 0;
    }

    if (m_notifSettings) {
        const auto blacklist = m_notifSettings->badgeBlacklistedApplications();
        const bool blacklisted = std::any_of(blacklist.cbegin(), blacklist.cend(), [&normalized](const QString &entry) {
            return canonicalDesktopId(entry) == normalized;
        });
        if (blacklisted) {
            qCInfo(lcNotif) << "  unreadCount(" << desktopEntry << ") = 0 (blacklisted)";
            return 0;
        }
    }

    const auto it = m_unreadNotifs.constFind(normalized);
    if (it != m_unreadNotifs.cend()) {
        qCInfo(lcNotif) << "  unreadCount(" << desktopEntry << ") =" << it->size() << "(canonical=" << normalized << ")";
        return it->size();
    }

    if (!m_unreadNotifs.isEmpty()) {
        qCDebug(lcNotif) << "  unreadCount(" << desktopEntry << ") = 0, no exact match. stored keys:" << m_unreadNotifs.keys();
    }
    return 0;
}

bool NotificationTracker::sniNeedsAttention(const QString &appId) const
{
    if (appId.isEmpty()) {
        return false;
    }

    const QString normalized = canonicalDesktopId(appId);
    for (const auto &entry : m_sniItems) {
        if (canonicalDesktopId(entry.id) == normalized && entry.status == QLatin1String("NeedsAttention")) {
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

    const QString normalized = canonicalDesktopId(appId);
    bool changed = false;

    // Find and remove all notification entries matching this canonical app.
    for (auto it = m_unreadNotifs.begin(); it != m_unreadNotifs.end();) {
        if (it.key() == normalized) {
            qCInfo(lcNotif) << "clearUnreadNotifications: clearing" << it->size() << "notifications for" << it.key() << "(triggered by SNI" << appId << ")";
            for (uint notifId : *it) {
                m_notifIdToEntry.remove(notifId);
                if (auto recordIt = m_notificationRecords.find(notifId); recordIt != m_notificationRecords.end()) {
                    recordIt->read = true;
                    recordIt->closeReason = CloseReason::Cleared;
                    recordIt->updatedAt = QDateTime::currentDateTimeUtc();
                    markNotificationInactive(notifId);
                }
            }
            it = m_unreadNotifs.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (changed) {
        qCInfo(lcNotif) << "clearUnreadNotifications: cleared for" << appId;
        bumpRevision();
    }
}

void NotificationTracker::dumpState() const
{
    qCInfo(lcNotif) << "=== Krema Notification State ===";

    // Notification watcher data
    const bool badgesEnabled = m_notifSettings ? m_notifSettings->badgesInTaskManager() : true;
    qCInfo(lcNotif).noquote() << QStringLiteral("[Notification Watcher] (tracked IDs: %1, badgesEnabled: %2)")
                                     .arg(m_notifIdToEntry.size())
                                     .arg(badgesEnabled ? QStringLiteral("true") : QStringLiteral("false"));

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
                                 const QString &appIcon,
                                 const QString &summary,
                                 const QString &body,
                                 const QStringList &actions,
                                 const QVariantMap &hints,
                                 int expireTimeout)
{
    QString rawEntry = hints.value(QStringLiteral("desktop-entry")).toString();
    if (rawEntry.isEmpty()) {
        rawEntry = appName;
    }
    const QString desktopEntry = canonicalDesktopId(rawEntry);
    if (desktopEntry.isEmpty()) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    NotificationRecord record;
    record.serverId = id;
    record.desktopEntry = desktopEntry;
    record.appName = appName;
    record.appIcon = appIcon;
    record.summary = summary;
    record.body = body;
    record.actions = actions;
    record.hints = hints;
    record.timeout = expireTimeout;
    record.replacesId = replacesId;
    record.urgency = static_cast<quint8>(qMin(hints.value(QStringLiteral("urgency")).toUInt(), 255u));
    record.createdAt = now;
    record.updatedAt = now;

    qCInfo(lcNotif) << ">>> Notify: id=" << id << "appName=" << appName << "desktop-entry hint=" << hints.value(QStringLiteral("desktop-entry")).toString()
                    << "resolved=" << desktopEntry << "replacesId=" << replacesId << "summary=" << summary;

    // A reused server ID no longer refers to any previous retained record.
    removeInactiveNotification(id);
    if (auto current = m_notifIdToEntry.find(id); current != m_notifIdToEntry.end()) {
        const QString oldEntry = current.value();
        if (auto setIt = m_unreadNotifs.find(oldEntry); setIt != m_unreadNotifs.end()) {
            setIt->remove(id);
            if (setIt->isEmpty()) {
                m_unreadNotifs.erase(setIt);
            }
        }
        m_notifIdToEntry.erase(current);
    }

    // If replacing, remove the old active ID while retaining its lifecycle.
    if (replacesId > 0) {
        if (auto it = m_notifIdToEntry.find(replacesId); it != m_notifIdToEntry.end()) {
            const QString oldEntry = it.value();
            if (auto setIt = m_unreadNotifs.find(oldEntry); setIt != m_unreadNotifs.end()) {
                setIt->remove(replacesId);
                if (setIt->isEmpty()) {
                    m_unreadNotifs.erase(setIt);
                }
            }
            m_notifIdToEntry.erase(it);
            if (auto oldRecord = m_notificationRecords.find(replacesId); oldRecord != m_notificationRecords.end()) {
                oldRecord->closeReason = CloseReason::Replaced;
                oldRecord->updatedAt = now;
            }
            markNotificationInactive(replacesId);
            qCInfo(lcNotif) << "    replaced old notif id=" << replacesId << "entry=" << oldEntry;
        }
    }

    m_notificationRecords.insert(id, record);
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
    if (auto recordIt = m_notificationRecords.find(id); recordIt != m_notificationRecords.end()) {
        recordIt->closeReason = CloseReason::Directed;
        recordIt->updatedAt = QDateTime::currentDateTimeUtc();
        recordIt->broadcastReason = 0;
        markNotificationInactive(id);
    }

    bumpRevision();
    qCInfo(lcNotif) << "<<< CloseNotification (directed): id=" << id << "entry=" << entry;
}

void NotificationTracker::NotificationClosed(uint id, uint reason)
{
    auto it = m_notifIdToEntry.find(id);
    if (it == m_notifIdToEntry.end()) {
        if (auto recordIt = m_notificationRecords.find(id); recordIt != m_notificationRecords.end()
            && recordIt->closeReason == CloseReason::Directed) {
            recordIt->broadcastReason = reason;
            recordIt->updatedAt = QDateTime::currentDateTimeUtc();
            markNotificationInactive(id);
            bumpRevision();
        }
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
    if (auto recordIt = m_notificationRecords.find(id); recordIt != m_notificationRecords.end()) {
        switch (reason) {
        case 1:
            recordIt->closeReason = CloseReason::BroadcastExpired;
            break;
        case 2:
            recordIt->closeReason = CloseReason::BroadcastDismissed;
            break;
        case 3:
            recordIt->closeReason = CloseReason::BroadcastClosedByCall;
            break;
        default:
            recordIt->closeReason = CloseReason::BroadcastUndefined;
            break;
        }
        recordIt->broadcastReason = reason;
        recordIt->updatedAt = QDateTime::currentDateTimeUtc();
        markNotificationInactive(id);
    }

    bumpRevision();
    qCInfo(lcNotif) << "<<< NotificationClosed (broadcast): id=" << id << "reason=" << reason << "entry=" << entry;
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
    pruneInactiveNotificationRecords();
    resetNotificationModel();
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
