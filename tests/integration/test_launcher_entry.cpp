// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Unity LauncherEntry badges, progress and urgency on dock items.
//
// Apps send com.canonical.Unity.LauncherEntry.Update signals on the session
// bus. Krema must show them on its own, without Plasma's private task manager
// QML module (Plasma 6.6 moved it into the task manager applet plugin).
//
// Must run under run-with-kwin.sh, which provides WAYLAND_DISPLAY, a private
// session bus and throwaway XDG directories.

#include "krema.h"
#include "models/dockmodel.h"
#include "models/launcherentrytracker.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/multidockmanager.h"
#include "shell/outputordermonitor.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <KAboutData>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDateTime>
#include <QQuickItem>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTest>
#include <QtQml>

#include <cmath>
#include <functional>
#include <limits>
#include <memory>

// Static library resources must be initialized from the global namespace.
static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace
{

using krema::LauncherEntryTracker;

constexpr int kTimeoutMs = 10000;

// Pinned by default (KremaSettings PinnedLaunchers) and installed in the test
// environment, as in a default Krema setup.
const QUrl kKonsole(QStringLiteral("applications:org.kde.konsole.desktop"));
const QUrl kDolphin(QStringLiteral("applications:org.kde.dolphin.desktop"));
const QString kKonsoleUri = QStringLiteral("application://org.kde.konsole.desktop");
const QString kDolphinUri = QStringLiteral("application://org.kde.dolphin.desktop");

// Mirrors the object graph and wiring Application::run() builds.
struct App {
    std::unique_ptr<KremaSettings> settings;
    std::unique_ptr<krema::DockModel> model;
    std::unique_ptr<krema::NotificationTracker> tracker;
    std::unique_ptr<LauncherEntryTracker> launcherEntries;
    std::unique_ptr<krema::MultiDockManager> manager;
};

App &app()
{
    static App *instance = [] {
        auto *a = new App;
        a->settings = std::make_unique<KremaSettings>();
        a->settings->load();
        a->settings->setMonitorMode(krema::MultiDockManager::PrimaryOnly);
        a->model = std::make_unique<krema::DockModel>();
        a->model->setPinnedLaunchers(a->settings->pinnedLaunchers());
        a->tracker = std::make_unique<krema::NotificationTracker>();
        a->launcherEntries = std::make_unique<LauncherEntryTracker>();

        auto *model = a->model.get();
        qmlRegisterSingletonType<krema::DockModel>("com.bhyoo.krema", 1, 0, "DockModel", [model](QQmlEngine *, QJSEngine *) -> QObject * {
            QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
            return model;
        });
        auto *settings = a->settings.get();
        qmlRegisterSingletonType<KremaSettings>("com.bhyoo.krema", 1, 0, "DockSettings", [settings](QQmlEngine *, QJSEngine *) -> QObject * {
            QQmlEngine::setObjectOwnership(settings, QQmlEngine::CppOwnership);
            return settings;
        });
        auto *tracker = a->tracker.get();
        qmlRegisterSingletonType<krema::NotificationTracker>("com.bhyoo.krema", 1, 0, "NotificationTracker", [tracker](QQmlEngine *, QJSEngine *) -> QObject * {
            QQmlEngine::setObjectOwnership(tracker, QQmlEngine::CppOwnership);
            return tracker;
        });
        auto *launcherEntries = a->launcherEntries.get();
        qmlRegisterSingletonType<LauncherEntryTracker>("com.bhyoo.krema",
                                                       1,
                                                       0,
                                                       "LauncherEntryTracker",
                                                       [launcherEntries](QQmlEngine *, QJSEngine *) -> QObject * {
                                                           QQmlEngine::setObjectOwnership(launcherEntries, QQmlEngine::CppOwnership);
                                                           return launcherEntries;
                                                       });
        return a;
    }();
    return *instance;
}

// Creates the dock shells the way Application::run() does, if not running.
void startDocks()
{
    auto &a = app();
    if (a.manager) {
        return;
    }
    a.manager = std::make_unique<krema::MultiDockManager>(a.settings.get(), a.model.get(), a.tracker.get());
    auto *outputOrder = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [outputOrder] {
            return outputOrder->orderReady();
        },
        kTimeoutMs));
    a.manager->initialize();
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
}

