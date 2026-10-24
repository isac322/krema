// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Stage 1 notification watcher state: canonical identity, replacement and
// close lifecycle, notification settings, and reactive clearing.
//
// The public watcher slots are invoked directly. This uses the same observable
// surface as the session-bus callbacks without depending on a particular
// notification server implementation or its transient notification state.

#include "../notification/fixtures/dbuspeers.h"

#include "models/notificationtracker.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <KConfigGroup>
#include <KSharedConfig>
#include <settings.h>
#include <server.h>

#include <QAbstractEventDispatcher>
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <functional>
#include <limits>

namespace
{

constexpr int kTimeoutMs = 5000;

class ScopedTracker
{
public:
    ScopedTracker() = default;
    ~ScopedTracker()
    {
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/NotificationWatcher"));
    }

    ScopedTracker(const ScopedTracker &) = delete;
    ScopedTracker &operator=(const ScopedTracker &) = delete;

    krema::NotificationTracker tracker;
};

} // namespace

class InvokeActionReceiver : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationManager")

public Q_SLOTS:
    void InvokeAction(uint id, const QString &action)
    {
        lastId = id;
        lastAction = action;
        ++callCount;
    }

public:
    uint lastId = 0;
    QString lastAction;
    int callCount = 0;
};

namespace
{

class ScopedInvokeActionReceiver
{
public:
    ScopedInvokeActionReceiver()
    {
        auto bus = QDBusConnection::sessionBus();
        serviceRegistered = bus.registerService(QStringLiteral("org.freedesktop.Notifications"));
        objectRegistered = bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
                                              &receiver,
                                              QDBusConnection::ExportAllSlots);
    }

    ~ScopedInvokeActionReceiver()
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/org/freedesktop/Notifications"));
        if (serviceRegistered) {
            bus.unregisterService(QStringLiteral("org.freedesktop.Notifications"));
        }
    }

    bool ready() const
    {
        return serviceRegistered && objectRegistered;
    }

    InvokeActionReceiver receiver;

private:
    bool serviceRegistered = false;
    bool objectRegistered = false;
};

int roleFor(const QAbstractItemModel *model, const QByteArray &name)
{
    const auto roles = model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == name) {
            return it.key();
        }
    }
    return -1;
}

QVariant roleValue(const QAbstractItemModel *model, int row, const QByteArray &name)
{
    const int role = roleFor(model, name);
    return role < 0 ? QVariant() : model->index(row, 0).data(role);
}

bool waitFor(const std::function<bool()> &condition)
{
    return QTest::qWaitFor(condition, kTimeoutMs);
}

void notify(krema::NotificationTracker &tracker,
            uint id,
            const QString &appName,
            const QString &desktopEntry,
            uint replacesId = 0,
            const QStringList &actions = {},
            const QString &summary = QStringLiteral("Test summary"),
            const QString &body = QStringLiteral("Test body"),
            int timeout = -1,
            uint urgency = 0)
{
    QVariantMap hints;
    if (!desktopEntry.isEmpty()) {
        hints.insert(QStringLiteral("desktop-entry"), desktopEntry);
    }
    if (urgency > 0) {
        hints.insert(QStringLiteral("urgency"), urgency);
    }
    tracker.Notify(id,
                   appName,
                   replacesId,
                   QStringLiteral("test-icon"),
                   summary,
                   body,
                   actions,
                   hints,
                   timeout);
}

} // namespace

TEST_CASE("Notification tracker canonicalizes identities and keeps replacements at one", "[notification][tracker]")
{
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    QSignalSpy revisions(&tracker, &krema::NotificationTracker::revisionChanged);
    const int initialRevision = tracker.revision();

    notify(tracker, 100, QStringLiteral("Mail"), QStringLiteral("Org.Example.Mail.DESKTOP"), 0, {QStringLiteral("default"), QStringLiteral("open")});
    REQUIRE(tracker.revision() == initialRevision + 1);
    REQUIRE(revisions.count() == 1);
    CHECK(tracker.unreadCount(QStringLiteral("org.example.mail")) == 1);
    CHECK(tracker.unreadCount(QStringLiteral("ORG.EXAMPLE.MAIL.desktop")) == 1);
    CHECK(tracker.unreadCount(QStringLiteral("mail")) == 0);

    // Replacing an active notification removes its old ID before inserting the
    // new one, so the app still has one unread notification.
    notify(tracker, 101, QStringLiteral("Mail"), QStringLiteral("org.example.mail.desktop"), 100);
    CHECK(tracker.unreadCount(QStringLiteral("org.example.mail")) == 1);
    CHECK(tracker.revision() == initialRevision + 2);
    CHECK(revisions.count() == 2);

    // Similar final segments are different canonical applications; they must
    // not be merged by a fuzzy suffix comparison.
    notify(tracker, 102, QStringLiteral("Other Mail"), QStringLiteral("com.other.mail"));
    CHECK(tracker.unreadCount(QStringLiteral("org.example.mail")) == 1);
    CHECK(tracker.unreadCount(QStringLiteral("com.other.mail.desktop")) == 1);
    CHECK(tracker.unreadCount(QStringLiteral("mail")) == 0);

    // A directed watcher close and a broadcast close both remove the active ID.
    tracker.CloseNotification(101);
    CHECK(tracker.unreadCount(QStringLiteral("org.example.mail")) == 0);
    CHECK(tracker.revision() == initialRevision + 4);
    CHECK(revisions.count() == 4);

    tracker.NotificationClosed(102, 2); // closed by the notification server
    CHECK(tracker.unreadCount(QStringLiteral("com.other.mail")) == 0);
    CHECK(tracker.revision() == initialRevision + 5);
    CHECK(revisions.count() == 5);
}

