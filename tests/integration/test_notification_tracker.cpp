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

#include <QCoreApplication>
#include <QDBusConnection>
#include <QSignalSpy>
#include <QTest>
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

bool waitFor(const std::function<bool()> &condition)
{
    return QTest::qWaitFor(condition, kTimeoutMs);
}

void notify(krema::NotificationTracker &tracker,
            uint id,
            const QString &appName,
            const QString &desktopEntry,
            uint replacesId = 0,
            const QStringList &actions = {})
{
    QVariantMap hints;
    if (!desktopEntry.isEmpty()) {
        hints.insert(QStringLiteral("desktop-entry"), desktopEntry);
    }
    tracker.Notify(id,
                   appName,
                   replacesId,
                   QStringLiteral("test-icon"),
                   QStringLiteral("Test summary"),
                   QStringLiteral("Test body"),
                   actions,
                   hints,
                   -1);
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

    auto restore = [&] {
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
    };

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

    restore();
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

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