// A separate bus connection plays the app: it has its own unique name, so
// disconnecting it looks like the app exiting.
class FakeApp
{
public:
    explicit FakeApp(const QString &name)
        : m_name(name)
        , m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, name))
    {
        REQUIRE(m_bus.isConnected());
    }
    ~FakeApp()
    {
        quit();
    }
    FakeApp(const FakeApp &) = delete;
    FakeApp &operator=(const FakeApp &) = delete;

    void update(const QString &appUri, const QVariantMap &properties)
    {
        auto signal = QDBusMessage::createSignal(QStringLiteral("/com/canonical/unity/launcherentry/1"),
                                                 QStringLiteral("com.canonical.Unity.LauncherEntry"),
                                                 QStringLiteral("Update"));
        signal << appUri << properties;
        REQUIRE(m_bus.send(signal));
    }

    void quit()
    {
        if (m_bus.isConnected()) {
            QDBusConnection::disconnectFromBus(m_name);
            m_bus = QDBusConnection(QString());
        }
    }

private:
    QString m_name;
    QDBusConnection m_bus;
};

bool waitFor(const std::function<bool()> &condition)
{
    return QTest::qWaitFor(condition, kTimeoutMs);
}

// Lets queued D-Bus signals reach the tracker when a condition must stay false.
void settle()
{
    QTest::qWait(300);
}

QQuickItem *findItem(QQuickItem *item, const std::function<bool(QQuickItem *)> &match)
{
    if (!item) {
        return nullptr;
    }
    if (match(item)) {
        return item;
    }
    const auto children = item->childItems();
    for (auto *child : children) {
        if (auto *found = findItem(child, match)) {
            return found;
        }
    }
    return nullptr;
}

// The primary dock's delegate for the launcher @p url.
QQuickItem *dockItemFor(const QUrl &url)
{
    startDocks();
    auto *view = app().manager->primaryShell()->view();
    QQuickItem *found = nullptr;
    waitFor([&] {
        found = findItem(view->rootObject(), [&](QQuickItem *item) {
            const QVariant index = item->property("index");
            return item->metaObject()->indexOfProperty("_badgeCount") >= 0 && index.isValid() && app().model->launcherUrl(index.toInt()) == url;
        });
        return found != nullptr;
    });
    return found;
}

QQuickItem *child(QQuickItem *dockItem, const QString &objectName)
{
    return findItem(dockItem, [&](QQuickItem *item) {
        return item->objectName() == objectName;
    });
}

// Resets every entry the tests may have left behind.
void resetEntries()
{
    FakeApp cleaner(QStringLiteral("launcher-entry-reset"));
    const QVariantMap cleared{
        {QStringLiteral("count"), 0},
        {QStringLiteral("count-visible"), false},
        {QStringLiteral("progress"), 0.0},
        {QStringLiteral("progress-visible"), false},
        {QStringLiteral("urgent"), false},
    };
    cleaner.update(kKonsoleUri, cleared);
    cleaner.update(kDolphinUri, cleared);
    auto *tracker = app().launcherEntries.get();
    REQUIRE(waitFor([tracker] {
        return !tracker->countVisible(kKonsole) && !tracker->progressVisible(kKonsole) && !tracker->urgent(kKonsole) && !tracker->countVisible(kDolphin);
    }));
}

} // namespace