TEST_CASE("Notification tracker falls back to app name when no desktop hint exists", "[notification][tracker]")
{
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;

    notify(tracker, 200, QStringLiteral("MyApp"), QString());

    CHECK(tracker.unreadCount(QStringLiteral("myapp")) == 1);
    CHECK(tracker.unreadCount(QStringLiteral("MYAPP.desktop")) == 1);
    CHECK(tracker.unreadCount(QStringLiteral("other-app")) == 0);
}

TEST_CASE("Notification tracker honors Plasma badge settings", "[notification][tracker][settings]")
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc"));
    KConfigGroup badges = config->group(QStringLiteral("Badges"));
    KConfigGroup application = config->group(QStringLiteral("Applications")).group(QStringLiteral("org.example.settings"));

    const bool hadGlobalSetting = badges.hasKey(QStringLiteral("InTaskManager"));
    const QVariant previousGlobalSetting = badges.readEntry(QStringLiteral("InTaskManager"), QVariant());
    const bool hadAppSetting = application.hasKey(QStringLiteral("ShowBadges"));
    const QVariant previousAppSetting = application.readEntry(QStringLiteral("ShowBadges"), QVariant());

    const auto restore = qScopeGuard([&] {
        if (hadGlobalSetting) {
            badges.writeEntry(QStringLiteral("InTaskManager"), previousGlobalSetting, KConfig::Notify);
        } else {
            badges.deleteEntry(QStringLiteral("InTaskManager"), KConfig::Notify);
        }
        if (hadAppSetting) {
            application.writeEntry(QStringLiteral("ShowBadges"), previousAppSetting, KConfig::Notify);
        } else {
            application.deleteEntry(QStringLiteral("ShowBadges"), KConfig::Notify);
        }
        config->sync();
    });

    badges.writeEntry(QStringLiteral("InTaskManager"), true, KConfig::Notify);
    application.writeEntry(QStringLiteral("ShowBadges"), true, KConfig::Notify);
    config->sync();

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    notify(tracker, 300, QStringLiteral("Settings"), QStringLiteral("org.example.settings"));
    REQUIRE(tracker.unreadCount(QStringLiteral("org.example.settings")) == 1);

    badges.writeEntry(QStringLiteral("InTaskManager"), false, KConfig::Notify);
    config->sync();
    REQUIRE(waitFor([&] {
        return tracker.unreadCount(QStringLiteral("org.example.settings")) == 0;
    }));

    badges.writeEntry(QStringLiteral("InTaskManager"), true, KConfig::Notify);
    application.writeEntry(QStringLiteral("ShowBadges"), false, KConfig::Notify);
    config->sync();
    REQUIRE(waitFor([&] {
        return tracker.unreadCount(QStringLiteral("org.example.settings")) == 0;
    }));
}

TEST_CASE("Notification tracker ignores a duplicate broadcast after directed close", "[notification][tracker]")
{
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    QSignalSpy revisions(&tracker, &krema::NotificationTracker::revisionChanged);

    notify(tracker, 350, QStringLiteral("Mail"), QStringLiteral("org.example.mail"));
    REQUIRE(tracker.unreadCount(QStringLiteral("org.example.mail")) == 1);

    tracker.CloseNotification(350);
    REQUIRE(tracker.unreadCount(QStringLiteral("org.example.mail")) == 0);
    const int revisionAfterDirectedClose = tracker.revision();
    const int signalsAfterDirectedClose = revisions.count();

    // The broadcast callback retains its close reason and emits one revision,
    // but must not decrement the already-cleared unread state again.
    tracker.NotificationClosed(350, 2);
    CHECK(tracker.unreadCount(QStringLiteral("org.example.mail")) == 0);
    CHECK(tracker.revision() == revisionAfterDirectedClose + 1);
    CHECK(revisions.count() == signalsAfterDirectedClose + 1);
}

TEST_CASE("Clearing notifications emits one revision only when state changes", "[notification][tracker]")
{
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    QSignalSpy revisions(&tracker, &krema::NotificationTracker::revisionChanged);

    notify(tracker, 400, QStringLiteral("Clear App"), QStringLiteral("org.example.clear.desktop"));
    notify(tracker, 401, QStringLiteral("Clear App"), QStringLiteral("org.example.clear"));
    notify(tracker, 402, QStringLiteral("Other App"), QStringLiteral("org.example.other"));
    REQUIRE(tracker.unreadCount(QStringLiteral("org.example.clear")) == 2);
    REQUIRE(tracker.unreadCount(QStringLiteral("org.example.other")) == 1);

    const int revisionBeforeClear = tracker.revision();
    const int signalsBeforeClear = revisions.count();
    tracker.clearUnreadNotifications(QStringLiteral("ORG.EXAMPLE.CLEAR.DESKTOP"));

    CHECK(tracker.unreadCount(QStringLiteral("org.example.clear")) == 0);
    CHECK(tracker.unreadCount(QStringLiteral("org.example.other")) == 1);
    CHECK(tracker.revision() == revisionBeforeClear + 1);
    CHECK(revisions.count() == signalsBeforeClear + 1);

    tracker.clearUnreadNotifications(QStringLiteral("org.example.clear"));
    CHECK(tracker.revision() == revisionBeforeClear + 1);
    CHECK(revisions.count() == signalsBeforeClear + 1);
}

