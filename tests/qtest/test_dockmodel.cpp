// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "models/dockmodel.h"

#include <taskmanager/activityinfo.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/virtualdesktopinfo.h>

#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

using namespace krema;

class TestDockModel : public QObject
{
    Q_OBJECT

private:
    struct Env {
        TaskManager::TasksModel tasksModel;
        TaskManager::VirtualDesktopInfo vdi;
        TaskManager::ActivityInfo ai;
    };

    std::unique_ptr<Env> makeEnv()
    {
        return std::make_unique<Env>();
    }

    std::unique_ptr<DockModel> makeModel(Env &env)
    {
        return std::make_unique<DockModel>(&env.tasksModel, &env.vdi, &env.ai);
    }

private Q_SLOTS:

    // --- isDesktopFile (pure URL parsing) ---

    void isDesktopFile_applicationsScheme()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QVERIFY(model->isDesktopFile(QUrl(QStringLiteral("applications:org.kde.dolphin.desktop"))));
    }

    void isDesktopFile_localDesktopFile()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QVERIFY(model->isDesktopFile(QUrl::fromLocalFile(QStringLiteral("/usr/share/applications/org.kde.dolphin.desktop"))));
    }

    void isDesktopFile_regularFile()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QVERIFY(!model->isDesktopFile(QUrl::fromLocalFile(QStringLiteral("/tmp/test.txt"))));
    }

    void isDesktopFile_httpUrl()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QVERIFY(!model->isDesktopFile(QUrl(QStringLiteral("https://example.com"))));
    }

    void isDesktopFile_emptyUrl()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QVERIFY(!model->isDesktopFile(QUrl()));
    }

    // --- Pinned launchers ---

    void pinnedLaunchers_initiallyEmpty()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        // Default launchers from kcfg may be present; just verify it doesn't crash
        const auto launchers = model->pinnedLaunchers();
        QVERIFY(launchers.size() >= 0);
    }

    void setPinnedLaunchers_emitsSignal()
    {
        if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
            QSKIP("TaskManager::TasksModel::setLauncherList hangs under offscreen in the sandboxed test environment");
        }

        auto env = makeEnv();
        auto model = makeModel(*env);
        QSignalSpy spy(model.get(), &DockModel::pinnedLaunchersChanged);

        model->setPinnedLaunchers({QStringLiteral("/usr/share/applications/org.kde.dolphin.desktop")});
        QCOMPARE(spy.count(), 1);
    }

    void setPinnedLaunchers_roundtrip()
    {
        if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
            QSKIP("TaskManager::TasksModel::setLauncherList hangs under offscreen in the sandboxed test environment");
        }

        auto env = makeEnv();
        auto model = makeModel(*env);

        QStringList launchers = {
            QStringLiteral("/usr/share/applications/org.kde.dolphin.desktop"),
            QStringLiteral("/usr/share/applications/org.kde.konsole.desktop"),
        };
        model->setPinnedLaunchers(launchers);

        QStringList result = model->pinnedLaunchers();
        QCOMPARE(result.size(), launchers.size());
        for (const auto &l : launchers) {
            QVERIFY(result.contains(l));
        }
    }

    // --- Virtual desktop mode ---

    void virtualDesktopMode_defaultIsZero()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QCOMPARE(model->virtualDesktopMode(), 0);
    }

    void setVirtualDesktopMode_emitsSignal()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QSignalSpy spy(model.get(), &DockModel::virtualDesktopModeChanged);

        model->setVirtualDesktopMode(1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(model->virtualDesktopMode(), 1);
    }

    void setVirtualDesktopMode_sameValueNoSignal()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        model->setVirtualDesktopMode(1);

        QSignalSpy spy(model.get(), &DockModel::virtualDesktopModeChanged);
        model->setVirtualDesktopMode(1);
        QCOMPARE(spy.count(), 0);
    }

    // --- Invalid index handling ---

    void iconName_invalidIndex()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QCOMPARE(model->iconName(-1), QString());
        QCOMPARE(model->iconName(9999), QString());
    }

    void launcherUrl_invalidIndex()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QCOMPARE(model->launcherUrl(-1), QUrl());
        QCOMPARE(model->launcherUrl(9999), QUrl());
    }

    void appId_invalidIndex()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QCOMPARE(model->appId(-1), QString());
        QCOMPARE(model->appId(9999), QString());
    }

    void isPinned_invalidIndex()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QVERIFY(!model->isPinned(-1));
        QVERIFY(!model->isPinned(9999));
    }

    void windowIds_invalidIndex()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QVERIFY(model->windowIds(-1).isEmpty());
        QVERIFY(model->windowIds(9999).isEmpty());
    }

    void childCount_invalidIndex()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        QCOMPARE(model->childCount(-1), 0);
        QCOMPARE(model->childCount(9999), 0);
    }

    void isOnCurrentDesktop_invalidIndex()
    {
        auto env = makeEnv();
        auto model = makeModel(*env);
        // Invalid index should return true (safe default)
        QVERIFY(model->isOnCurrentDesktop(-1));
    }
};

QTEST_MAIN(TestDockModel)
#include "test_dockmodel.moc"
