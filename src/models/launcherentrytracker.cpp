// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "launcherentrytracker.h"

#include <settings.h>

#include <KConfigGroup>
#include <KDesktopFile>
#include <KService>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <limits>

Q_LOGGING_CATEGORY(lcLauncherEntry, "krema.launcherentry")

namespace krema
{

static QString stripDesktopSuffix(const QString &id)
{
    static const QLatin1String suffix(".desktop");
    return id.endsWith(suffix) ? id.left(id.size() - suffix.size()) : id;
}

LauncherEntryTracker::LauncherEntryTracker(QObject *parent)
    : QObject(parent)
    , m_serviceWatcher(QString(), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForUnregistration)
{
    // Settings — must load() explicitly (constructor doesn't auto-load)
    m_settings = std::make_unique<NotificationManager::Settings>(this);
    m_settings->load();
    connect(m_settings.get(), &NotificationManager::Settings::settingsChanged, this, &LauncherEntryTracker::loadSettings);
    loadSettings();

    connect(&m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &LauncherEntryTracker::onServiceUnregistered);

    auto bus = QDBusConnection::sessionBus();
    // Apps broadcast Update from their own object path; listen to all senders.
    if (!bus.connect(QString(),
                     QString(),
                     QStringLiteral("com.canonical.Unity.LauncherEntry"),
                     QStringLiteral("Update"),
                     this,
                     SLOT(update(QString, QVariantMap)))) {
        qCWarning(lcLauncherEntry) << "Failed to subscribe to com.canonical.Unity.LauncherEntry.Update";
    }

    // Like Plasma's task manager: announce the Unity service for apps that
    // look for it. When plasmashell (or another dock) owns it, just listen.
    bus.registerObject(QStringLiteral("/Unity"), this);
    if (!bus.registerService(QStringLiteral("com.canonical.Unity"))) {
        qCInfo(lcLauncherEntry) << "com.canonical.Unity is owned by another process; listening only";
    }

    const KConfigGroup mapping(KSharedConfig::openConfig(QStringLiteral("taskmanagerrulesrc")), QStringLiteral("Unity Launcher Mapping"));
    const QStringList keys = mapping.keyList();
    for (const QString &key : keys) {
        const QString value = mapping.readEntry(key, QString());
        if (!value.isEmpty()) {
            m_mappingRules.insert(key, value);
        }
    }
}

LauncherEntryTracker::~LauncherEntryTracker() = default;

int LauncherEntryTracker::revision() const
{
    return m_revision;
}

void LauncherEntryTracker::bumpRevision()
{
    ++m_revision;
    Q_EMIT revisionChanged();
}

void LauncherEntryTracker::loadSettings()
{
    // Desktop entries ("foo"); compared without the storage ID's ".desktop".
    m_badgeBlacklist = m_settings->badgeBlacklistedApplications();
    for (QString &entry : m_badgeBlacklist) {
        entry = stripDesktopSuffix(entry);
    }
    bumpRevision();
}

bool LauncherEntryTracker::countAllowed(const QString &storageId) const
{
    // Unlike Plasma's task manager, Do Not Disturb does not hide counts:
    // Krema keeps every badge in DND and only suppresses the attention
    // animation (DockItem.qml).
    return m_settings->badgesInTaskManager() && !m_badgeBlacklist.contains(stripDesktopSuffix(storageId), Qt::CaseInsensitive);
}

QString LauncherEntryTracker::storageIdFor(const QUrl &launcherUrl) const
{
    if (launcherUrl.isEmpty()) {
        return {};
    }
    if (const auto cached = m_storageIdCache.constFind(launcherUrl); cached != m_storageIdCache.constEnd()) {
        return *cached;
    }

    // Same resolution as Plasma's SmartLauncher item.
    QString storageId;
    if (launcherUrl.scheme() == QLatin1String("applications")) {
        const KService::Ptr service = KService::serviceByMenuId(launcherUrl.path());
        if (service && launcherUrl.path() == service->menuId()) {
            storageId = service->menuId();
        }
    }
    if (launcherUrl.isLocalFile() && KDesktopFile::isDesktopFile(launcherUrl.toLocalFile())) {
        const KDesktopFile desktopFile(launcherUrl.toLocalFile());
        if (const KService::Ptr service = KService::serviceByStorageId(desktopFile.fileName())) {
            storageId = service->storageId();
        }
    }
    if (storageId.isEmpty()) {
        // Not cached: the app may be installed later.
        return {};
    }

    storageId = m_mappingRules.value(storageId, storageId);
    m_storageIdCache.insert(launcherUrl, storageId);
    return storageId;
}

int LauncherEntryTracker::count(const QUrl &launcherUrl) const
{
    const QString storageId = storageIdFor(launcherUrl);
    if (storageId.isEmpty() || !countAllowed(storageId)) {
        return 0;
    }
    return m_entries.value(storageId).count;
}

bool LauncherEntryTracker::countVisible(const QUrl &launcherUrl) const
{
    const QString storageId = storageIdFor(launcherUrl);
    if (storageId.isEmpty() || !countAllowed(storageId)) {
        return false;
    }
    return m_entries.value(storageId).countVisible;
}

int LauncherEntryTracker::progress(const QUrl &launcherUrl) const
{
    const QString storageId = storageIdFor(launcherUrl);
    return storageId.isEmpty() ? 0 : m_entries.value(storageId).progress;
}

bool LauncherEntryTracker::progressVisible(const QUrl &launcherUrl) const
{
    const QString storageId = storageIdFor(launcherUrl);
    return !storageId.isEmpty() && m_entries.value(storageId).progressVisible;
}

bool LauncherEntryTracker::urgent(const QUrl &launcherUrl) const
{
    const QString storageId = storageIdFor(launcherUrl);
    return !storageId.isEmpty() && m_entries.value(storageId).urgent;
}

void LauncherEntryTracker::update(const QString &appUri, const QVariantMap &properties)
{
    QString storageId = m_appUriToStorageId.value(appUri);
    if (storageId.isEmpty()) {
        // Apps should send their desktop file name with the application:// prefix.
        static const QLatin1String prefix("application://");
        const QString desktopFile = appUri.startsWith(prefix) ? appUri.mid(prefix.size()) : appUri;
        const KService::Ptr service = KService::serviceByStorageId(desktopFile);
        if (!service) {
            qCDebug(lcLauncherEntry) << "No application for LauncherEntry" << appUri;
            return;
        }
        storageId = service->storageId();
        m_appUriToStorageId.insert(appUri, storageId);
    }

    // The entry lives as long as its latest sender stays on the bus (an app
    // restarted with a new connection takes it over).
    const QString sender = message().service();
    if (m_appUriToService.value(appUri) != sender) {
        m_appUriToService.insert(appUri, sender);
        m_serviceWatcher.addWatchedService(sender);
    }

    Entry &entry = m_entries[storageId];
    const Entry before = entry;

    if (const auto it = properties.constFind(QStringLiteral("count")); it != properties.constEnd()) {
        const qint64 value = it->toLongLong();
        // Out-of-range counts are ignored, as in Plasma.
        if (value >= 0 && value < std::numeric_limits<int>::max()) {
            entry.count = static_cast<int>(value);
        }
    }
    if (const auto it = properties.constFind(QStringLiteral("count-visible")); it != properties.constEnd()) {
        entry.countVisible = it->toBool();
    }
    if (const auto it = properties.constFind(QStringLiteral("progress")); it != properties.constEnd()) {
        // The API sends a 0..1 fraction; keep whole percent so tiny changes do
        // not re-evaluate the dock.
        double value = it->toDouble();
        if (!std::isfinite(value)) {
            value = 0.0;
        }
        entry.progress = static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 100.0));
    }
    if (const auto it = properties.constFind(QStringLiteral("progress-visible")); it != properties.constEnd()) {
        entry.progressVisible = it->toBool();
    }
    if (const auto it = properties.constFind(QStringLiteral("urgent")); it != properties.constEnd()) {
        entry.urgent = it->toBool();
    }

    if (entry.count != before.count || entry.countVisible != before.countVisible || entry.progress != before.progress
        || entry.progressVisible != before.progressVisible || entry.urgent != before.urgent) {
        bumpRevision();
    }
}

void LauncherEntryTracker::onServiceUnregistered(const QString &service)
{
    m_serviceWatcher.removeWatchedService(service);
    bool changed = false;
    for (auto it = m_appUriToService.begin(); it != m_appUriToService.end();) {
        if (it.value() != service) {
            ++it;
            continue;
        }
        const QString storageId = m_appUriToStorageId.take(it.key());
        changed = m_entries.remove(storageId) > 0 || changed;
        it = m_appUriToService.erase(it);
    }
    if (changed) {
        bumpRevision();
    }
}

} // namespace krema