TEST_CASE("Clearing notifications keeps server-active records active", "[notification][model][tracker]")
{
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;

    notify(tracker, 410, QStringLiteral("Clear App"), QStringLiteral("org.example.clear"));
    REQUIRE(tracker.unreadCount(QStringLiteral("org.example.clear")) == 1);
    REQUIRE(tracker.notifications(QStringLiteral("org.example.clear"), true).size() == 1);

    tracker.clearUnreadNotifications(QStringLiteral("org.example.clear"));

    CHECK(tracker.unreadCount(QStringLiteral("org.example.clear")) == 0);
    const auto activeRows = tracker.notifications(QStringLiteral("org.example.clear"), true);
    REQUIRE(activeRows.size() == 1);
    CHECK(activeRows.constFirst().toMap().value(QStringLiteral("active")).toBool());
    CHECK(roleValue(&tracker, 0, "closeReason").toString() == QStringLiteral("active"));

    tracker.NotificationClosed(410, 2);

    CHECK(tracker.notifications(QStringLiteral("org.example.clear"), true).isEmpty());
    CHECK(roleValue(&tracker, 0, "closeReason").toString() == QStringLiteral("broadcast-dismissed"));
}



TEST_CASE("Notification model exposes replacement metadata and preserves payload fields", "[notification][model]")
{
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;

    notify(tracker,
           501,
           QStringLiteral("Mail"),
           QStringLiteral("org.example.mail"),
           0,
           {QStringLiteral("open"), QStringLiteral("Open")},
           QStringLiteral("Original summary"),
           QStringLiteral("Original body"),
           12000,
           2);

    REQUIRE(tracker.rowCount() == 1);
    CHECK(roleValue(&tracker, 0, "id").toUInt() == 501);
    CHECK(roleValue(&tracker, 0, "desktopEntry").toString() == QStringLiteral("org.example.mail"));
    CHECK(roleValue(&tracker, 0, "appName").toString() == QStringLiteral("Mail"));
    CHECK(roleValue(&tracker, 0, "summary").toString() == QStringLiteral("Original summary"));
    CHECK(roleValue(&tracker, 0, "body").toString() == QStringLiteral("Original body"));
    CHECK(roleValue(&tracker, 0, "actions").toStringList() == QStringList{QStringLiteral("open"), QStringLiteral("Open")});
    CHECK(roleValue(&tracker, 0, "timeout").toInt() == 12000);
    CHECK(roleValue(&tracker, 0, "urgency").toInt() == 2);
    CHECK(roleValue(&tracker, 0, "active").toBool());
    CHECK_FALSE(roleValue(&tracker, 0, "read").toBool());
    CHECK_FALSE(roleValue(&tracker, 0, "dismissed").toBool());

    notify(tracker,
           502,
           QStringLiteral("Mail"),
           QStringLiteral("org.example.mail"),
           501,
           {QStringLiteral("reply"), QStringLiteral("Reply")},
           QStringLiteral("Replacement summary"),
           QStringLiteral("Replacement body"),
           3000,
           1);

    REQUIRE(tracker.rowCount() == 2);
    CHECK(roleValue(&tracker, 0, "id").toUInt() == 501);
    CHECK_FALSE(roleValue(&tracker, 0, "active").toBool());
    CHECK(roleValue(&tracker, 0, "closeReason").toString() == QStringLiteral("replaced"));
    CHECK(roleValue(&tracker, 1, "id").toUInt() == 502);
    CHECK(roleValue(&tracker, 1, "replacesId").toUInt() == 501);
    CHECK(roleValue(&tracker, 1, "summary").toString() == QStringLiteral("Replacement summary"));
    CHECK(roleValue(&tracker, 1, "body").toString() == QStringLiteral("Replacement body"));
    CHECK(roleValue(&tracker, 1, "actions").toStringList() == QStringList{QStringLiteral("reply"), QStringLiteral("Reply")});
    CHECK(roleValue(&tracker, 1, "timeout").toInt() == 3000);
    CHECK(roleValue(&tracker, 1, "urgency").toInt() == 1);
}

TEST_CASE("Notification model bounds inactive history without evicting active records", "[notification][model][retention]")
{
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;

    notify(tracker, 10000, QStringLiteral("History"), QStringLiteral("org.example.history"));
    for (uint id = 9000; id < 9101; ++id) {
        notify(tracker, id, QStringLiteral("History"), QStringLiteral("org.example.history"));
        tracker.NotificationClosed(id, 1);
    }

    const auto rows = tracker.notifications(QStringLiteral("org.example.history"));
    REQUIRE(rows.size() == 101);
    CHECK(tracker.notifications(QStringLiteral("org.example.history"), true).size() == 1);

    bool oldestPresent = false;
    bool newestPresent = false;
    bool activePresent = false;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const uint id = row.value(QStringLiteral("id")).toUInt();
        oldestPresent |= id == 9000;
        newestPresent |= id == 9100;
        activePresent |= id == 10000 && row.value(QStringLiteral("active")).toBool();
    }
    CHECK_FALSE(oldestPresent);
    CHECK(newestPresent);
    CHECK(activePresent);
}

