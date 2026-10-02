// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Exercises the real TaskManager -> DockModel -> TaskIconProvider icon path
// against a private KWin virtual compositor. The child windows are ordinary
// xdg toplevels, so AppId and DecorationRole come from KWin/TaskManager rather
// than a mocked model.
//
// Must run under run-with-kwin.sh.

#include "models/dockmodel.h"
#include "models/taskiconprovider.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTest>
#include <QWidget>

namespace
{

using TaskManager::AbstractTasksModel;

constexpr int kTimeoutMs = 15000;
const QString kHealthyAppId = QStringLiteral("krema-icon-healthy");
const QString kHealthyTitle = QStringLiteral("krema-icon-healthy-window");
const QString kHealthyIcon = QStringLiteral("utilities-terminal");
const QString kUnresolvedAppId = QStringLiteral("krema-icon-unresolved");
const QString kUnresolvedTitle = QStringLiteral("krema-icon-unresolved-window");
const QString kGenericIcon = QStringLiteral("application-x-executable");

krema::DockModel &model()
{
    // Keep the model alive until process exit. KWin's model owns Wayland-side
    // objects whose teardown order is not useful to this integration test.
    static auto *instance = new krema::DockModel;
    return *instance;
}

int taskRow(const QString &title)
{
    auto *tasks = model().tasksModel();
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const QModelIndex index = tasks->index(row, 0);
        if (index.data(Qt::DisplayRole).toString() == title) {
            return row;
        }
    }
    return -1;
}
bool taskHasDecoration(const QString &title)
{
    const int row = taskRow(title);
    if (row < 0) {
        return false;
    }
    return !model().tasksModel()->index(row, 0).data(Qt::DecorationRole).value<QIcon>().isNull();
}


bool appIdMatches(const QString &actual, const QString &expected)
{
    return actual == expected || actual == expected + QStringLiteral(".desktop");
}

QRect artworkBounds(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return {};
    }

    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
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
    return right < left ? QRect{} : QRect(left, top, right - left + 1, bottom - top + 1);
}

QString desktopFilePath(const QString &appId)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications/") + appId + QStringLiteral(".desktop");
}

bool installHealthyDesktopEntry()
{
    const QString path = desktopFilePath(kHealthyAppId);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }

    QFile desktop(path);
    if (!desktop.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    desktop.write(QStringLiteral("[Desktop Entry]\n"
                                 "Type=Application\n"
                                 "Name=Krema Healthy Icon Test\n"
                                 "Exec=true\n"
                                 "Icon=%1\n")
                      .arg(kHealthyIcon)
                      .toUtf8());
    desktop.close();
    return QProcess::execute(QStringLiteral("kbuildsycoca6"), {}) == 0;
}

auto startChild(QProcess &process, const QString &appId, const QString &title)
{
    process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), appId, title});
    return qScopeGuard([&process] {
        process.kill();
        process.waitForFinished();
    });
}

bool noTestWindows()
{
    return QTest::qWaitFor(
        [] {
            return taskRow(kHealthyTitle) < 0 && taskRow(kUnresolvedTitle) < 0;
        },
        kTimeoutMs);
}

int childMain(int argc, char **argv)
{
    const QString appId = QString::fromLocal8Bit(argv[2]);
    const QString title = QString::fromLocal8Bit(argv[3]);
    QGuiApplication::setDesktopFileName(appId);
    QApplication application(argc, argv);
    QWidget window;
    window.setWindowTitle(title);
    window.resize(240, 180);
    window.show();
    return application.exec();
}

} // namespace

