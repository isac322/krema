// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "models/dockitemsmodel.h"
#include "models/dockmodel.h"
#include "models/trashmanager.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QMetaEnum>
#include <QScopeGuard>
#include <QTemporaryDir>

namespace
{

QGuiApplication &guiApplication()
{
    static int argc = 1;
    static char appName[] = "krema-trash-unit-tests";
    static char *argv[] = {appName, nullptr};
    static bool platformSelected = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return true;
    }();
    Q_UNUSED(platformSelected);
    static QGuiApplication *application = new QGuiApplication(argc, argv);
    return *application;
}

void writeThemeWithoutFullTrashIcon(const QString &root, const QString &themeName)
{
    const QString themeRoot = root + QLatin1Char('/') + themeName;
    const QString iconDirectory = themeRoot + QStringLiteral("/32x32");
    REQUIRE(QDir().mkpath(iconDirectory));

    QFile index(themeRoot + QStringLiteral("/index.theme"));
    REQUIRE(index.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(index.write("[Icon Theme]\n"
                        "Name=Krema Trash Fixture\n"
                        "Directories=32x32\n"
                        "\n"
                        "[32x32]\n"
                        "Size=32\n"
                        "Type=Fixed\n") > 0);
    index.close();

    QImage icon(32, 32, QImage::Format_ARGB32_Premultiplied);
    icon.fill(Qt::transparent);
    REQUIRE(icon.save(iconDirectory + QStringLiteral("/user-trash.png")));
}

} // namespace

TEST_CASE("Trash levels expose stable ordered bands", "[trash]")
{
    const QMetaEnum levels = QMetaEnum::fromType<krema::TrashManager::Level>();
    REQUIRE(levels.isValid());
    REQUIRE(levels.keyCount() == 4);
    CHECK(levels.key(0) == QByteArrayLiteral("Empty"));
    CHECK(levels.key(1) == QByteArrayLiteral("Few"));
    CHECK(levels.key(2) == QByteArrayLiteral("Moderate"));
    CHECK(levels.key(3) == QByteArrayLiteral("Many"));
    CHECK(levels.value(0) < levels.value(1));
    CHECK(levels.value(1) < levels.value(2));
    CHECK(levels.value(2) < levels.value(3));
}

TEST_CASE("Trash icon policy falls back to user-trash without a full theme icon", "[trash]")
{
    Q_UNUSED(guiApplication());

    QTemporaryDir themeDirectory;
    REQUIRE(themeDirectory.isValid());
    const QString themeName = QStringLiteral("krema-trash-no-full-icon");
    writeThemeWithoutFullTrashIcon(themeDirectory.path(), themeName);

    const QStringList previousSearchPaths = QIcon::themeSearchPaths();
    const QString previousThemeName = QIcon::themeName();
    const auto restoreTheme = qScopeGuard([&] {
        QIcon::setThemeSearchPaths(previousSearchPaths);
        QIcon::setThemeName(previousThemeName);
    });
    QIcon::setThemeSearchPaths(QStringList{themeDirectory.path()});
    QIcon::setThemeName(themeName);

    krema::TrashManager manager;
    CHECK_FALSE(QIcon::hasThemeIcon(QStringLiteral("user-trash-full")));
    CHECK(manager.iconName() == QStringLiteral("user-trash"));

    krema::DockModel dockModel;
    REQUIRE(dockModel.trashManager()->isEmpty());
    CHECK(dockModel.iconName(dockModel.trashIndex()) == dockModel.trashManager()->iconName());
}

TEST_CASE("Visual Trash row never maps to a task source row", "[trash][mapping]")
{
    krema::DockItemsModel model(nullptr, nullptr);

    REQUIRE(model.rowCount() == 1);
    CHECK(model.trashIndex() == 0);
    CHECK(model.isTrash(0));
    CHECK_FALSE(model.isTrash(-1));
    CHECK_FALSE(model.isTrash(1));
    CHECK(model.taskIndex(0) == -1);
    CHECK(model.visualIndex(-1) == -1);
    CHECK(model.visualIndex(0) == -1);
    CHECK(model.data(model.index(0, 0), krema::DockItemsModel::ItemKindRole).toInt() == krema::DockItemsModel::Trash);
    CHECK(model.data(model.index(0, 0), krema::DockItemsModel::SourceIndexRole).toInt() == -1);

    model.setTrashSlot(0);
    CHECK(model.trashSlot() == 0);
    CHECK(model.trashIndex() == 0);
    CHECK(model.taskIndex(model.trashIndex()) == -1);
}