TEST_CASE("Notification model tracks local read and dismiss state", "[notification][model]")
{
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;

    notify(tracker, 601, QStringLiteral("Reader"), QStringLiteral("org.example.reader"));
    REQUIRE(tracker.rowCount() == 1);
    CHECK(tracker.notifications(QStringLiteral("org.example.reader")).size() == 1);

    REQUIRE(tracker.markRead(601));
    CHECK(roleValue(&tracker, 0, "read").toBool());
    CHECK(tracker.unreadCount(QStringLiteral("org.example.reader")) == 0);
    CHECK(roleValue(&tracker, 0, "active").toBool());

    REQUIRE(tracker.markDismissed(601));
    CHECK(roleValue(&tracker, 0, "dismissed").toBool());
    CHECK(roleValue(&tracker, 0, "active").toBool());
}

TEST_CASE("Notification model dispatches actions through KDE notification manager", "[notification][model][dbus]")
{
    ScopedInvokeActionReceiver receiver;
    REQUIRE(receiver.ready());

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    notify(tracker,
           701,
           QStringLiteral("Actions"),
           QStringLiteral("org.example.actions"),
           0,
           {QStringLiteral("open"), QStringLiteral("Open")});

    REQUIRE(tracker.invokeAction(701, QStringLiteral("open")));
    REQUIRE(waitFor([&] {
        return receiver.receiver.callCount == 1;
    }));
    CHECK(receiver.receiver.lastId == 701);
    CHECK(receiver.receiver.lastAction == QStringLiteral("open"));
}

TEST_CASE("Notification model retains broadcast close reason", "[notification][model][tracker]")
{
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;

    notify(tracker, 801, QStringLiteral("Closer"), QStringLiteral("org.example.closer"));
    REQUIRE(tracker.rowCount() == 1);

    tracker.NotificationClosed(801, 2);

    REQUIRE(tracker.rowCount() == 1);
    CHECK_FALSE(roleValue(&tracker, 0, "active").toBool());
    CHECK(tracker.notifications(QStringLiteral("org.example.closer"), true).isEmpty());
    CHECK(tracker.notifications(QStringLiteral("org.example.closer")).size() == 1);
    CHECK(roleValue(&tracker, 0, "broadcastReason").toUInt() == 2);
    CHECK(roleValue(&tracker, 0, "closeReason").toString() == QStringLiteral("broadcast-dismissed"));
}

TEST_CASE("Notification watcher receives real D-Bus transport and lifecycle signals", "[notification][dbus][transport]")
{
    krema::test::NotificationServerPeer server;
    REQUIRE(server.start());

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(waitFor([&] {
        return server.registerWatcherCalls() >= 1;
    }));

    REQUIRE(server.sendNotify(QStringLiteral("Fixture Mail"),
                              900,
                              0,
                              QStringLiteral("org.example.fixture"),
                              QStringLiteral("Initial"),
                              QStringLiteral("Initial body"),
                              {QStringLiteral("open"), QStringLiteral("Open")},
                              5000,
                              1));
    REQUIRE(waitFor([&] {
        return tracker.rowCount() == 1 && tracker.unreadCount(QStringLiteral("org.example.fixture")) == 1;
    }));
    CHECK(roleValue(&tracker, 0, "summary").toString() == QStringLiteral("Initial"));

    REQUIRE(server.sendNotify(QStringLiteral("Fixture Mail"),
                              901,
                              900,
                              QStringLiteral("org.example.fixture"),
                              QStringLiteral("Replacement"),
                              QStringLiteral("Replacement body")));
    REQUIRE(waitFor([&] {
        return tracker.rowCount() == 2 && tracker.unreadCount(QStringLiteral("org.example.fixture")) == 1;
    }));
    CHECK(roleValue(&tracker, 0, "closeReason").toString() == QStringLiteral("replaced"));
    CHECK(roleValue(&tracker, 1, "id").toUInt() == 901);

    REQUIRE(server.sendClose(901));
    REQUIRE(waitFor([&] {
        return tracker.notifications(QStringLiteral("org.example.fixture"), true).isEmpty();
    }));
    CHECK(roleValue(&tracker, 1, "closeReason").toString() == QStringLiteral("directed"));

    REQUIRE(server.sendNotify(QStringLiteral("Fixture Mail"), 902, 0, QStringLiteral("org.example.fixture")));
    REQUIRE(waitFor([&] {
        return tracker.unreadCount(QStringLiteral("org.example.fixture")) == 1;
    }));
    REQUIRE(server.broadcastClose(902, 1));
    REQUIRE(waitFor([&] {
        return tracker.notifications(QStringLiteral("org.example.fixture"), true).isEmpty();
    }));
    CHECK(roleValue(&tracker, 2, "broadcastReason").toUInt() == 1);
    CHECK(roleValue(&tracker, 2, "closeReason").toString() == QStringLiteral("broadcast-expired"));
}

TEST_CASE("Notification watcher re-registers after notification service owner loss", "[notification][dbus][lifecycle]")
{
    krema::test::NotificationServerPeer server;
    REQUIRE(server.start());

    ScopedTracker scoped;
    REQUIRE(waitFor([&] {
        return server.registerWatcherCalls() >= 1;
    }));

    server.stop();
    REQUIRE(waitFor([&] {
        return !server.isRegistered();
    }));
    REQUIRE(server.start());
    REQUIRE(waitFor([&] {
        return server.registerWatcherCalls() >= 2;
    }));
}

