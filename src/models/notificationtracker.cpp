// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "notificationtracker.h"

#include <settings.h>

#include <KService>
#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusArgument>

#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcNotif, "krema.notifications")

namespace krema
{
namespace
{
constexpr qsizetype kMaxInactiveNotificationRecords = 100;
constexpr auto kNotificationService = "org.freedesktop.Notifications";
constexpr auto kSniWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kSniKeySeparator = QLatin1Char('|');

static QString sniKey(const QString &service, const QString &objectPath)
{
    return service + kSniKeySeparator + objectPath;
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

struct SniToolTip {
    QString title;
    QString subTitle;
};

static SniToolTip parseSniToolTip(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        if (argument.currentType() == QDBusArgument::VariantType) {
            QDBusVariant nested;
            argument >> nested;
            return parseSniToolTip(nested.variant());
        }
        if (argument.currentType() == QDBusArgument::MapType) {
            SniToolTip result;
            argument.beginMap();
            while (!argument.atEnd()) {
                QString key;
                QDBusVariant mapValue;
                argument >> key >> mapValue;
                if (key == QLatin1String("title")) {
                    result.title = mapValue.variant().toString();
                } else if (key == QLatin1String("description")) {
                    result.subTitle = mapValue.variant().toString();
                }
            }
            argument.endMap();
            return result;
        }
        if (argument.currentType() != QDBusArgument::StructureType) {
            return {};
        }
        SniToolTip result;
        QString iconName;
        argument.beginStructure();
        argument >> iconName;
        argument.beginArray();
        while (!argument.atEnd()) {
            argument.beginStructure();
            qint32 width = 0;
            qint32 height = 0;
            QByteArray pixmap;
            argument >> width >> height >> pixmap;
            argument.endStructure();
        }
        argument.endArray();
        argument >> result.title >> result.subTitle;
        argument.endStructure();
        return result;
    }
    if (const auto map = value.toMap(); !map.isEmpty()) {
        return {map.value(QStringLiteral("title")).toString(), map.value(QStringLiteral("description")).toString()};
    }
    const QStringList list = value.toStringList();
    if (list.size() >= 2) {
        return {list.at(list.size() - 2), list.at(list.size() - 1)};
    }
    return {};
}
} // namespace

NotificationTracker::NotificationTracker(QObject *parent)
    : QAbstractListModel(parent)
    , m_notificationServiceWatcher(QStringLiteral("org.freedesktop.Notifications"), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange)
    , m_sniWatcherServiceWatcher(QStringLiteral("org.kde.StatusNotifierWatcher"), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange)
    , m_sniServiceWatcher(QString(), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForUnregistration)
    , m_dndExpiryTimer(this)
{
    // Settings — must load() explicitly (constructor doesn't auto-load)
    m_notifSettings = std::make_unique<NotificationManager::Settings>(this);
    m_notifSettings->load();

    qCInfo(lcNotif) << "NotificationTracker init: badgesInTaskManager=" << (m_notifSettings ? m_notifSettings->badgesInTaskManager() : true)
                    << "blacklist=" << (m_notifSettings ? m_notifSettings->badgeBlacklistedApplications() : QStringList());

    // DND state tracking
    connect(m_notifSettings.get(), &NotificationManager::Settings::settingsChanged, this, &NotificationTracker::updateDndState);
    connect(m_notifSettings.get(), &NotificationManager::Settings::notificationsInhibitedByApplicationChanged, this, &NotificationTracker::updateDndState);
    m_dndExpiryTimer.setSingleShot(true);
    connect(&m_dndExpiryTimer, &QTimer::timeout, this, &NotificationTracker::updateDndState);
    setupNotificationWatcher();
    setupSniWatcher();
    updateDndState();
}

NotificationTracker::~NotificationTracker()
{
    m_dndExpiryTimer.stop();
    if (!m_notificationWatcherRegistered) {
        return;
    }
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
        ids.append(it.key());
    }
    std::sort(ids.begin(), ids.end());
    beginResetModel();
    m_modelIds = std::move(ids);
    endResetModel();
}