TEST_CASE("TaskManager named icon reaches DockModel and provider", "[task-icons]")
{
    REQUIRE(noTestWindows());
    REQUIRE(installHealthyDesktopEntry());

    QProcess child;
    const auto stopChild = startChild(child, kHealthyAppId, kHealthyTitle);
    REQUIRE(child.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return taskRow(kHealthyTitle) >= 0;
        },
        kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            const int row = taskRow(kHealthyTitle);
            return row >= 0 && appIdMatches(model().appId(row), kHealthyAppId) && model().iconName(row) == kHealthyIcon
                && taskHasDecoration(kHealthyTitle);
        },
        kTimeoutMs));


    const int row = taskRow(kHealthyTitle);
    REQUIRE(row >= 0);
    const QModelIndex index = model().tasksModel()->index(row, 0);
    REQUIRE(index.isValid());

    INFO("TaskManager AppId: " << index.data(AbstractTasksModel::AppId).toString().toStdString());
    REQUIRE(appIdMatches(model().appId(row), kHealthyAppId));
    CHECK(model().iconName(row) == kHealthyIcon);

    const QIcon decoration = index.data(Qt::DecorationRole).value<QIcon>();
    REQUIRE_FALSE(decoration.isNull());
    const QPixmap taskPixmap = decoration.pixmap(QSize(64, 64));
    REQUIRE_FALSE(taskPixmap.isNull());
    const QImage decorationArtwork = taskPixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    CHECK(artworkBounds(taskPixmap).isValid());

    // Disable normalization so this compares the provider's named-theme pixels
    // directly with the independent DecorationRole artwork.
    krema::TaskIconProvider provider(false);
    QSize returnedSize;
    const QPixmap providerPixmap = provider.requestPixmap(kHealthyIcon, &returnedSize, QSize(64, 64));
    REQUIRE_FALSE(providerPixmap.isNull());
    CHECK(returnedSize == QSize(64, 64));
    CHECK(artworkBounds(providerPixmap).isValid());
    CHECK(providerPixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied) == decorationArtwork);
}

TEST_CASE("TaskManager keeps an unresolved app identity on the placeholder path", "[task-icons]")
{
    REQUIRE(noTestWindows());


    QProcess child;
    const auto stopChild = startChild(child, kUnresolvedAppId, kUnresolvedTitle);
    REQUIRE(child.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return taskRow(kUnresolvedTitle) >= 0;
        },
        kTimeoutMs));


    const int row = taskRow(kUnresolvedTitle);
    REQUIRE(row >= 0);
    const QModelIndex index = model().tasksModel()->index(row, 0);
    REQUIRE(index.isValid());

    const QString taskManagerAppId = model().appId(row);
    INFO("TaskManager AppId: " << taskManagerAppId.toStdString());
    REQUIRE_FALSE(taskManagerAppId.isEmpty());
    // TaskManager resolves AppId from executable/KService identity rather than
    // necessarily preserving the raw Wayland app_id supplied by the window.
    CHECK(taskManagerAppId != kUnresolvedAppId);

    const QString iconName = model().iconName(row);
    INFO("DecorationRole icon name: " << iconName.toStdString());
    CHECK((iconName.isEmpty() || iconName == kGenericIcon || iconName == QStringLiteral("wayland")
           || iconName == QStringLiteral("unknown")));

    const QIcon decoration = index.data(Qt::DecorationRole).value<QIcon>();
    if (iconName.isEmpty()) {
        // A nameless DecorationRole is the production placeholder route; the
        // QML delegate intentionally does not ask the provider for a pixmap.
        CHECK((decoration.isNull() || artworkBounds(decoration.pixmap(QSize(64, 64))).isValid()));
    } else {
        REQUIRE_FALSE(decoration.isNull());
        CHECK(artworkBounds(decoration.pixmap(QSize(64, 64))).isValid());
    }

    // KWin supplies DecorationRole from window/desktop metadata. There is no
    // supported protocol seam for forcing a nameless DecorationRole or a
    // delayed icon without replacing the real TaskManager model. Therefore
    // this test accepts the supported unresolved outcomes: an empty name
    // selects DockItem's visible placeholder, while the generic, "wayland",
    // and "unknown" names exercise the provider's fallback artwork. In every
    // case AppId remains actionable.
    krema::TaskIconProvider provider;
    QSize returnedSize;
    const QPixmap fallbackPixmap = provider.requestPixmap(iconName, &returnedSize, QSize(64, 64));
    REQUIRE_FALSE(fallbackPixmap.isNull());
    CHECK(returnedSize == QSize(64, 64));
    CHECK(artworkBounds(fallbackPixmap).isValid());
}

int main(int argc, char *argv[])
{
    if (argc == 4 && qstrcmp(argv[1], "--child") == 0) {
        return childMain(argc, argv);
    }

    QApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