TEST_CASE("Notification watcher re-registers once after gapless service owner replacement", "[notification][dbus][lifecycle][owner-handover]")
{
    krema::test::NotificationServerPeer original;
    REQUIRE(original.start());
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(waitFor([&] {
        return original.registerWatcherCalls() == 1;
    }));
    REQUIRE(original.sendNotify(QStringLiteral("Original"), 950, 0, QStringLiteral("org.example.handover")));
    REQUIRE(waitFor([&] {
        return tracker.unreadCount(QStringLiteral("org.example.handover")) == 1;
    }));

    auto bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("org.freedesktop.Notifications");
    const QString oldOwner = bus.interface()->serviceOwner(service).value();
    REQUIRE_FALSE(oldOwner.isEmpty());
    QDBusServiceWatcher owners(service, bus, QDBusServiceWatcher::WatchForOwnerChange);
    QSignalSpy ownerChanges(&owners, &QDBusServiceWatcher::serviceOwnerChanged);
    const QString connectionName = QStringLiteral("notification-owner-replacement");
    const auto replacementBus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName);
    const auto disconnect = qScopeGuard([&] {
        QDBusConnection::disconnectFromBus(connectionName);
    });
    REQUIRE(replacementBus.isConnected());
    krema::test::NotificationServerPeer replacement(replacementBus);
    REQUIRE(replacement.start(true));
    const QString newOwner = bus.interface()->serviceOwner(service).value();
    REQUIRE_FALSE(newOwner.isEmpty());
    REQUIRE(newOwner != oldOwner);
    REQUIRE(waitFor([&] {
        return ownerChanges.count() == 1;
    }));
    CHECK(ownerChanges.at(0).at(1).toString() == oldOwner);
    CHECK(ownerChanges.at(0).at(2).toString() == newOwner);
    REQUIRE(waitFor([&] {
        return replacement.registerWatcherCalls() >= 1;
    }));
    REQUIRE(replacement.sendNotify(QStringLiteral("Replacement"), 951, 0, QStringLiteral("org.example.handover")));
    REQUIRE(waitFor([&] {
        return tracker.unreadCount(QStringLiteral("org.example.handover")) == 2;
    }));
    CHECK(replacement.registerWatcherCalls() == 1);
    CHECK(original.registerWatcherCalls() == 1);
    REQUIRE(tracker.notifications(QStringLiteral("org.example.handover"), true).size() == 2);
    CHECK(roleValue(&tracker, 1, "id").toUInt() == 951);
}

TEST_CASE("SNI watcher replaces its snapshot after gapless service owner replacement", "[notification][sni][dbus][owner-handover]")
{
    krema::test::StatusNotifierItemPeer oldItem(QStringLiteral("org.example.fixture.OldSni"), QStringLiteral("/OldItem"));
    oldItem.setId(QStringLiteral("org.example.old-sni"));
    oldItem.setStatus(QStringLiteral("NeedsAttention"));
    oldItem.setTitle(QStringLiteral("Old title"));
    REQUIRE(oldItem.start());
    krema::test::StatusNotifierItemPeer newItem(QStringLiteral("org.example.fixture.NewSni"), QStringLiteral("/NewItem"));
    newItem.setId(QStringLiteral("org.example.new-sni"));
    newItem.setStatus(QStringLiteral("NeedsAttention"));
    newItem.setTitle(QStringLiteral("New title"));
    REQUIRE(newItem.start());
    krema::test::StatusNotifierWatcherPeer original;
    original.setRegisteredItems({oldItem.itemAddress()});
    REQUIRE(original.start());
    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(waitFor([&] {
        return tracker.sniNeedsAttention(QStringLiteral("org.example.old-sni"));
    }));
    REQUIRE_FALSE(tracker.sniNeedsAttention(QStringLiteral("org.example.new-sni")));

    auto bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("org.kde.StatusNotifierWatcher");
    const QString oldOwner = bus.interface()->serviceOwner(service).value();
    REQUIRE_FALSE(oldOwner.isEmpty());
    QDBusServiceWatcher owners(service, bus, QDBusServiceWatcher::WatchForOwnerChange);
    QSignalSpy ownerChanges(&owners, &QDBusServiceWatcher::serviceOwnerChanged);
    const QString connectionName = QStringLiteral("sni-watcher-owner-replacement");
    const auto replacementBus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName);
    const auto disconnect = qScopeGuard([&] {
        QDBusConnection::disconnectFromBus(connectionName);
    });
    REQUIRE(replacementBus.isConnected());
    krema::test::StatusNotifierWatcherPeer replacement(replacementBus);
    replacement.setRegisteredItems({newItem.itemAddress()});
    REQUIRE(replacement.start(true));
    const QString newOwner = bus.interface()->serviceOwner(service).value();
    REQUIRE_FALSE(newOwner.isEmpty());
    REQUIRE(newOwner != oldOwner);
    REQUIRE(waitFor([&] {
        return ownerChanges.count() == 1;
    }));
    CHECK(ownerChanges.at(0).at(1).toString() == oldOwner);
    CHECK(ownerChanges.at(0).at(2).toString() == newOwner);
    REQUIRE(waitFor([&] {
        return !tracker.sniNeedsAttention(QStringLiteral("org.example.old-sni")) && tracker.sniNeedsAttention(QStringLiteral("org.example.new-sni"));
    }));
    CHECK(tracker.sniTitle(QStringLiteral("org.example.old-sni")).isEmpty());
    CHECK(tracker.sniTitle(QStringLiteral("org.example.new-sni")) == QStringLiteral("New title"));
}

