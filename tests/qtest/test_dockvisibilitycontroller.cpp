// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "mockdockplatform.h"
#include "shell/dockvisibilitycontroller.h"

#include <taskmanager/activityinfo.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/virtualdesktopinfo.h>

#include <QSignalSpy>
#include <QTest>
#include <QWindow>

using namespace krema;
using namespace krema::testing;

/// Test subclass that overrides hasOverlappingWindow for DodgeWindows testing.
class TestableVisibilityController : public DockVisibilityController
{
public:
    using DockVisibilityController::DockVisibilityController;

    bool m_fakeOverlap = false;

protected:
    bool hasOverlappingWindow(bool /*activeOnly*/) const override
    {
        return m_fakeOverlap;
    }
};

class TestDockVisibilityController : public QObject
{
    Q_OBJECT

private:
    struct Env {
        MockDockPlatform platform;
        TaskManager::TasksModel tasksModel;
        TaskManager::VirtualDesktopInfo vdi;
        TaskManager::ActivityInfo ai;
        QWindow window;
    };

    std::unique_ptr<Env> makeEnv()
    {
        auto env = std::make_unique<Env>();
        env->tasksModel.classBegin();
        env->tasksModel.componentComplete();
        return env;
    }

    std::unique_ptr<DockVisibilityController> makeController(Env &env)
    {
        return std::make_unique<DockVisibilityController>(&env.platform, &env.tasksModel, &env.vdi, &env.ai, &env.window);
    }

    std::unique_ptr<TestableVisibilityController> makeTestableController(Env &env)
    {
        return std::make_unique<TestableVisibilityController>(&env.platform, &env.tasksModel, &env.vdi, &env.ai, &env.window);
    }

private Q_SLOTS:

    // --- AlwaysVisible mode ---

