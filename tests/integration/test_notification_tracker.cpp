// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Stage 1 notification watcher state: canonical identity, replacement and
// close lifecycle, notification settings, and reactive clearing.
//
// The public watcher slots are invoked directly. This uses the same observable
// surface as the session-bus callbacks without depending on a particular
// notification server implementation or its transient notification state.

#include "models/notificationtracker.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <KConfigGroup>
#include <KSharedConfig>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>
#include <QVariantMap>
#include <functional>

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

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}

#include "test_notification_tracker.moc"