TEST_CASE("SNI registration, status transitions, re-registration and owner loss are observable", "[notification][sni][dbus]")
{
    krema::test::StatusNotifierItemPeer item(QStringLiteral("org.example.fixture.Sni"));
    // The tracker issues Id and Status property calls independently; publish
    // the status first so either reply order must still converge.
    item.setStatus(QStringLiteral("NeedsAttention"));
    item.setId(QStringLiteral("org.example.sni"));
    REQUIRE(item.start());

    krema::test::StatusNotifierWatcherPeer watcher;
    watcher.setRegisteredItems({item.itemAddress()});
    REQUIRE(watcher.start());

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(waitFor([&] {
        return tracker.sniNeedsAttention(QStringLiteral("org.example.sni"));
    }));

    notify(tracker, 500, QStringLiteral("SNI App"), QStringLiteral("org.example.sni.desktop"));
    notify(tracker, 501, QStringLiteral("Other App"), QStringLiteral("org.example.other.desktop"));
    REQUIRE(tracker.unreadCount(QStringLiteral("org.example.sni")) == 1);
    REQUIRE(tracker.unreadCount(QStringLiteral("org.example.other")) == 1);

    const int revisionBeforeAttention = tracker.revision();
    item.setStatus(QStringLiteral("Active"));
    REQUIRE(waitFor([&] {
        return !tracker.sniNeedsAttention(QStringLiteral("org.example.sni")) && tracker.unreadCount(QStringLiteral("org.example.sni")) == 0
            && tracker.revision() != revisionBeforeAttention;
    }));
    CHECK(tracker.unreadCount(QStringLiteral("org.example.other")) == 1);

    item.setStatus(QStringLiteral("NeedsAttention"));
    REQUIRE(waitFor([&] {
        return tracker.sniNeedsAttention(QStringLiteral("org.example.sni"));
    }));
    // Re-entering attention is a boolean state transition; it does not
    // synthesize a numeric badge after the watcher unread state was cleared.
    CHECK(tracker.unreadCount(QStringLiteral("org.example.sni")) == 0);
    CHECK(tracker.unreadCount(QStringLiteral("org.example.other")) == 1);

    item.setStatus(QStringLiteral("Passive"));
    REQUIRE(waitFor([&] {
        return !tracker.sniNeedsAttention(QStringLiteral("org.example.sni"));
    }));

    item.setStatus(QStringLiteral("NeedsAttention"));
    REQUIRE(waitFor([&] {
        return tracker.sniNeedsAttention(QStringLiteral("org.example.sni"));
    }));

    item.stop();
    REQUIRE(waitFor([&] {
        return !tracker.sniNeedsAttention(QStringLiteral("org.example.sni"));
    }));

    item.setStatus(QStringLiteral("NeedsAttention"));
    REQUIRE(item.start());
    watcher.removeItem(item.itemAddress());
    watcher.addItem(item.itemAddress());
    REQUIRE(waitFor([&] {
        return tracker.sniNeedsAttention(QStringLiteral("org.example.sni"));
    }));

    watcher.stop();
    REQUIRE(waitFor([&] {
        return !tracker.sniNeedsAttention(QStringLiteral("org.example.sni"));
    }));
    watcher.setRegisteredItems({item.itemAddress()});
    REQUIRE(watcher.start());
    REQUIRE(waitFor([&] {
        return tracker.sniNeedsAttention(QStringLiteral("org.example.sni"));
    }));
}

TEST_CASE("SNI metadata is queried and refreshed through live signals", "[notification][sni][metadata][dbus]")
{
    krema::test::StatusNotifierItemPeer item(QStringLiteral("org.example.fixture.Metadata"));
    item.setId(QStringLiteral("org.example.metadata"));
    item.setStatus(QStringLiteral("NeedsAttention"));
    item.setTitle(QStringLiteral("Initial title"));
    item.setAttentionIconName(QStringLiteral("dialog-warning"));
    item.setOverlayIconName(QStringLiteral("mail-unread"));
    item.setToolTip(QVariant::fromValue(krema::test::ToolTipData{QStringLiteral("tray-icon"), {}, QStringLiteral("Tooltip title"), QStringLiteral("Tooltip subtitle")}));
    REQUIRE(item.start());

    krema::test::StatusNotifierWatcherPeer watcher;
    watcher.setRegisteredItems({item.itemAddress()});
    REQUIRE(watcher.start());

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(waitFor([&] {
        return tracker.sniTitle(QStringLiteral("org.example.metadata")) == QStringLiteral("Initial title");
    }));
    CHECK(tracker.sniAttentionIconName(QStringLiteral("org.example.metadata")) == QStringLiteral("dialog-warning"));
    CHECK(tracker.sniOverlayIconName(QStringLiteral("org.example.metadata")) == QStringLiteral("mail-unread"));
    CHECK(tracker.sniToolTipTitle(QStringLiteral("org.example.metadata")) == QStringLiteral("Tooltip title"));
    CHECK(tracker.sniToolTipSubTitle(QStringLiteral("org.example.metadata")) == QStringLiteral("Tooltip subtitle"));

    item.setTitle(QStringLiteral("Updated title"));
    item.setAttentionIconName(QStringLiteral("bell"));
    item.setOverlayIconName(QStringLiteral("mail-read"));
    item.setToolTip(QVariant::fromValue(krema::test::ToolTipData{QStringLiteral("tray-icon"), {}, QStringLiteral("Updated tooltip"), QStringLiteral("Updated subtitle")}));
    REQUIRE(waitFor([&] {
        return tracker.sniTitle(QStringLiteral("org.example.metadata")) == QStringLiteral("Updated title")
            && tracker.sniAttentionIconName(QStringLiteral("org.example.metadata")) == QStringLiteral("bell")
            && tracker.sniOverlayIconName(QStringLiteral("org.example.metadata")) == QStringLiteral("mail-read")
            && tracker.sniToolTipTitle(QStringLiteral("org.example.metadata")) == QStringLiteral("Updated tooltip")
            && tracker.sniToolTipSubTitle(QStringLiteral("org.example.metadata")) == QStringLiteral("Updated subtitle");
    }));

    watcher.removeItem(item.itemAddress());
    REQUIRE(waitFor([&] {
        return tracker.sniTitle(QStringLiteral("org.example.metadata")).isEmpty()
            && tracker.sniAttentionIconName(QStringLiteral("org.example.metadata")).isEmpty()
            && tracker.sniToolTipSubTitle(QStringLiteral("org.example.metadata")).isEmpty();
    }));
}

