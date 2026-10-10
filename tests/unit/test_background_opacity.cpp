// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// BackgroundOpacity range of the generated KremaSettings (krema.kcfg).
//
// The Panel Style page opacity slider offers 0% to 100%. Every value it can
// produce, including 0% (fully transparent background, no blur), must be stored
// as chosen and survive a restart instead of being clamped by KConfigXT.

#include "krema.h"

#include <catch2/catch_test_macros.hpp>

#include <KConfig>
#include <KConfigGroup>

#include <QTemporaryDir>

namespace
{

// Isolates kremarc in a throwaway XDG_CONFIG_HOME for the test's lifetime.
struct ScratchConfig {
    QTemporaryDir dir;
    QByteArray previous;

    ScratchConfig()
    {
        REQUIRE(dir.isValid());
        previous = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", dir.path().toLocal8Bit());
    }

    ~ScratchConfig()
    {
        if (previous.isNull()) {
            qunsetenv("XDG_CONFIG_HOME");
        } else {
            qputenv("XDG_CONFIG_HOME", previous);
        }
    }

    [[nodiscard]] QString kremarc() const
    {
        return dir.filePath(QStringLiteral("kremarc"));
    }
};

// Sets the opacity the way the settings slider does (property write), saves,
// and returns what a freshly started Krema loads.
double storeAndReload(double opacity)
{
    {
        KremaSettings settings;
        settings.load();
        settings.setBackgroundOpacity(opacity);
        settings.save();
    }
    KremaSettings restarted;
    restarted.load();
    return restarted.backgroundOpacity();
}

} // namespace

TEST_CASE("BackgroundOpacity keeps every value the settings slider offers", "[settings][background-opacity]")
{
    ScratchConfig config;

    SECTION("0% is stored and survives a restart")
    {
        CHECK(storeAndReload(0.0) == 0.0);

        const KConfig onDisk(config.kremarc(), KConfig::SimpleConfig);
        CHECK(onDisk.group(QStringLiteral("General")).readEntry("BackgroundOpacity", -1.0) == 0.0);
    }

    SECTION("intermediate and full opacity are unchanged")
    {
        CHECK(storeAndReload(0.05) == 0.05);
        CHECK(storeAndReload(0.35) == 0.35);
        CHECK(storeAndReload(1.0) == 1.0);
    }

    SECTION("out-of-range values are clamped to [0, 1]")
    {
        CHECK(storeAndReload(-0.2) == 0.0);
        CHECK(storeAndReload(1.5) == 1.0);
    }
}