void NotificationTracker::updateDndState()
{
    m_dndExpiryTimer.stop();
    bool active = false;
    if (m_notifSettings) {
        active = m_notifSettings->notificationsInhibitedByApplication();
        if (!active) {
            const auto until = m_notifSettings->notificationsInhibitedUntil();
            const auto now = QDateTime::currentDateTime();
            if (until.isValid() && until > now) {
                active = true;
                const qint64 msUntil = now.msecsTo(until);
                // QTimer uses int milliseconds on older Qt releases. A capped
                // timeout rechecks the absolute deadline before ending DND.
                const auto delay = std::chrono::milliseconds(std::clamp<qint64>(msUntil, 1, std::numeric_limits<int>::max()));
                m_dndExpiryTimer.start(delay);
                qCDebug(lcNotif) << "DND expiry scheduled in" << delay.count() << "ms";
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

const NotificationTracker::SniEntry *NotificationTracker::sniEntryForApp(const QString &appId) const
{
    if (appId.isEmpty()) {
        return nullptr;
    }
    const QString normalized = canonicalDesktopId(appId);
    if (normalized.isEmpty()) {
        return nullptr;
    }
    const SniEntry *fallback = nullptr;
    for (const auto &entry : m_sniItems) {
        if (canonicalDesktopId(entry.id) != normalized) {
            continue;
        }
        if (entry.status == QLatin1String("NeedsAttention")) {
            return &entry;
        }
        if (!fallback) {
            fallback = &entry;
        }
    }
    return fallback;
}

bool NotificationTracker::sniNeedsAttention(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    if (entry && entry->status == QLatin1String("NeedsAttention")) {
        qCInfo(lcNotif) << "  sniNeedsAttention(" << appId << ") = true (sniId=" << entry->id << ")";
        return true;
    }
    return false;
}

QString NotificationTracker::sniTitle(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->title : QString();
}

QString NotificationTracker::sniAttentionIconName(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->attentionIconName : QString();
}

QString NotificationTracker::sniOverlayIconName(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->overlayIconName : QString();
}

QString NotificationTracker::sniToolTipTitle(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->toolTipTitle : QString();
}

QString NotificationTracker::sniToolTipSubTitle(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->toolTipSubTitle : QString();
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
                if (auto recordIt = m_notificationRecords.find(notifId); recordIt != m_notificationRecords.end()) {
                    recordIt->read = true;
                    recordIt->updatedAt = QDateTime::currentDateTimeUtc();
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
    connect(&m_notificationServiceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &NotificationTracker::onNotificationServiceOwnerChanged);

    // The notification server hardcodes this object path/interface for
    // RegisterWatcher callbacks; it is not the freedesktop server path.
    if (!m_notificationObjectRegistered) {
        if (!bus.registerObject(QStringLiteral("/NotificationWatcher"),
                                QStringLiteral("org.kde.NotificationWatcher"),
                                this,
                                QDBusConnection::ExportScriptableSlots)) {
            qCWarning(lcNotif) << "Failed to register D-Bus notification watcher object:" << bus.lastError().message();
        } else {
            m_notificationObjectRegistered = true;
            qCInfo(lcNotif) << "D-Bus watcher object registered at /NotificationWatcher";
        }
    }

    connectNotificationClosedMatch();

    const auto *dbusInterface = bus.interface();
    if (dbusInterface && dbusInterface->isServiceRegistered(QString::fromLatin1(kNotificationService))) {
        registerNotificationWatcher();
    } else {
        qCInfo(lcNotif) << "Notification server is not owned yet; waiting for service registration";
    }
}

void NotificationTracker::connectNotificationClosedMatch()
{
    if (m_notificationClosedMatchConnected) {
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    if (!bus.connect(QString::fromLatin1(kNotificationService),
                     QStringLiteral("/org/freedesktop/Notifications"),
                     QStringLiteral("org.freedesktop.Notifications"),
                     QStringLiteral("NotificationClosed"),
                     this,
                     SLOT(NotificationClosed(uint, uint)))) {
        qCWarning(lcNotif) << "Failed to subscribe to NotificationClosed:" << bus.lastError().message();
        return;
    }
    m_notificationClosedMatchConnected = true;
}

void NotificationTracker::registerNotificationWatcher()
{
    if (!m_notificationObjectRegistered || m_notificationRegisterPending || m_notificationWatcherRegistered) {
        return;
    }

    auto bus = QDBusConnection::sessionBus();
    const auto *dbusInterface = bus.interface();
    if (!dbusInterface || !dbusInterface->isServiceRegistered(QString::fromLatin1(kNotificationService))) {
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(QString::fromLatin1(kNotificationService),
                                                      QStringLiteral("/org/freedesktop/Notifications"),
                                                      QStringLiteral("org.kde.NotificationManager"),
                                                      QStringLiteral("RegisterWatcher"));
    const quint64 generation = ++m_notificationOwnerGeneration;
    m_notificationRegisterPending = true;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_notificationOwnerGeneration) {
            return;
        }
        m_notificationRegisterPending = false;
        if (reply.isError()) {
            qCWarning(lcNotif) << "RegisterWatcher FAILED:" << reply.error().message();
            return;
        }
        m_notificationWatcherRegistered = true;
        qCInfo(lcNotif) << "RegisterWatcher succeeded — now receiving notifications";
    });
}

void NotificationTracker::onNotificationServiceOwnerChanged(const QString &service, const QString &, const QString &newOwner)
{
    if (service != QString::fromLatin1(kNotificationService)) {
        return;
    }
    ++m_notificationOwnerGeneration;
    m_notificationWatcherRegistered = false;
    m_notificationRegisterPending = false;
    qCInfo(lcNotif) << "Notification server owner changed; watcher registration invalidated";
    if (!newOwner.isEmpty()) {
        registerNotificationWatcher();
    }
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

    if (!m_sniWatcherRegisteredMatchConnected) {
        m_sniWatcherRegisteredMatchConnected =
            bus.connect(QString::fromLatin1(kSniWatcherService),
                        QStringLiteral("/StatusNotifierWatcher"),
                        QStringLiteral("org.kde.StatusNotifierWatcher"),
                        QStringLiteral("StatusNotifierItemRegistered"),
                        this,
                        SLOT(onSniRegistered(QString)));
        if (!m_sniWatcherRegisteredMatchConnected) {
            qCWarning(lcNotif) << "Failed to subscribe to StatusNotifierItemRegistered:" << bus.lastError().message();
        }
    }
    if (!m_sniWatcherUnregisteredMatchConnected) {
        m_sniWatcherUnregisteredMatchConnected =
            bus.connect(QString::fromLatin1(kSniWatcherService),
                        QStringLiteral("/StatusNotifierWatcher"),
                        QStringLiteral("org.kde.StatusNotifierWatcher"),
                        QStringLiteral("StatusNotifierItemUnregistered"),
                        this,
                        SLOT(onSniUnregistered(QString)));
        if (!m_sniWatcherUnregisteredMatchConnected) {
            qCWarning(lcNotif) << "Failed to subscribe to StatusNotifierItemUnregistered:" << bus.lastError().message();
        }
    }
    if (!m_sniStatusMatchConnected) {
        m_sniStatusMatchConnected =
            bus.connect(QString(),
                        QString(),
                        QStringLiteral("org.kde.StatusNotifierItem"),
                        QStringLiteral("NewStatus"),
                        this,
                        SLOT(onSniNewStatus(QString, QDBusMessage)));
        if (!m_sniStatusMatchConnected) {
            qCWarning(lcNotif) << "Failed to subscribe to StatusNotifierItem.NewStatus:" << bus.lastError().message();
        }
    }
    const auto connectSniSignal = [this, &bus](bool &connected, const QString &signal, const char *slot) {
        if (connected) {
            return;
        }
        connected = bus.connect(QString(), QString(), QStringLiteral("org.kde.StatusNotifierItem"), signal, this, slot);
        if (!connected) {
            qCWarning(lcNotif) << "Failed to subscribe to StatusNotifierItem." << signal << ":" << bus.lastError().message();
        }
    };
    connectSniSignal(m_sniTitleMatchConnected, QStringLiteral("NewTitle"), SLOT(onSniNewTitle(QDBusMessage)));
    connectSniSignal(m_sniAttentionIconMatchConnected, QStringLiteral("NewAttentionIcon"), SLOT(onSniNewAttentionIcon(QDBusMessage)));
    connectSniSignal(m_sniOverlayIconMatchConnected, QStringLiteral("NewOverlayIcon"), SLOT(onSniNewOverlayIcon(QDBusMessage)));
    connectSniSignal(m_sniToolTipMatchConnected, QStringLiteral("NewToolTip"), SLOT(onSniNewToolTip(QDBusMessage)));

    connect(&m_sniServiceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &NotificationTracker::onSniServiceUnregistered);
    connect(&m_sniWatcherServiceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &NotificationTracker::onSniWatcherServiceOwnerChanged);

    const auto *dbusInterface = bus.interface();
    if (dbusInterface && dbusInterface->isServiceRegistered(QString::fromLatin1(kSniWatcherService))) {
        queryRegisteredSniItems();
    } else {
        qCInfo(lcNotif) << "SNI watcher is not owned yet; waiting for service registration";
    }
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
    QDBusMessage msg = QDBusMessage::createMethodCall(QString::fromLatin1(kSniWatcherService),
                                                      QStringLiteral("/StatusNotifierWatcher"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    msg << QStringLiteral("org.kde.StatusNotifierWatcher") << QStringLiteral("RegisteredStatusNotifierItems");

    const quint64 watcherGeneration = m_sniWatcherGeneration;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcherGeneration](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusVariant> reply = *w;
        w->deleteLater();
        if (watcherGeneration != m_sniWatcherGeneration) {
            return;
        }
        if (reply.isError()) {
            qCWarning(lcNotif) << "Failed to query SNI items:" << reply.error().message();
            return;
        }

        const QStringList items = reply.value().variant().toStringList();
        qCInfo(lcNotif) << "Registered SNI items:" << items.size() << items;
        for (const QString &item : items) {
            onSniRegistered(item);
        }
    });
}

void NotificationTracker::trackSniItem(const QString &service, const QString &objectPath)
{
    if (service.isEmpty() || objectPath.isEmpty() || !objectPath.startsWith(QLatin1Char('/'))) {
        qCWarning(lcNotif) << "Ignoring malformed SNI item:" << service << objectPath;
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    const auto *dbusInterface = bus.interface();
    if (!dbusInterface || !dbusInterface->isServiceRegistered(service)) {
        qCInfo(lcNotif) << "Ignoring SNI item whose service is no longer owned:" << service << objectPath;
        return;
    }
    const QString key = sniKey(service, objectPath);
    if (m_sniItems.contains(key)) {
        return;
    }

    m_sniServiceWatcher.addWatchedService(service);
    const quint64 generation = ++m_sniItemGeneration;
    m_sniItems.insert(key, SniEntry{QString(), QStringLiteral("Passive"), {}, {}, {}, {}, {}, generation});

    QDBusMessage idMsg = QDBusMessage::createMethodCall(service, objectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    idMsg << QStringLiteral("org.kde.StatusNotifierItem") << QStringLiteral("Id");
    auto *idWatcher = new QDBusPendingCallWatcher(bus.asyncCall(idMsg), this);
    connect(idWatcher, &QDBusPendingCallWatcher::finished, this, [this, key, generation](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusVariant> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            qCInfo(lcNotif) << "SNI Id query failed for" << key << ":" << reply.error().message();
            return;
        }
        const auto it = m_sniItems.find(key);
        if (it == m_sniItems.end() || it->generation != generation) {
            return;
        }
        const QString newId = reply.value().variant().toString();
        if (it->id == newId) {
            return;
        }
        it->id = newId;
        qCInfo(lcNotif) << "SNI tracked:" << key << "id=" << it->id;
        bumpRevision();
    });

    QDBusMessage statusMsg = QDBusMessage::createMethodCall(service, objectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    statusMsg << QStringLiteral("org.kde.StatusNotifierItem") << QStringLiteral("Status");
    auto *statusWatcher = new QDBusPendingCallWatcher(bus.asyncCall(statusMsg), this);
    connect(statusWatcher, &QDBusPendingCallWatcher::finished, this, [this, key, generation](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusVariant> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            qCInfo(lcNotif) << "SNI Status query failed for" << key << ":" << reply.error().message();
            return;
        }
        const auto it = m_sniItems.find(key);
        if (it == m_sniItems.end() || it->generation != generation) {
            return;
        }
        const QString newStatus = reply.value().variant().toString();
        if (it->status != newStatus) {
            qCInfo(lcNotif) << "SNI initial status:" << key << it->status << "->" << newStatus;
            it->status = newStatus;
            bumpRevision();
        }
    });
    querySniProperty(key, generation, service, objectPath, QStringLiteral("Title"));
    querySniProperty(key, generation, service, objectPath, QStringLiteral("AttentionIconName"));
    querySniProperty(key, generation, service, objectPath, QStringLiteral("OverlayIconName"));
    querySniProperty(key, generation, service, objectPath, QStringLiteral("ToolTip"));
}

void NotificationTracker::querySniProperty(const QString &key,
                                            quint64 generation,
                                            const QString &service,
                                            const QString &objectPath,
                                            const QString &property)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, objectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QStringLiteral("org.kde.StatusNotifierItem") << property;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, key, generation, property](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusVariant> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            qCInfo(lcNotif) << "SNI property query failed for" << key << property << ":" << reply.error().message();
            return;
        }
        auto it = m_sniItems.find(key);
        if (it == m_sniItems.end() || it->generation != generation) {
            return;
        }
        const QVariant value = reply.value().variant();
        QString newValue;
        if (property == QLatin1String("ToolTip")) {
            const auto toolTip = parseSniToolTip(value);
            bool changed = it->toolTipTitle != toolTip.title || it->toolTipSubTitle != toolTip.subTitle;
            it->toolTipTitle = toolTip.title;
            it->toolTipSubTitle = toolTip.subTitle;
            if (changed) {
                bumpRevision();
            }
            return;
        }
        newValue = value.toString();
        QString *storedValue = nullptr;
        if (property == QLatin1String("Title")) {
            storedValue = &it->title;
        } else if (property == QLatin1String("AttentionIconName")) {
            storedValue = &it->attentionIconName;
        } else if (property == QLatin1String("OverlayIconName")) {
            storedValue = &it->overlayIconName;
        }
        if (storedValue && *storedValue != newValue) {
            *storedValue = newValue;
            bumpRevision();
        }
    });
}
void NotificationTracker::clearSniItems()
{
    if (m_sniItems.isEmpty()) {
        return;
    }
    for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
        const int separator = it.key().indexOf(kSniKeySeparator);
        if (separator > 0) {
            m_sniServiceWatcher.removeWatchedService(it.key().left(separator));
        }
    }
    m_sniItems.clear();
    bumpRevision();
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
    const QString service = slashIdx > 0 ? item.left(slashIdx) : item;
    const QString objectPath = slashIdx > 0 ? item.mid(slashIdx) : QStringLiteral("/StatusNotifierItem");
    const QString key = sniKey(service, objectPath);

    if (m_sniItems.remove(key) > 0) {
        bool hasMoreItems = false;
        const QString prefix = service + kSniKeySeparator;
        for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
            if (it.key().startsWith(prefix)) {
                hasMoreItems = true;
                break;
            }
        }
        if (!hasMoreItems) {
            m_sniServiceWatcher.removeWatchedService(service);
        }
        bumpRevision();
        qCInfo(lcNotif) << "SNI unregistered:" << service << objectPath;
    }
}