TEST_CASE("SNI metadata selection prefers attention deterministically", "[notification][sni][metadata][precedence]")
{
    krema::test::StatusNotifierItemPeer active(QStringLiteral("org.example.fixture.Active"), QStringLiteral("/Active"));
    active.setId(QStringLiteral("org.example.same-app"));
    active.setStatus(QStringLiteral("Active"));
    active.setTitle(QStringLiteral("Active title"));
    REQUIRE(active.start());

    krema::test::StatusNotifierItemPeer attention(QStringLiteral("org.example.fixture.Attention"), QStringLiteral("/Attention"));
    attention.setId(QStringLiteral("org.example.same-app"));
    attention.setStatus(QStringLiteral("NeedsAttention"));
    attention.setTitle(QStringLiteral("Attention title"));
    REQUIRE(attention.start());

    krema::test::StatusNotifierWatcherPeer watcher;
    watcher.setRegisteredItems({active.itemAddress(), attention.itemAddress()});
    REQUIRE(watcher.start());

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(waitFor([&] {
        return tracker.sniNeedsAttention(QStringLiteral("org.example.same-app"))
            && tracker.sniTitle(QStringLiteral("org.example.same-app")) == QStringLiteral("Attention title");
    }));
}

TEST_CASE("SNI item metadata refreshes after gapless service owner replacement", "[notification][sni][metadata][owner-handover]")
{
    const QString serviceName = QStringLiteral("org.example.fixture.GaplessSni");
    const QString objectPath = QStringLiteral("/StatusNotifierItem");
    krema::test::StatusNotifierItemPeer original(serviceName, objectPath);
    original.setId(QStringLiteral("org.example.gapless"));
    original.setStatus(QStringLiteral("NeedsAttention"));
    original.setTitle(QStringLiteral("Old title"));
    original.setAttentionIconName(QStringLiteral("old-attention"));
    REQUIRE(original.start());

    krema::test::StatusNotifierWatcherPeer watcher;
    watcher.setRegisteredItems({original.itemAddress()});
    REQUIRE(watcher.start());

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(waitFor([&] {
        return tracker.sniTitle(QStringLiteral("org.example.gapless")) == QStringLiteral("Old title")
            && tracker.sniAttentionIconName(QStringLiteral("org.example.gapless")) == QStringLiteral("old-attention");
    }));

    const QString connectionName = QStringLiteral("sni-item-owner-replacement");
    const auto replacementBus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName);
    const auto disconnect = qScopeGuard([&] {
        QDBusConnection::disconnectFromBus(connectionName);
    });
    REQUIRE(replacementBus.isConnected());

    krema::test::StatusNotifierItemPeer replacement(serviceName, objectPath, replacementBus);
    replacement.setId(QStringLiteral("org.example.gapless"));
    replacement.setStatus(QStringLiteral("NeedsAttention"));
    replacement.setTitle(QStringLiteral("New title"));
    replacement.setAttentionIconName(QStringLiteral("new-attention"));
    replacement.setOverlayIconName(QStringLiteral("new-overlay"));
    REQUIRE(replacement.start(true));

    REQUIRE(waitFor([&] {
        return tracker.sniTitle(QStringLiteral("org.example.gapless")) == QStringLiteral("New title")
            && tracker.sniAttentionIconName(QStringLiteral("org.example.gapless")) == QStringLiteral("new-attention")
            && tracker.sniOverlayIconName(QStringLiteral("org.example.gapless")) == QStringLiteral("new-overlay");
    }));
}

TEST_CASE("DND application inhibition is active without an Until deadline and clears", "[notification][dnd][settings]")
{
    auto &notificationServer = NotificationManager::Server::self();
    notificationServer.clearInhibitions();
    REQUIRE(notificationServer.init());

    NotificationManager::Settings settings;
    settings.load();
    REQUIRE_FALSE(settings.notificationsInhibitedByApplication());

    auto config = KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc"));
    KConfigGroup dnd = config->group(QStringLiteral("DoNotDisturb"));
    const bool hadUntil = dnd.hasKey(QStringLiteral("Until"));
    const QVariant previousUntil = dnd.readEntry(QStringLiteral("Until"), QVariant());
    uint cookie = 0;
    QDBusInterface notifications(QStringLiteral("org.freedesktop.Notifications"),
                                 QStringLiteral("/org/freedesktop/Notifications"),
                                 QStringLiteral("org.freedesktop.Notifications"),
                                 QDBusConnection::sessionBus());
    const auto restore = qScopeGuard([&] {
        if (cookie != 0) {
            notifications.call(QStringLiteral("UnInhibit"), cookie);
        }
        if (hadUntil) {
            dnd.writeEntry(QStringLiteral("Until"), previousUntil, KConfig::Notify);
        } else {
            dnd.deleteEntry(QStringLiteral("Until"), KConfig::Notify);
        }
        config->sync();
        notificationServer.clearInhibitions();
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/org/freedesktop/Notifications"));
        bus.unregisterService(QStringLiteral("org.freedesktop.Notifications"));
    });

    // Remove the expiry source so this gate exercises the application-owned
    // inhibition path, not the separate Until path.
    dnd.deleteEntry(QStringLiteral("Until"), KConfig::Notify);
    config->sync();
    settings.load();
    REQUIRE_FALSE(settings.notificationsInhibitedUntil().isValid());

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(waitFor([&] {
        return !tracker.dndActive();
    }));

    const QDBusReply<uint> inhibition = notifications.call(QStringLiteral("Inhibit"),
                                                           QStringLiteral("org.krema.test"),
                                                           QStringLiteral("deterministic DND gate"),
                                                           QVariantMap());
    REQUIRE(inhibition.isValid());
    cookie = inhibition.value();
    REQUIRE(cookie != 0);
    CHECK(settings.notificationsInhibitedByApplication());
    CHECK_FALSE(settings.notificationsInhibitedUntil().isValid());
    REQUIRE(waitFor([&] {
        return tracker.dndActive();
    }));

    const QDBusMessage release = notifications.call(QStringLiteral("UnInhibit"), cookie);
    REQUIRE(release.type() == QDBusMessage::ReplyMessage);
    cookie = 0;
    CHECK_FALSE(settings.notificationsInhibitedByApplication());
    REQUIRE(waitFor([&] {
        return !tracker.dndActive();
    }));
}

