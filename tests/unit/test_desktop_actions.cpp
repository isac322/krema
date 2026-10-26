// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "models/desktopactions.h"

#include <catch2/catch_test_macros.hpp>

#include <KService>
#include <KServiceAction>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

namespace
{

std::unique_ptr<QCoreApplication> createCoreApplicationIfNeeded()
{
    if (QCoreApplication::instance()) {
        return nullptr;
    }

    static int argc = 1;
    static char name[] = "krema-desktop-actions-test";
    static char *argv[] = {name, nullptr};
    return std::make_unique<QCoreApplication>(argc, argv);
}

QString writeDesktopFile(const QString &directory, const QString &fileName, const QByteArray &contents)
{
    REQUIRE(QDir().mkpath(directory));
    const QString path = directory + QLatin1Char('/') + fileName;
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(contents) == contents.size());
    file.close();
    return path;
}

QStringList actionNames(const QList<KServiceAction> &actions)
{
    QStringList names;
    for (const KServiceAction &action : actions) {
        names << (action.isSeparator() ? QStringLiteral("|") : action.name());
    }
    return names;
}

// Actions are listed out of alphabetical and group order on purpose: the menu
// must follow the Actions= key, the order the desktop file declares.
const QByteArray browserEntry =
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=Krema Fixture Browser\n"
    "Exec=krema-fixture-browser %u\n"
    "Icon=krema-fixture-browser\n"
    "Actions=new-window;hidden;new-private-window;\n"
    "\n"
    "[Desktop Action new-private-window]\n"
    "Name=New Private Window\n"
    "Name[de]=Neues privates Fenster\n"
    "Exec=krema-fixture-browser --private-window %u\n"
    "\n"
    "[Desktop Action hidden]\n"
    "Name=Hidden Action\n"
    "Exec=krema-fixture-browser --hidden\n"
    "NoDisplay=true\n"
    "\n"
    "[Desktop Action new-window]\n"
    "Name=New Window\n"
    "Name[de]=Neues Fenster\n"
    "Icon=window-new\n"
    "Exec=krema-fixture-browser --new-window %u\n";

} // namespace

TEST_CASE("Desktop actions follow the declared order and skip hidden ones", "[desktopactions]")
{
    // The fixture also has German names: pin the untranslated locale, the
    // same way the i18n case below selects German, so the English labels
    // hold whatever locale the developer runs ctest in.
    qunsetenv("LANGUAGE");
    qputenv("LC_ALL", "C");
    qputenv("LANG", "C");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    // An absolute path outside every XDG applications directory, like a
    // launcher pinned from an arbitrary location.
    const QString path = writeDesktopFile(dir.path(), QStringLiteral("krema-fixture-browser.desktop"), browserEntry);
    const auto application = createCoreApplicationIfNeeded();

    const QList<KServiceAction> actions = krema::desktopActions(QUrl::fromLocalFile(path));

    REQUIRE(actionNames(actions) == QStringList{QStringLiteral("new-window"), QStringLiteral("new-private-window")});
    REQUIRE(actions.at(0).text() == QStringLiteral("New Window"));
    REQUIRE(actions.at(0).icon() == QStringLiteral("window-new"));
    REQUIRE(actions.at(0).exec() == QStringLiteral("krema-fixture-browser --new-window %u"));
    // An action without an Icon key is still offered.
    REQUIRE(actions.at(1).text() == QStringLiteral("New Private Window"));
    REQUIRE(actions.at(1).icon().isEmpty());
    // Each action keeps its application, so launching it does not depend on
    // the task row that opened the menu.
    REQUIRE(actions.at(1).service());
    REQUIRE(actions.at(1).service()->entryPath() == path);
}

TEST_CASE("Desktop actions use the localized action names", "[desktopactions][i18n]")
{
    // KConfig picks the desktop-file locale from the environment on first use.
    qputenv("LANGUAGE", "de");
    qputenv("LC_ALL", "de_DE.UTF-8");
    qputenv("LANG", "de_DE.UTF-8");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = writeDesktopFile(dir.path(), QStringLiteral("krema-fixture-browser.desktop"), browserEntry);
    const auto application = createCoreApplicationIfNeeded();

    const QList<KServiceAction> actions = krema::desktopActions(QUrl::fromLocalFile(path));

    REQUIRE(actions.size() == 2);
    REQUIRE(actions.at(0).text() == QStringLiteral("Neues Fenster"));
    REQUIRE(actions.at(1).text() == QStringLiteral("Neues privates Fenster"));
}

TEST_CASE("Desktop actions keep inner separators only", "[desktopactions]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = writeDesktopFile(dir.path(),
                                          QStringLiteral("krema-fixture-separators.desktop"),
                                          "[Desktop Entry]\n"
                                          "Type=Application\n"
                                          "Name=Krema Fixture Separators\n"
                                          "Exec=krema-fixture-separators\n"
                                          "Actions=_SEPARATOR_;first;hidden;_SEPARATOR_;_SEPARATOR_;second;_SEPARATOR_;\n"
                                          "\n"
                                          "[Desktop Action first]\n"
                                          "Name=First\n"
                                          "Exec=krema-fixture-separators --first\n"
                                          "\n"
                                          "[Desktop Action hidden]\n"
                                          "Name=Hidden\n"
                                          "Exec=krema-fixture-separators --hidden\n"
                                          "NoDisplay=true\n"
                                          "\n"
                                          "[Desktop Action second]\n"
                                          "Name=Second\n"
                                          "Exec=krema-fixture-separators --second\n");
    const auto application = createCoreApplicationIfNeeded();

    const QList<KServiceAction> actions = krema::desktopActions(QUrl::fromLocalFile(path));

    REQUIRE(actionNames(actions) == QStringList{QStringLiteral("first"), QStringLiteral("|"), QStringLiteral("second")});
}