TEST_CASE("A LauncherEntry count shows as a badge on the dock item", "[launcher-entry][dock]")
{
    auto *item = dockItemFor(kKonsole);
    REQUIRE(item);
    auto *badge = child(item, QStringLiteral("badge"));
    REQUIRE(badge);
    FakeApp konsole(QStringLiteral("launcher-entry-badge"));

    // QA-01: count + count-visible from the app shows the count on its icon
    konsole.update(kKonsoleUri, {{QStringLiteral("count"), 3}, {QStringLiteral("count-visible"), true}});
    CHECK(waitFor([&] {
        return badge->isVisible() && item->property("_badgeCount").toInt() == 3;
    }));

    // QA-05: count-visible=false hides the badge again
    konsole.update(kKonsoleUri, {{QStringLiteral("count-visible"), false}});
    CHECK(waitFor([&] {
        return !badge->isVisible() && item->property("_badgeCount").toInt() == 0;
    }));
}

TEST_CASE("A LauncherEntry progress shows as a progress bar on the dock item", "[launcher-entry][dock]")
{
    auto *item = dockItemFor(kKonsole);
    REQUIRE(item);
    auto *bar = child(item, QStringLiteral("progressBar"));
    auto *fill = child(item, QStringLiteral("progressFill"));
    REQUIRE(bar);
    REQUIRE(fill);
    FakeApp konsole(QStringLiteral("launcher-entry-progress"));

    // QA-02: the bar appears and its fill follows the reported fraction
    konsole.update(kKonsoleUri, {{QStringLiteral("progress"), 0.5}, {QStringLiteral("progress-visible"), true}});
    CHECK(waitFor([&] {
        return bar->isVisible() && std::abs(fill->width() - bar->width() * 0.5) < 1.0;
    }));

    konsole.update(kKonsoleUri, {{QStringLiteral("progress-visible"), false}});
    CHECK(waitFor([&] {
        return !bar->isVisible();
    }));
}

TEST_CASE("A LauncherEntry urgent flag makes the dock item demand attention", "[launcher-entry][dock]")
{
    auto *item = dockItemFor(kKonsole);
    REQUIRE(item);
    FakeApp konsole(QStringLiteral("launcher-entry-urgent"));

    // QA-03
    konsole.update(kKonsoleUri, {{QStringLiteral("urgent"), true}});
    CHECK(waitFor([&] {
        return item->property("_isDemandingAttention").toBool();
    }));
    konsole.update(kKonsoleUri, {{QStringLiteral("urgent"), false}});
    CHECK(waitFor([&] {
        return !item->property("_isDemandingAttention").toBool();
    }));
}

TEST_CASE("Control: other dock items stay unchanged", "[launcher-entry][dock][control]")
{
    auto *konsoleItem = dockItemFor(kKonsole);
    auto *dolphinItem = dockItemFor(kDolphin);
    REQUIRE(konsoleItem);
    REQUIRE(dolphinItem);
    FakeApp konsole(QStringLiteral("launcher-entry-control"));

    konsole.update(
        kKonsoleUri,
        {{QStringLiteral("count"), 7}, {QStringLiteral("count-visible"), true}, {QStringLiteral("progress"), 0.3}, {QStringLiteral("progress-visible"), true}});
    REQUIRE(waitFor([&] {
        return konsoleItem->property("_badgeCount").toInt() == 7;
    }));
    CHECK(dolphinItem->property("_badgeCount").toInt() == 0);
    CHECK_FALSE(child(dolphinItem, QStringLiteral("badge"))->isVisible());
    CHECK_FALSE(child(dolphinItem, QStringLiteral("progressBar"))->isVisible());
    resetEntries();
}