QString NotificationTracker::sniKeyForMessage(const QDBusMessage &message) const
{
    const QString directKey = sniKey(message.service(), message.path());
    if (m_sniItems.contains(directKey)) {
        return directKey;
    }

    const auto *dbusInterface = QDBusConnection::sessionBus().interface();
    for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
        const int separator = it.key().indexOf(kSniKeySeparator);
        if (separator <= 0 || it.key().mid(separator + 1) != message.path()) {
            continue;
        }

        const QString registeredService = it.key().left(separator);
        if (registeredService == message.service()
            || (dbusInterface && dbusInterface->serviceOwner(registeredService) == message.service())) {
            return it.key();
        }
    }
    return directKey;
}

void NotificationTracker::onSniNewStatus(const QString &status, const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);

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
void NotificationTracker::onSniNewTitle(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    const auto it = m_sniItems.constFind(key);
    if (it != m_sniItems.cend()) {
        querySniProperty(key, it->generation, message.service(), message.path(), QStringLiteral("Title"));
    }
}

void NotificationTracker::onSniNewAttentionIcon(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    const auto it = m_sniItems.constFind(key);
    if (it != m_sniItems.cend()) {
        querySniProperty(key, it->generation, message.service(), message.path(), QStringLiteral("AttentionIconName"));
    }
}

