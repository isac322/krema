// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "../../src/utils/identitymanager.h"
#include <QUrl>
#include <catch2/catch_test_macros.hpp>

using namespace krema;

TEST_CASE("IdentityManager::normalizeAppId", "[identity]")
{
    SECTION("Standardization")
    {
        CHECK(IdentityManager::normalizeAppId(QStringLiteral("org.kde.dolphin.desktop")) == QStringLiteral("org.kde.dolphin"));
        CHECK(IdentityManager::normalizeAppId(QStringLiteral("ORG.KDE.DOLPHIN.DESKTOP")) == QStringLiteral("org.kde.dolphin"));
        CHECK(IdentityManager::normalizeAppId(QStringLiteral("dolphin")) == QStringLiteral("org.kde.dolphin")); // via Bridge
        CHECK(IdentityManager::normalizeAppId(QStringLiteral("systemsettings")) == QStringLiteral("org.kde.systemsettings")); // via Bridge
    }

    SECTION("KDE Bridges")
    {
        CHECK(IdentityManager::normalizeAppId(QStringLiteral("konsole")) == QStringLiteral("org.kde.konsole"));
        CHECK(IdentityManager::normalizeAppId(QStringLiteral("kcalc")) == QStringLiteral("org.kde.kcalc"));
    }

    SECTION("Steam IDs")
    {
        CHECK(IdentityManager::normalizeAppId(QStringLiteral("steam_app_1234")) == QStringLiteral("steam_app_1234"));
    }
}

TEST_CASE("IdentityManager::appIdFromUrl", "[identity]")
{
    CHECK(IdentityManager::appIdFromUrl(QUrl(QStringLiteral("applications:org.kde.dolphin.desktop"))) == QStringLiteral("org.kde.dolphin"));
    CHECK(IdentityManager::appIdFromUrl(QUrl(QStringLiteral("applications:dolphin.desktop"))) == QStringLiteral("org.kde.dolphin"));
    CHECK(IdentityManager::appIdFromUrl(QUrl(QStringLiteral("file:///usr/share/applications/org.kde.dolphin.desktop"))) == QStringLiteral("org.kde.dolphin"));
}

TEST_CASE("IdentityManager::iconCandidates", "[identity]")
{
    QStringList candidates = IdentityManager::iconCandidates(QStringLiteral("dolphin"));

    CHECK(candidates.contains(QStringLiteral("org.kde.dolphin")));
    CHECK(candidates.contains(QStringLiteral("dolphin")));

    candidates = IdentityManager::iconCandidates(QStringLiteral("steam_app_123"));
    CHECK(candidates.contains(QStringLiteral("steam_icon_123")));
    CHECK(candidates.contains(QStringLiteral("steam")));
}