TEST_CASE("An app that leaves the bus clears its entry", "[launcher-entry][tracker]")
{
    auto *tracker = app().launcherEntries.get();
    FakeApp konsole(QStringLiteral("launcher-entry-exit"));
    konsole.update(kKonsoleUri,
                   {{QStringLiteral("count"), 2},
                    {QStringLiteral("count-visible"), true},
                    {QStringLiteral("progress"), 0.25},
                    {QStringLiteral("progress-visible"), true},
                    {QStringLiteral("urgent"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->countVisible(kKonsole) && tracker->progressVisible(kKonsole) && tracker->urgent(kKonsole);
    }));

    // QA-04: when the sender's connection goes away, everything resets
    const int revision = tracker->revision();
    konsole.quit();
    CHECK(waitFor([tracker] {
        return tracker->count(kKonsole) == 0 && !tracker->countVisible(kKonsole) && tracker->progress(kKonsole) == 0 && !tracker->progressVisible(kKonsole)
            && !tracker->urgent(kKonsole);
    }));
    CHECK(tracker->revision() != revision);
}

TEST_CASE("Partial updates keep the other fields", "[launcher-entry][tracker]")
{
    auto *tracker = app().launcherEntries.get();
    FakeApp konsole(QStringLiteral("launcher-entry-partial"));
    konsole.update(kKonsoleUri, {{QStringLiteral("count"), 4}, {QStringLiteral("count-visible"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->count(kKonsole) == 4;
    }));

    // QA-05: a progress-only update leaves the count alone
    konsole.update(kKonsoleUri, {{QStringLiteral("progress"), 0.8}, {QStringLiteral("progress-visible"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->progress(kKonsole) == 80;
    }));
    CHECK(tracker->count(kKonsole) == 4);
    CHECK(tracker->countVisible(kKonsole));
    resetEntries();
}

TEST_CASE("Progress is converted to a percentage and bounded", "[launcher-entry][tracker]")
{
    auto *tracker = app().launcherEntries.get();
    FakeApp konsole(QStringLiteral("launcher-entry-bounds"));
    int marker = 100;
    const auto progressAfter = [&](double value) {
        // A fresh count marks this update as processed.
        ++marker;
        konsole.update(kKonsoleUri, {{QStringLiteral("progress"), value}, {QStringLiteral("progress-visible"), true}, {QStringLiteral("count"), marker}});
        REQUIRE(waitFor([&] {
            return tracker->count(kKonsole) == marker;
        }));
        return tracker->progress(kKonsole);
    };

    // QA-05: fraction 0..1 becomes 0..100, out-of-range and non-finite values
    // never escape 0..100
    CHECK(progressAfter(0.424) == 42);
    CHECK(progressAfter(1.5) == 100);
    CHECK(progressAfter(-0.2) == 0);
    CHECK(progressAfter(0.6) == 60);
    CHECK(progressAfter(std::numeric_limits<double>::quiet_NaN()) == 0);
    resetEntries();
}

TEST_CASE("Out-of-range counts are ignored", "[launcher-entry][tracker]")
{
    auto *tracker = app().launcherEntries.get();
    FakeApp konsole(QStringLiteral("launcher-entry-count-bounds"));
    konsole.update(kKonsoleUri, {{QStringLiteral("count"), 12}, {QStringLiteral("count-visible"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->count(kKonsole) == 12;
    }));

    // QA-10: negative or >= INT_MAX counts keep the last valid count; the
    // urgent flag in the same update marks it as processed
    konsole.update(kKonsoleUri, {{QStringLiteral("count"), -53}, {QStringLiteral("urgent"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->urgent(kKonsole);
    }));
    CHECK(tracker->count(kKonsole) == 12);
    konsole.update(kKonsoleUri, {{QStringLiteral("count"), qint64(std::numeric_limits<int>::max()) + 1}, {QStringLiteral("urgent"), false}});
    REQUIRE(waitFor([tracker] {
        return !tracker->urgent(kKonsole);
    }));
    CHECK(tracker->count(kKonsole) == 12);
    resetEntries();
}

TEST_CASE("An app restarted on a new connection keeps its entry when the old one leaves", "[launcher-entry][tracker]")
{
    auto *tracker = app().launcherEntries.get();
    auto oldInstance = std::make_unique<FakeApp>(QStringLiteral("launcher-entry-old"));
    oldInstance->update(kKonsoleUri, {{QStringLiteral("count"), 1}, {QStringLiteral("count-visible"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->count(kKonsole) == 1;
    }));
    FakeApp newInstance(QStringLiteral("launcher-entry-new"));
    newInstance.update(kKonsoleUri, {{QStringLiteral("count"), 2}});
    REQUIRE(waitFor([tracker] {
        return tracker->count(kKonsole) == 2;
    }));

    // QA-11: the old connection going away does not clear the new one's entry
    oldInstance.reset();
    settle();
    CHECK(tracker->count(kKonsole) == 2);
    CHECK(tracker->countVisible(kKonsole));

    newInstance.quit();
    CHECK(waitFor([tracker] {
        return !tracker->countVisible(kKonsole);
    }));
}

TEST_CASE("Unity Launcher Mapping rules redirect a launcher to another app's entry", "[launcher-entry][tracker]")
{
    // QA-12: taskmanagerrulesrc maps the installed desktop file to the one the
    // app announces itself as (read when the tracker starts)
    auto rules = KSharedConfig::openConfig(QStringLiteral("taskmanagerrulesrc"));
    rules->group(QStringLiteral("Unity Launcher Mapping")).writeEntry(QStringLiteral("org.kde.konsole.desktop"), QStringLiteral("org.kde.dolphin.desktop"));
    rules->sync();
    LauncherEntryTracker mapped;
    FakeApp dolphin(QStringLiteral("launcher-entry-mapped"));
    dolphin.update(kDolphinUri, {{QStringLiteral("count"), 4}, {QStringLiteral("count-visible"), true}});
    CHECK(waitFor([&] {
        return mapped.count(kKonsole) == 4 && mapped.countVisible(kKonsole);
    }));
    CHECK(mapped.count(kDolphin) == 4);
    rules->deleteGroup(QStringLiteral("Unity Launcher Mapping"));
    rules->sync();
    resetEntries();
}

TEST_CASE("Updates for other or unknown apps do not touch an entry", "[launcher-entry][tracker]")
{
    auto *tracker = app().launcherEntries.get();
    FakeApp sender(QStringLiteral("launcher-entry-unknown"));

    // QA-06
    sender.update(QStringLiteral("application://krema-test-does-not-exist.desktop"), {{QStringLiteral("count"), 9}, {QStringLiteral("count-visible"), true}});
    sender.update(kDolphinUri, {{QStringLiteral("count"), 5}, {QStringLiteral("count-visible"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->count(kDolphin) == 5;
    }));
    CHECK(tracker->count(kKonsole) == 0);
    CHECK_FALSE(tracker->countVisible(kKonsole));
    resetEntries();
}

TEST_CASE("Launcher URLs in desktop file form resolve to the same entry", "[launcher-entry][tracker]")
{
    auto *tracker = app().launcherEntries.get();
    const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, QStringLiteral("org.kde.konsole.desktop"));
    REQUIRE_FALSE(path.isEmpty());
    FakeApp konsole(QStringLiteral("launcher-entry-file-url"));

    // QA-08: applications: and file:// launcher URLs of one app share its entry
    konsole.update(kKonsoleUri, {{QStringLiteral("count"), 6}, {QStringLiteral("count-visible"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->count(kKonsole) == 6;
    }));
    CHECK(tracker->count(QUrl::fromLocalFile(path)) == 6);
    CHECK(tracker->countVisible(QUrl::fromLocalFile(path)));
    resetEntries();
}

TEST_CASE("Badge settings hide the count but keep progress", "[launcher-entry][tracker][settings]")
{
    auto *tracker = app().launcherEntries.get();
    FakeApp konsole(QStringLiteral("launcher-entry-settings"));
    konsole.update(
        kKonsoleUri,
        {{QStringLiteral("count"), 3}, {QStringLiteral("count-visible"), true}, {QStringLiteral("progress"), 0.5}, {QStringLiteral("progress-visible"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->countVisible(kKonsole) && tracker->progressVisible(kKonsole);
    }));

    // QA-07: Plasma's notification settings (plasmanotifyrc) gate counts the
    // way they gate Plasma's own task manager badges
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc"));
    auto writeAndNotify = [&](const QString &group, const QString &key, const QVariant &value) {
        KConfigGroup grp = config->group(group);
        grp.writeEntry(key, value, KConfig::Notify);
        config->sync();
    };

    SECTION("badges disabled")
    {
        writeAndNotify(QStringLiteral("Badges"), QStringLiteral("InTaskManager"), false);
        CHECK(waitFor([tracker] {
            return !tracker->countVisible(kKonsole) && tracker->count(kKonsole) == 0;
        }));
        CHECK(tracker->progressVisible(kKonsole));
        CHECK(tracker->progress(kKonsole) == 50);
        config->group(QStringLiteral("Badges")).deleteEntry(QStringLiteral("InTaskManager"), KConfig::Notify);
        config->sync();
    }
    SECTION("app blacklisted")
    {
        KConfigGroup app = config->group(QStringLiteral("Applications")).group(QStringLiteral("org.kde.konsole"));
        app.writeEntry(QStringLiteral("ShowBadges"), false, KConfig::Notify);
        config->sync();
        CHECK(waitFor([tracker] {
            return !tracker->countVisible(kKonsole) && tracker->count(kKonsole) == 0;
        }));
        CHECK(tracker->progressVisible(kKonsole));
        config->group(QStringLiteral("Applications")).deleteGroup(QStringLiteral("org.kde.konsole"), KConfig::Notify);
        config->sync();
    }

    // Removing the setting brings the count back
    CHECK(waitFor([tracker] {
        return tracker->countVisible(kKonsole) && tracker->count(kKonsole) == 3;
    }));
    resetEntries();
}

TEST_CASE("Do Not Disturb keeps LauncherEntry counts", "[launcher-entry][tracker][dnd]")
{
    auto *tracker = app().launcherEntries.get();
    FakeApp konsole(QStringLiteral("launcher-entry-dnd"));
    konsole.update(kKonsoleUri, {{QStringLiteral("count"), 5}, {QStringLiteral("count-visible"), true}});
    REQUIRE(waitFor([tracker] {
        return tracker->countVisible(kKonsole);
    }));

    // QA-09: Krema keeps badges during DND (only the attention animation is
    // suppressed); NotificationTracker seeing DND proves the setting landed
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc"));
    KConfigGroup dnd = config->group(QStringLiteral("DoNotDisturb"));
    dnd.writeEntry(QStringLiteral("Until"), QDateTime::currentDateTime().addSecs(3600), KConfig::Notify);
    config->sync();
    REQUIRE(waitFor([] {
        return app().tracker->dndActive();
    }));
    settle();
    CHECK(tracker->countVisible(kKonsole));
    CHECK(tracker->count(kKonsole) == 5);

    dnd.deleteEntry(QStringLiteral("Until"), KConfig::Notify);
    config->sync();
    REQUIRE(waitFor([] {
        return !app().tracker->dndActive();
    }));
    resetEntries();
}

TEST_CASE("A second listener works while another one owns com.canonical.Unity", "[launcher-entry][tracker][service]")
{
    // QA-15: the first tracker (as plasmashell would) owns the service name;
    // a later one must still receive updates.
    auto *first = app().launcherEntries.get();
    REQUIRE(QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("com.canonical.Unity")));
    LauncherEntryTracker second;
    FakeApp konsole(QStringLiteral("launcher-entry-second"));
    konsole.update(kKonsoleUri, {{QStringLiteral("count"), 8}, {QStringLiteral("count-visible"), true}});
    CHECK(waitFor([&] {
        return second.count(kKonsole) == 8 && first->count(kKonsole) == 8;
    }));
    resetEntries();
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }
    KAboutData aboutData(QStringLiteral("krema"), QStringLiteral("Krema"), QStringLiteral("test"));
    KAboutData::setApplicationData(aboutData);
    initResources();
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
    return Catch::Session().run(argc, argv);
}
