// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "models/taskiconprovider.h"

#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QScopeGuard>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <QStringList>

#include <algorithm>

namespace
{

QGuiApplication &guiApplication()
{
    static int argc = 1;
    static char appName[] = "krema-unit-tests";
    static char *argv[] = {appName, nullptr};
    static bool platformSelected = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return true;
    }();
    Q_UNUSED(platformSelected);
    // Keep the application alive through static QIcon cache destruction at process exit.
    static QGuiApplication *application = new QGuiApplication(argc, argv);
    return *application;
}

QIcon coloredIcon(const QColor &color, const QRect &content, int canvasSize = 32)
{
    QImage image(canvasSize, canvasSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.fillRect(content, color);
    painter.end();
    return QIcon(QPixmap::fromImage(image));
}

QRect alphaBounds(const QPixmap &pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) == 0) {
                continue;
            }
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }

    return bottom < 0 ? QRect() : QRect(left, top, right - left + 1, bottom - top + 1);
}

QColor centerColor(const QPixmap &pixmap)
{
    const QImage image = pixmap.toImage();
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

void requireColorAtCenter(const QPixmap &pixmap, const QColor &expected)
{
    const QColor actual = centerColor(pixmap);
    REQUIRE(actual.alpha() > 200);
    REQUIRE(actual.red() == expected.red());
    REQUIRE(actual.green() == expected.green());
    REQUIRE(actual.blue() == expected.blue());
}

} // namespace

TEST_CASE("TaskIconProvider renders registered raw artwork", "[taskiconprovider][raw]")
{
    Q_UNUSED(guiApplication());

    krema::TaskIconProvider provider(false);
    const QString key = QStringLiteral("raw:test-rendering-7f7d");
    const QColor artwork(220, 35, 55);
    krema::TaskIconProvider::registerRawIcon(key, coloredIcon(artwork, QRect(0, 0, 32, 32)));

    QSize returnedSize;
    const QPixmap rendered = provider.requestPixmap(key + QStringLiteral("?v=19"), &returnedSize, QSize(32, 32));

    REQUIRE(returnedSize == QSize(32, 32));
    REQUIRE(rendered.size() == QSize(32, 32));
    requireColorAtCenter(rendered, artwork);
    REQUIRE(alphaBounds(rendered) == QRect(0, 0, 32, 32));
}

TEST_CASE("TaskIconProvider replaces raw artwork under the same key", "[taskiconprovider][raw]")
{
    Q_UNUSED(guiApplication());

    krema::TaskIconProvider provider(false);
    const QString key = QStringLiteral("raw:test-replacement-3b16");
    const QColor first(235, 45, 45);
    const QColor replacement(35, 75, 235);

    krema::TaskIconProvider::registerRawIcon(key, coloredIcon(first, QRect(0, 0, 32, 32)));
    requireColorAtCenter(provider.requestPixmap(key, nullptr, QSize(32, 32)), first);

    krema::TaskIconProvider::registerRawIcon(key, coloredIcon(replacement, QRect(0, 0, 32, 32)));
    const QPixmap rendered = provider.requestPixmap(key, nullptr, QSize(32, 32));
    requireColorAtCenter(rendered, replacement);
    REQUIRE(centerColor(rendered) != first);
}

TEST_CASE("TaskIconProvider keeps distinct raw identities independent", "[taskiconprovider][raw][identity]")
{
    Q_UNUSED(guiApplication());

    krema::TaskIconProvider provider(false);
    const QString emptyOwnerKey = QStringLiteral("raw:owner:");
    const QString sharedOwnerKey = QStringLiteral("raw:owner:shared");
    const QColor emptyOwnerArtwork(245, 170, 25);
    const QColor sharedOwnerArtwork(25, 185, 210);

    // The two keys model distinct opaque identities even when one owner suffix is empty.
    const QIcon sharedArtwork = coloredIcon(sharedOwnerArtwork, QRect(0, 0, 32, 32));
    krema::TaskIconProvider::registerRawIcon(emptyOwnerKey, coloredIcon(emptyOwnerArtwork, QRect(0, 0, 32, 32)));
    krema::TaskIconProvider::registerRawIcon(sharedOwnerKey, sharedArtwork);

    requireColorAtCenter(provider.requestPixmap(emptyOwnerKey, nullptr, QSize(32, 32)), emptyOwnerArtwork);
    requireColorAtCenter(provider.requestPixmap(sharedOwnerKey, nullptr, QSize(32, 32)), sharedOwnerArtwork);

    // Null artwork and an actually empty key must not overwrite a valid registration.
    krema::TaskIconProvider::registerRawIcon(emptyOwnerKey, QIcon());
    krema::TaskIconProvider::registerRawIcon(QString(), coloredIcon(QColor(10, 10, 10), QRect(0, 0, 32, 32)));
    requireColorAtCenter(provider.requestPixmap(emptyOwnerKey, nullptr, QSize(32, 32)), emptyOwnerArtwork);
}

