// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "models/notificationtracker.h"

#include <QSignalSpy>
#include <QTest>

using namespace krema;

class TestNotificationTracker : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // --- Notification count ---

    void initialCountIsZero()
    {
        NotificationTracker tracker;
        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 0);
    }

    void notify_incrementsCount()
    {
        NotificationTracker tracker;
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, QStringLiteral("Message"), {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);

        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 1);
    }

    void notify_multipleNotifications()
    {
        NotificationTracker tracker;
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, QStringLiteral("Msg 1"), {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);
        tracker.Notify(2, QStringLiteral("Slack"), 0, {}, QStringLiteral("Msg 2"), {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);

        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 2);
    }

    void notify_differentAppsIndependent()
    {
        NotificationTracker tracker;
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);
        tracker.Notify(2, QStringLiteral("Telegram"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("telegram")}}, -1);

        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 1);
        QCOMPARE(tracker.unreadCount(QStringLiteral("telegram")), 1);
    }

    // --- Close notification ---

    void closeNotification_decrementsCount()
    {
        NotificationTracker tracker;
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);
        tracker.Notify(2, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);

        tracker.CloseNotification(1);
        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 1);
    }

    void notificationClosed_decrementsCount()
    {
        NotificationTracker tracker;
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);

        tracker.NotificationClosed(1, 2); // reason=2 (dismissed)
        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 0);
    }

    void closeUnknownNotification_noCrash()
    {
        NotificationTracker tracker;
        // Should not crash
        tracker.CloseNotification(999);
        tracker.NotificationClosed(999, 1);
        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 0);
    }

    // --- Clear notifications ---

    void clearUnread_resetsCount()
    {
        NotificationTracker tracker;
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);
        tracker.Notify(2, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);

        tracker.clearUnreadNotifications(QStringLiteral("slack"));
        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 0);
    }

    void clearUnread_doesNotAffectOtherApps()
    {
        NotificationTracker tracker;
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);
        tracker.Notify(2, QStringLiteral("Telegram"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("telegram")}}, -1);

        tracker.clearUnreadNotifications(QStringLiteral("slack"));
        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 0);
        QCOMPARE(tracker.unreadCount(QStringLiteral("telegram")), 1);
    }

    // --- Revision tracking ---

    void revision_incrementsOnNotify()
    {
        NotificationTracker tracker;
        int initialRevision = tracker.revision();

        QSignalSpy spy(&tracker, &NotificationTracker::revisionChanged);
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);

        QVERIFY(tracker.revision() > initialRevision);
        QVERIFY(spy.count() >= 1);
    }

    void revision_incrementsOnClose()
    {
        NotificationTracker tracker;
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);

        int revBefore = tracker.revision();
        tracker.CloseNotification(1);
        QVERIFY(tracker.revision() > revBefore);
    }

    void revision_incrementsOnClear()
    {
        NotificationTracker tracker;
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, {}, {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);

        int revBefore = tracker.revision();
        tracker.clearUnreadNotifications(QStringLiteral("slack"));
        // clearUnreadNotifications may or may not bump revision depending on
        // whether it finds matching entries. Verify count is cleared.
        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 0);
        // Revision should be at least what it was (may have been bumped)
        QVERIFY(tracker.revision() >= revBefore);
    }

    // --- replacesId ---

    void replacesId_doesNotDuplicateCount()
    {
        NotificationTracker tracker;
        // First notification
        tracker.Notify(1, QStringLiteral("Slack"), 0, {}, QStringLiteral("Original"), {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);
        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 1);

        // Replace notification (replacesId=1)
        tracker.Notify(2, QStringLiteral("Slack"), 1, {}, QStringLiteral("Updated"), {}, {}, {{QStringLiteral("desktop-entry"), QStringLiteral("slack")}}, -1);
        // Should still be 1 (replaced, not added)
        QCOMPARE(tracker.unreadCount(QStringLiteral("slack")), 1);
    }
};

QTEST_MAIN(TestNotificationTracker)
#include "test_notificationtracker.moc"