void NotificationTracker::onSniNewOverlayIcon(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    const auto it = m_sniItems.constFind(key);
    if (it != m_sniItems.cend()) {
        querySniProperty(key, it->generation, message.service(), message.path(), QStringLiteral("OverlayIconName"));
    }
}

void NotificationTracker::onSniNewToolTip(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    const auto it = m_sniItems.constFind(key);
    if (it != m_sniItems.cend()) {
        querySniProperty(key, it->generation, message.service(), message.path(), QStringLiteral("ToolTip"));
    }
}

void NotificationTracker::onSniServiceUnregistered(const QString &service)
{
    const QString prefix = service + kSniKeySeparator;
    bool changed = false;
    for (auto it = m_sniItems.begin(); it != m_sniItems.end();) {
        if (it.key().startsWith(prefix)) {
            qCInfo(lcNotif) << "SNI service gone:" << service << it.key() << "id=" << it->id;
            it = m_sniItems.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    m_sniServiceWatcher.removeWatchedService(service);
    if (changed) {
        bumpRevision();
    }
}

void NotificationTracker::onSniWatcherServiceOwnerChanged(const QString &service, const QString &, const QString &newOwner)
{
    if (service != QString::fromLatin1(kSniWatcherService)) {
        return;
    }
    ++m_sniWatcherGeneration;
    qCInfo(lcNotif) << "SNI watcher owner changed; clearing tracked items";
    clearSniItems();
    if (!newOwner.isEmpty()) {
        queryRegisteredSniItems();
    }
}

} // namespace krema