    void alwaysVisible_initiallyVisible()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AlwaysVisible));

        QVERIFY(ctrl->isDockVisible());
    }

    void alwaysVisible_staysVisibleOnUnhover()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AlwaysVisible));

        ctrl->setHovered(true);
        ctrl->setHovered(false);

        // Even after unhover + potential timer, should remain visible
        QTest::qWait(600);
        QVERIFY(ctrl->isDockVisible());
    }

    void alwaysVisible_toggleIsNoop()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AlwaysVisible));

        ctrl->toggleVisibility();
        QVERIFY(ctrl->isDockVisible());
    }

    // --- AutoHide mode ---

    void autoHide_initiallyHidden()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));

        QVERIFY(!ctrl->isDockVisible());
    }

    void autoHide_hoverShowsAfterDelay()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));
        ctrl->setShowDelay(50);

        ctrl->setHovered(true);
        // Not yet visible (delay pending)
        QVERIFY(!ctrl->isDockVisible());

        // Wait for show timer
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->isDockVisible(), 200);
    }

    void autoHide_unhoverHidesAfterDelay()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));
        ctrl->setShowDelay(10);
        ctrl->setHideDelay(50);

        // Show the dock
        ctrl->setHovered(true);
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->isDockVisible(), 200);

        // Unhover
        ctrl->setHovered(false);
        QVERIFY(ctrl->isDockVisible()); // Still visible (delay pending)

        QTRY_VERIFY_WITH_TIMEOUT(!ctrl->isDockVisible(), 500);
    }

    void autoHide_hoverCancelsHideTimer()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));
        ctrl->setShowDelay(10);
        ctrl->setHideDelay(100);

        // Show
        ctrl->setHovered(true);
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->isDockVisible(), 200);

        // Start hide, then re-hover before timer fires
        ctrl->setHovered(false);
        QTest::qWait(30);
        ctrl->setHovered(true);

        // Should stay visible
        QTest::qWait(200);
        QVERIFY(ctrl->isDockVisible());
    }

    void autoHide_toggleVisibility()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));

        QVERIFY(!ctrl->isDockVisible());
        ctrl->toggleVisibility();
        QVERIFY(ctrl->isDockVisible());
        ctrl->toggleVisibility();
        QVERIFY(!ctrl->isDockVisible());
    }

    // --- Interaction lock ---

    void interactionLock_preventsHiding()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));
        ctrl->setShowDelay(10);
        ctrl->setHideDelay(50);

        // Show
        ctrl->setHovered(true);
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->isDockVisible(), 200);

        // Lock interaction (context menu open)
        ctrl->setInteracting(true);
        ctrl->setHovered(false);

        // Should NOT hide even after delay
        QTest::qWait(200);
        QVERIFY(ctrl->isDockVisible());

        // Release lock
        ctrl->setInteracting(false);

        // Now should hide
        QTRY_VERIFY_WITH_TIMEOUT(!ctrl->isDockVisible(), 500);
    }

    void interactionLock_refCounting()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));
        ctrl->setHideDelay(50);

        // Show via toggle
        ctrl->toggleVisibility();
        QVERIFY(ctrl->isDockVisible());

        // Two locks
        ctrl->setInteracting(true);
        ctrl->setInteracting(true);
        ctrl->setHovered(false);

        // Release one — still locked
        ctrl->setInteracting(false);
        QTest::qWait(200);
        QVERIFY(ctrl->isDockVisible());

        // Release second — now should hide
        ctrl->setInteracting(false);
        QTRY_VERIFY_WITH_TIMEOUT(!ctrl->isDockVisible(), 500);
    }

    void interactionLock_neverGoesNegative()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));

        // Release without acquire — should not crash or go negative
        ctrl->setInteracting(false);
        ctrl->setInteracting(false);

        // Acquire should still show dock (count goes to 1, not -1+1=0)
        ctrl->setInteracting(true);
        QVERIFY(ctrl->isDockVisible());

        // Single release should bring count back to 0
        ctrl->setInteracting(false);
    }

    // --- Keyboard navigation ---

    void keyboardActive_preventsHiding()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));
        ctrl->setHideDelay(50);

        ctrl->setKeyboardActive(true);
        QVERIFY(ctrl->isDockVisible());
        QVERIFY(env->platform.m_keyboardInteractive);

        // Unhover should not hide while keyboard active
        ctrl->setHovered(false);
        QTest::qWait(200);
        QVERIFY(ctrl->isDockVisible());
    }

    void keyboardActive_hidesOnDeactivate()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));
        ctrl->setHideDelay(50);

        ctrl->setKeyboardActive(true);
        QVERIFY(ctrl->isDockVisible());

        ctrl->setKeyboardActive(false);
        QVERIFY(!env->platform.m_keyboardInteractive);

        QTRY_VERIFY_WITH_TIMEOUT(!ctrl->isDockVisible(), 500);
    }

    // --- DodgeWindows mode (using testable subclass) ---

    void dodgeWindows_visibleWhenNoOverlap()
    {
        auto env = makeEnv();
        auto ctrl = makeTestableController(*env);
        ctrl->m_fakeOverlap = false;
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::DodgeWindows));

        QVERIFY(ctrl->isDockVisible());
    }

    void dodgeWindows_hidesOnOverlap()
    {
        auto env = makeEnv();
        auto ctrl = makeTestableController(*env);
        ctrl->m_fakeOverlap = true;
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::DodgeWindows));

        // evaluateVisibility is called on mode change; wait for debounce
        QTRY_VERIFY_WITH_TIMEOUT(!ctrl->isDockVisible(), 500);
    }

    void dodgeWindows_showsWhenOverlapRemoved()
    {
        auto env = makeEnv();
        auto ctrl = makeTestableController(*env);
        ctrl->m_fakeOverlap = true;
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::DodgeWindows));
        QTRY_VERIFY_WITH_TIMEOUT(!ctrl->isDockVisible(), 500);

        // Overlap removed
        ctrl->m_fakeOverlap = false;
        ctrl->requestEvaluate();
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->isDockVisible(), 500);
    }

    void dodgeWindows_hoverOverridesOverlap()
    {
        auto env = makeEnv();
        auto ctrl = makeTestableController(*env);
        ctrl->m_fakeOverlap = true;
        ctrl->setShowDelay(10);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::DodgeWindows));
        QTRY_VERIFY_WITH_TIMEOUT(!ctrl->isDockVisible(), 500);

        // Hover should show even with overlap
        ctrl->setHovered(true);
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->isDockVisible(), 200);
    }

    void dodgeWindows_interactionOverridesOverlap()
    {
        auto env = makeEnv();
        auto ctrl = makeTestableController(*env);
        ctrl->m_fakeOverlap = true;
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::DodgeWindows));
        QTRY_VERIFY_WITH_TIMEOUT(!ctrl->isDockVisible(), 500);

        ctrl->setInteracting(true);
        QVERIFY(ctrl->isDockVisible());
    }

    // --- Mode switching ---

    void modeSwitchClearsTimers()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));
        ctrl->setShowDelay(10);
        ctrl->setHideDelay(500);

        // Start show timer
        ctrl->setHovered(true);
        QTRY_VERIFY_WITH_TIMEOUT(ctrl->isDockVisible(), 200);

        // Start hide timer
        ctrl->setHovered(false);

        // Switch to AlwaysVisible before hide timer fires
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AlwaysVisible));

        // Should be visible and stay visible
        QTest::qWait(600);
        QVERIFY(ctrl->isDockVisible());
    }

    void modeSignalEmitted()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);

        QSignalSpy modeSpy(ctrl.get(), &DockVisibilityController::modeChanged);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));

        QCOMPARE(modeSpy.count(), 1);
    }

    void visibleSignalEmitted()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);
        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));

        QSignalSpy visibleSpy(ctrl.get(), &DockVisibilityController::dockVisibleChanged);
        ctrl->toggleVisibility();

        QCOMPARE(visibleSpy.count(), 1);
    }

    // --- Platform interaction ---

    void platformVisibilityModeUpdated()
    {
        auto env = makeEnv();
        auto ctrl = makeController(*env);

        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::AutoHide));
        QCOMPARE(env->platform.m_visibilityMode, DockPlatform::VisibilityMode::AutoHide);

        ctrl->setMode(static_cast<int>(DockPlatform::VisibilityMode::DodgeWindows));
        QCOMPARE(env->platform.m_visibilityMode, DockPlatform::VisibilityMode::DodgeWindows);
    }
};

QTEST_MAIN(TestDockVisibilityController)
#include "test_dockvisibilitycontroller.moc"