TEST_CASE("Desktop actions are empty when the entry offers none", "[desktopactions]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString plain = writeDesktopFile(dir.path(),
                                           QStringLiteral("krema-fixture-plain.desktop"),
                                           "[Desktop Entry]\n"
                                           "Type=Application\n"
                                           "Name=Krema Fixture Plain\n"
                                           "Exec=krema-fixture-plain\n");
    const auto application = createCoreApplicationIfNeeded();
    REQUIRE(krema::desktopActions(QUrl::fromLocalFile(plain)).isEmpty());

    // Only hidden actions and separators: nothing to show.
    const QString hiddenOnly = writeDesktopFile(dir.path(),
                                                QStringLiteral("krema-fixture-hidden.desktop"),
                                                "[Desktop Entry]\n"
                                                "Type=Application\n"
                                                "Name=Krema Fixture Hidden\n"
                                                "Exec=krema-fixture-hidden\n"
                                                "Actions=hidden;_SEPARATOR_;\n"
                                                "\n"
                                                "[Desktop Action hidden]\n"
                                                "Name=Hidden\n"
                                                "Exec=krema-fixture-hidden --hidden\n"
                                                "NoDisplay=true\n");
    REQUIRE(krema::desktopActions(QUrl::fromLocalFile(hiddenOnly)).isEmpty());

    // Launchers that do not resolve to an application.
    REQUIRE(krema::desktopActions(QUrl()).isEmpty());
    REQUIRE(krema::desktopActions(QUrl::fromLocalFile(dir.path() + QStringLiteral("/missing.desktop"))).isEmpty());
    REQUIRE(krema::desktopActions(QUrl::fromLocalFile(dir.path() + QStringLiteral("/krema-fixture-plain.txt"))).isEmpty());
    REQUIRE(krema::desktopActions(QUrl(QStringLiteral("applications:krema-fixture-not-installed.desktop"))).isEmpty());
    REQUIRE(krema::desktopActions(QUrl(QStringLiteral("https://example.org/app.desktop"))).isEmpty());
}

TEST_CASE("Desktop actions resolve installed and user-local applications", "[desktopactions][sycoca]")
{
    // A private XDG tree: KSycoca builds its database from it on first use.
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString dataHome = root.path() + QStringLiteral("/data-home");
    const QString dataDir = root.path() + QStringLiteral("/data-dir");
    REQUIRE(QDir().mkpath(root.path() + QStringLiteral("/cache")));
    qputenv("XDG_DATA_HOME", dataHome.toLocal8Bit());
    qputenv("XDG_DATA_DIRS", dataDir.toLocal8Bit());
    qputenv("XDG_CACHE_HOME", (root.path() + QStringLiteral("/cache")).toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", (root.path() + QStringLiteral("/config")).toLocal8Bit());

    // System-wide (XDG_DATA_DIRS) and user-local (~/.local/share/applications).
    const QString installed = writeDesktopFile(dataDir + QStringLiteral("/applications"), QStringLiteral("krema-fixture-browser.desktop"), browserEntry);
    const QString local = writeDesktopFile(dataHome + QStringLiteral("/applications"),
                                           QStringLiteral("krema-fixture-local.desktop"),
                                           "[Desktop Entry]\n"
                                           "Type=Application\n"
                                           "Name=Krema Fixture Local\n"
                                           "Exec=krema-fixture-local\n"
                                           "Actions=compose;\n"
                                           "\n"
                                           "[Desktop Action compose]\n"
                                           "Name=Compose\n"
                                           "Icon=mail-message-new\n"
                                           "Exec=krema-fixture-local --compose\n");
    const auto application = createCoreApplicationIfNeeded();

    // TaskManager reports installed applications as applications: URLs and
    // others as file URLs; both must resolve.
    const QList<KServiceAction> byMenuId = krema::desktopActions(QUrl(QStringLiteral("applications:krema-fixture-browser.desktop")));
    REQUIRE(actionNames(byMenuId) == QStringList{QStringLiteral("new-window"), QStringLiteral("new-private-window")});
    REQUIRE(byMenuId.at(0).service()->entryPath() == installed);

    const QList<KServiceAction> byPath = krema::desktopActions(QUrl::fromLocalFile(installed));
    REQUIRE(actionNames(byPath) == QStringList{QStringLiteral("new-window"), QStringLiteral("new-private-window")});

    const QList<KServiceAction> userLocal = krema::desktopActions(QUrl(QStringLiteral("applications:krema-fixture-local.desktop")));
    REQUIRE(actionNames(userLocal) == QStringList{QStringLiteral("compose")});
    REQUIRE(userLocal.at(0).icon() == QStringLiteral("mail-message-new"));
    REQUIRE(userLocal.at(0).service()->entryPath() == local);
    REQUIRE(actionNames(krema::desktopActions(QUrl::fromLocalFile(local))) == QStringList{QStringLiteral("compose")});
}