TEST_CASE("TaskIconProvider refreshes normalization after raw replacement", "[taskiconprovider][raw][normalization]")
{
    Q_UNUSED(guiApplication());

    krema::TaskIconProvider provider(true);
    const QString key = QStringLiteral("raw:test-normalization-revision-9c20");
    const QColor first(225, 45, 50);
    const QColor replacement(40, 90, 225);

    // Match the 256px analysis probe so QIcon's refusal to upscale small rasters
    // cannot make the measured padding depend on the fixture's resolution.
    krema::TaskIconProvider::registerRawIcon(key, coloredIcon(first, QRect(96, 96, 64, 64), 256));
    const QPixmap firstRendered = provider.requestPixmap(key, nullptr, QSize(32, 32));
    const QRect firstBounds = alphaBounds(firstRendered);
    requireColorAtCenter(firstRendered, first);
    REQUIRE(firstRendered.size() == QSize(32, 32));
    REQUIRE(firstBounds.width() < 20);
    REQUIRE(firstBounds.height() < 20);

    // The full-canvas replacement takes the breathing-room shrink path, while
    // stale metadata would still normalize it to the first icon's small body.
    krema::TaskIconProvider::registerRawIcon(key, coloredIcon(replacement, QRect(0, 0, 256, 256), 256));
    const QPixmap replacementRendered = provider.requestPixmap(key, nullptr, QSize(32, 32));
    const QRect replacementBounds = alphaBounds(replacementRendered);
    requireColorAtCenter(replacementRendered, replacement);
    REQUIRE(replacementRendered.size() == firstRendered.size());
    REQUIRE(replacementBounds.width() > firstBounds.width());
    REQUIRE(replacementBounds.height() > firstBounds.height());

    // A provider with no cached analysis is the geometry oracle; this allows
    // intentional margins without pinning their exact pixel dimensions.
    krema::TaskIconProvider freshProvider(true);
    const QPixmap freshReplacement = freshProvider.requestPixmap(key, nullptr, QSize(32, 32));
    requireColorAtCenter(freshReplacement, replacement);
    REQUIRE(replacementBounds == alphaBounds(freshReplacement));
}

TEST_CASE("TaskIconProvider preserves healthy named theme lookup", "[taskiconprovider][theme]")
{
    Q_UNUSED(guiApplication());

    QTemporaryDir themeDirectory;
    REQUIRE(themeDirectory.isValid());

    const QString themeName = QStringLiteral("krema-named-fixture-theme");
    const QString iconName = QStringLiteral("krema-named-fixture-icon");
    const QString iconDirectory = themeDirectory.path() + QLatin1Char('/') + themeName + QStringLiteral("/32x32");
    REQUIRE(QDir().mkpath(iconDirectory));

    const QString indexPath = themeDirectory.path() + QLatin1Char('/') + themeName + QStringLiteral("/index.theme");
    QFile indexFile(indexPath);
    REQUIRE(indexFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    indexFile.write("[Icon Theme]\n"
                    "Name=Krema Named Fixture\n"
                    "Directories=32x32\n"
                    "\n"
                    "[32x32]\n"
                    "Size=32\n"
                    "Type=Fixed\n");
    indexFile.close();

    const QString artworkPath = iconDirectory + QLatin1Char('/') + iconName + QStringLiteral(".png");
    QImage fixture(32, 32, QImage::Format_ARGB32_Premultiplied);
    fixture.fill(Qt::transparent);
    {
        QPainter painter(&fixture);
        painter.fillRect(QRect(2, 2, 28, 28), QColor(23, 180, 91));
        painter.fillRect(QRect(10, 10, 12, 12), QColor(245, 188, 32));
    }
    REQUIRE(fixture.save(artworkPath));

    QImage expected;
    REQUIRE(expected.load(artworkPath));
    expected = expected.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const QStringList previousSearchPaths = QIcon::themeSearchPaths();
    const QString previousThemeName = QIcon::themeName();
    const auto restoreTheme = qScopeGuard([&] {
        QIcon::setThemeSearchPaths(previousSearchPaths);
        QIcon::setThemeName(previousThemeName);
    });
    QIcon::setThemeSearchPaths(QStringList{themeDirectory.path()});
    QIcon::setThemeName(themeName);

    const QIcon directThemeIcon = QIcon::fromTheme(iconName);
    REQUIRE_FALSE(directThemeIcon.isNull());
    const QPixmap directPixmap = directThemeIcon.pixmap(QSize(32, 32), 1.0);
    REQUIRE_FALSE(directPixmap.isNull());
    REQUIRE(directPixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied) == expected);

    krema::TaskIconProvider provider(false);
    const QPixmap rendered = provider.requestPixmap(iconName, nullptr, QSize(32, 32));
    REQUIRE_FALSE(rendered.isNull());
    REQUIRE(rendered.size() == QSize(32, 32));
    REQUIRE(rendered.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied) == expected);
}
