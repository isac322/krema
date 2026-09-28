// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace NotificationManager
{
class Settings;
}

namespace krema
{

/**
 * Tracks the Unity LauncherEntry API (badge count, progress, urgency) that apps
 * send as com.canonical.Unity.LauncherEntry.Update signals on the session bus.
 *
 * Follows the behavior of Plasma's task manager SmartLauncher backend, which
 * Krema used through the private org.kde.plasma.private.taskmanager QML module
 * until Plasma 6.6 compiled that module into the task manager applet plugin.
 *
 * QML reads the state through Q_INVOKABLE methods keyed by a dock item's
 * launcher URL. Bindings that read `revision` are re-evaluated whenever an
 * entry or the badge settings change.
 */
class LauncherEntryTracker : public QObject, protected QDBusContext
{
    Q_OBJECT

    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit LauncherEntryTracker(QObject *parent = nullptr);
    ~LauncherEntryTracker() override;

    [[nodiscard]] int revision() const;

    /// Badge count for the app behind @p launcherUrl; 0 while Plasma's badge
    /// settings hide badges for it.
    [[nodiscard]] Q_INVOKABLE int count(const QUrl &launcherUrl) const;
    [[nodiscard]] Q_INVOKABLE bool countVisible(const QUrl &launcherUrl) const;
    /// Task progress in percent (0..100).
    [[nodiscard]] Q_INVOKABLE int progress(const QUrl &launcherUrl) const;
    [[nodiscard]] Q_INVOKABLE bool progressVisible(const QUrl &launcherUrl) const;
    [[nodiscard]] Q_INVOKABLE bool urgent(const QUrl &launcherUrl) const;

Q_SIGNALS:
    void revisionChanged();

private Q_SLOTS:
    void update(const QString &appUri, const QVariantMap &properties);

private:
    struct Entry {
        int count = 0;
        bool countVisible = false;
        int progress = 0;
        bool progressVisible = false;
        bool urgent = false;
    };

    void loadSettings();
    void onServiceUnregistered(const QString &service);
    void bumpRevision();

    /// Storage ID ("foo.desktop") of the app behind a dock launcher URL, after
    /// taskmanagerrulesrc's Unity Launcher Mapping. Empty if unknown.
    [[nodiscard]] QString storageIdFor(const QUrl &launcherUrl) const;
    [[nodiscard]] bool countAllowed(const QString &storageId) const;

    std::unique_ptr<NotificationManager::Settings> m_settings;
    QStringList m_badgeBlacklist; // desktop entries without ".desktop"

    QDBusServiceWatcher m_serviceWatcher;
    QHash<QString, QString> m_appUriToService; // latest sender's unique bus name
    QHash<QString, QString> m_appUriToStorageId;
    // Key: desktop file an app is installed as; value: the one it announces
    // itself as on the Unity API.
    QHash<QString, QString> m_mappingRules;
    QHash<QString, Entry> m_entries; // by storage ID

    mutable QHash<QUrl, QString> m_storageIdCache;

    int m_revision = 0;
};

} // namespace krema
