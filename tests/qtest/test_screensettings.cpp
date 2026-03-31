// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "config/screensettings.h"
#include "krema.h"

#include <KSharedConfig>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace krema;

class TestScreenSettings : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<KremaSettings> makeSettings()
    {
        auto s = std::make_unique<KremaSettings>();
        s->load();
        return s;
    }

private Q_SLOTS:

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void cleanup()
    {
        // Remove test config between tests
        auto config = KSharedConfig::openConfig(QStringLiteral("kremarc"));
        for (const auto &group : config->groupList()) {
            config->deleteGroup(group);
        }
        config->sync();
    }

    // --- Fallback to global defaults ---

    void noOverride_returnsGlobalIconSize()
    {
        auto global = makeSettings();
        global->setIconSize(64);
        global->save();

        ScreenSettings ss(QStringLiteral("HDMI-A-1"), global.get());
        QCOMPARE(ss.iconSize(), 64);
    }

    void noOverride_hasOverridesIsFalse()
    {
        auto global = makeSettings();
        ScreenSettings ss(QStringLiteral("HDMI-A-1"), global.get());
        QVERIFY(!ss.hasOverrides());
    }

    // --- Per-screen overrides ---

    void setIconSize_overridesGlobal()
    {
        auto global = makeSettings();
        global->setIconSize(48);
        global->save();

        ScreenSettings ss(QStringLiteral("HDMI-A-1"), global.get());
        ss.setIconSize(72);
        ss.save();

        QCOMPARE(ss.iconSize(), 72);
        QVERIFY(ss.hasOverrides());
    }

    void setEdge_overridesGlobal()
    {
        auto global = makeSettings();
        global->setEdge(1); // Bottom
        global->save();

        ScreenSettings ss(QStringLiteral("DP-1"), global.get());
        ss.setEdge(0); // Top
        ss.save();

        QCOMPARE(ss.edge(), 0);
    }

    void setVisibilityMode_overridesGlobal()
    {
        auto global = makeSettings();
        global->setVisibilityMode(0); // AlwaysVisible
        global->save();

        ScreenSettings ss(QStringLiteral("DP-1"), global.get());
        ss.setVisibilityMode(1); // AutoHide
        ss.save();

        QCOMPARE(ss.visibilityMode(), 1);
    }

    void setBackgroundStyle_overridesGlobal()
    {
        auto global = makeSettings();
        global->setBackgroundStyle(0); // PanelInherit
        global->save();

        ScreenSettings ss(QStringLiteral("DP-1"), global.get());
        ss.setBackgroundStyle(3); // Acrylic
        ss.save();

        QCOMPARE(ss.backgroundStyle(), 3);
    }

    void setBackgroundOpacity_overridesGlobal()
    {
        auto global = makeSettings();
        global->setBackgroundOpacity(0.6);
        global->save();

        ScreenSettings ss(QStringLiteral("DP-1"), global.get());
        ss.setBackgroundOpacity(0.9);
        ss.save();

        QCOMPARE(ss.backgroundOpacity(), 0.9);
    }

    void setMaxZoomFactor_overridesGlobal()
    {
        auto global = makeSettings();
        global->setMaxZoomFactor(1.6);
        global->save();

        ScreenSettings ss(QStringLiteral("DP-1"), global.get());
        ss.setMaxZoomFactor(1.2);
        ss.save();

        QCOMPARE(ss.maxZoomFactor(), 1.2);
    }

    void setFloating_overridesGlobal()
    {
        auto global = makeSettings();
        global->setFloating(true);
        global->save();

        ScreenSettings ss(QStringLiteral("DP-1"), global.get());
        ss.setFloating(false);
        ss.save();

        QCOMPARE(ss.floating(), false);
    }

    void setCornerRadius_overridesGlobal()
    {
        auto global = makeSettings();
        global->setCornerRadius(12);
        global->save();

        ScreenSettings ss(QStringLiteral("DP-1"), global.get());
        ss.setCornerRadius(0);
        ss.save();

        QCOMPARE(ss.cornerRadius(), 0);
    }

    void setPinnedLaunchers_overridesGlobal()
    {
        auto global = makeSettings();
        global->setPinnedLaunchers({QStringLiteral("a.desktop")});
        global->save();

        ScreenSettings ss(QStringLiteral("DP-1"), global.get());
        QStringList screenLaunchers = {QStringLiteral("b.desktop"), QStringLiteral("c.desktop")};
        ss.setPinnedLaunchers(screenLaunchers);
        ss.save();

        QCOMPARE(ss.pinnedLaunchers(), screenLaunchers);
    }

    // --- Clear overrides ---

    void clearOverride_revertsToGlobal()
    {
        auto global = makeSettings();
        global->setIconSize(48);
        global->save();

        ScreenSettings ss(QStringLiteral("HDMI-A-1"), global.get());
        ss.setIconSize(72);
        ss.save();
        QCOMPARE(ss.iconSize(), 72);

        ss.clearOverride(QStringLiteral("IconSize"));
        QCOMPARE(ss.iconSize(), 48);
    }

    void clearOverrides_removesAll()
    {
        auto global = makeSettings();

        ScreenSettings ss(QStringLiteral("HDMI-A-1"), global.get());
        ss.setIconSize(72);
        ss.setEdge(0);
        ss.save();
        QVERIFY(ss.hasOverrides());

        ss.clearOverrides();
        QVERIFY(!ss.hasOverrides());
    }

    // --- Signals ---

    void iconSizeChanged_emitted()
    {
        auto global = makeSettings();
        ScreenSettings ss(QStringLiteral("DP-1"), global.get());

        QSignalSpy spy(&ss, &ScreenSettings::iconSizeChanged);
        ss.setIconSize(64);
        QCOMPARE(spy.count(), 1);
    }

    void edgeChanged_emitted()
    {
        auto global = makeSettings();
        ScreenSettings ss(QStringLiteral("DP-1"), global.get());

        QSignalSpy spy(&ss, &ScreenSettings::edgeChanged);
        ss.setEdge(2);
        QCOMPARE(spy.count(), 1);
    }

    // --- Persistence across instances ---

    void overridePersistsAcrossInstances()
    {
        auto global = makeSettings();
        global->setIconSize(48);
        global->save();

        {
            ScreenSettings ss(QStringLiteral("HDMI-A-1"), global.get());
            ss.setIconSize(72);
            ss.save();
        }

        // New instance should read the persisted override
        ScreenSettings ss2(QStringLiteral("HDMI-A-1"), global.get());
        QCOMPARE(ss2.iconSize(), 72);
        QVERIFY(ss2.hasOverrides());
    }

    // --- Different screens are independent ---

    void differentScreensIndependent()
    {
        auto global = makeSettings();
        global->setIconSize(48);
        global->save();

        ScreenSettings ss1(QStringLiteral("HDMI-A-1"), global.get());
        ScreenSettings ss2(QStringLiteral("DP-1"), global.get());

        ss1.setIconSize(64);
        ss1.save();

        // ss2 should still return global default
        QCOMPARE(ss2.iconSize(), 48);
    }
};

QTEST_MAIN(TestScreenSettings)
#include "test_screensettings.moc"