TEST_CASE("DND Until is active before its boundary and clears after expiry", "[notification][dnd][settings]")
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc"));
    KConfigGroup dnd = config->group(QStringLiteral("DoNotDisturb"));
    const bool hadUntil = dnd.hasKey(QStringLiteral("Until"));
    const QVariant previousUntil = dnd.readEntry(QStringLiteral("Until"), QVariant());
    const auto restore = qScopeGuard([&] {
        if (hadUntil) {
            dnd.writeEntry(QStringLiteral("Until"), previousUntil, KConfig::Notify);
        } else {
            dnd.deleteEntry(QStringLiteral("Until"), KConfig::Notify);
        }
        config->sync();
    });

    dnd.writeEntry(QStringLiteral("Until"), QDateTime::currentDateTime().addMSecs(350), KConfig::Notify);
    config->sync();

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(waitFor([&] {
        return tracker.dndActive();
    }));
    REQUIRE(waitFor([&] {
        return !tracker.dndActive();
    }));
}

TEST_CASE("DND thirty-day deadline arms safely and rechecks the real deadline", "[notification][dnd][settings][long-dnd]")
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc"));
    KConfigGroup dnd = config->group(QStringLiteral("DoNotDisturb"));
    const bool hadUntil = dnd.hasKey(QStringLiteral("Until"));
    const QVariant previousUntil = dnd.readEntry(QStringLiteral("Until"), QVariant());
    const auto restore = qScopeGuard([&] {
        if (hadUntil) {
            dnd.writeEntry(QStringLiteral("Until"), previousUntil, KConfig::Notify);
        } else {
            dnd.deleteEntry(QStringLiteral("Until"), KConfig::Notify);
        }
        config->sync();
    });
    const QDateTime until = QDateTime::currentDateTime().addDays(30);
    dnd.writeEntry(QStringLiteral("Until"), until, KConfig::Notify);
    config->sync();

    ScopedTracker scoped;
    auto &tracker = scoped.tracker;
    REQUIRE(tracker.dndActive());
    const auto timers = tracker.findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
    REQUIRE(timers.size() == 1);
    auto *timer = timers.constFirst();
    REQUIRE(timer->isActive());
    REQUIRE(timer->interval() == std::numeric_limits<int>::max());
    auto *dispatcher = QAbstractEventDispatcher::instance();
    REQUIRE(dispatcher != nullptr);
    REQUIRE(dispatcher->remainingTime(timer->id()) > QAbstractEventDispatcher::Duration::zero());
    QSignalSpy changes(&tracker, &krema::NotificationTracker::dndActiveChanged);

    SECTION("an early timeout keeps DND active and rearms without advancing the clock")
    {
        REQUIRE(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        CHECK(QDateTime::currentDateTime() < until);
        CHECK(tracker.dndActive());
        CHECK(changes.count() == 0);
        CHECK(timer->isActive());
        CHECK(timer->interval() == std::numeric_limits<int>::max());
        CHECK(dispatcher->remainingTime(timer->id()) > QAbstractEventDispatcher::Duration::zero());
    }
    SECTION("a rewritten short deadline expires on the real clock")
    {
        const auto shortUntil = QDateTime::currentDateTime().addMSecs(350);
        dnd.writeEntry(QStringLiteral("Until"), shortUntil, KConfig::Notify);
        config->sync();
        REQUIRE(waitFor([&] {
            return timer->isActive() && timer->interval() <= 350;
        }));
        REQUIRE(tracker.dndActive());
        REQUIRE(waitFor([&] {
            return !tracker.dndActive();
        }));
        CHECK(QDateTime::currentDateTime() >= shortUntil);
        CHECK_FALSE(timer->isActive());
        CHECK(changes.count() == 1);
    }
    SECTION("cancelling the deadline stops its timer")
    {
        dnd.deleteEntry(QStringLiteral("Until"), KConfig::Notify);
        config->sync();
        REQUIRE(waitFor([&] {
            return !tracker.dndActive();
        }));
        CHECK_FALSE(timer->isActive());
        CHECK(changes.count() == 1);
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}

#include "test_notification_tracker.moc"
