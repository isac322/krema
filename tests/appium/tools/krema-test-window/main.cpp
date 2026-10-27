// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors
//
// Deterministic test window for Krema E2E tests.
//
// One process = one toplevel window. The window's Wayland app_id comes from
// --app-id (default org.kde.krema.testwindow, which has an installed .desktop
// file) and its title from --title, so a test can open N windows of a known
// app, find them in KWin's window list and close them again with SIGTERM.
//
// --badge N sends a Unity LauncherEntry update so badge scenarios have a
// source that is not a real notification daemon.
//
// --color C fills the window content with a solid color, so a screenshot can
// tell which window's live content a preview thumbnail shows.

#include <QApplication>
#include <QCommandLineParser>
#include <QColor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QSocketNotifier>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <csignal>
#include <cstdio>
#include <optional>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
int s_sigFd[2] = {-1, -1};

void onSignal(int)
{
    const char c = 1;
    [[maybe_unused]] const auto n = ::write(s_sigFd[0], &c, 1);
}

void sendBadge(const QString &appId, int count)
{
    QDBusMessage msg = QDBusMessage::createSignal(QStringLiteral("/org/kde/krema/testwindow"),
                                                  QStringLiteral("com.canonical.Unity.LauncherEntry"),
                                                  QStringLiteral("Update"));
    QVariantMap props;
    props.insert(QStringLiteral("count"), qint64(count));
    props.insert(QStringLiteral("count-visible"), count > 0);
    msg << QStringLiteral("application://%1.desktop").arg(appId) << props;
    QDBusConnection::sessionBus().send(msg);
}

// Logs every key press the window receives ("key-press <title> key=0x.. mods=0x..")
// to stderr, which TestWindows sends to artifacts/test-windows.log. Lets tests
// tell "KWin swallowed the combo as a global shortcut" from "it reached the app".
class KeyLogger : public QObject
{
public:
    explicit KeyLogger(QString title)
        : m_title(std::move(title))
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress && watched->isWindowType()) {
            const auto *ke = static_cast<QKeyEvent *>(event);
            fprintf(stderr, "key-press %s key=0x%x mods=0x%x\n", qPrintable(m_title), ke->key(), uint(ke->modifiers()));
            fflush(stderr);
        }
        return false;
    }

private:
    QString m_title;
};
class DragSourceWindow : public QWidget
{
public:
    DragSourceWindow(QStringList paths, QString eventsPath)
        : m_paths(std::move(paths))
        , m_eventsPath(std::move(eventsPath))
    {
        setWindowTitle(QStringLiteral("PR137 external MIME source"));
        resize(240, 120);
    }

    void record(const QJsonObject &fields) const
    {
        QFile file(m_eventsPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            return;
        }
        QJsonObject event(fields);
        event.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
        const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
        file.write(line);
        file.flush();
        if (file.handle() >= 0) {
            ::fsync(file.handle());
        }
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressPosition = event->position().toPoint();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!event->buttons().testFlag(Qt::LeftButton) || !m_pressPosition.has_value()
            || (event->position().toPoint() - *m_pressPosition).manhattanLength() < QApplication::startDragDistance()) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        m_pressPosition.reset();

        auto *mime = new QMimeData;
        QJsonArray urls;
        QList<QUrl> localUrls;
        localUrls.reserve(m_paths.size());
        for (const QString &path : m_paths) {
            const QUrl url = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
            localUrls.append(url);
            urls.append(url.toString());
        }
        mime->setUrls(localUrls);
        QJsonArray formats;
        for (const QString &format : mime->formats()) {
            formats.append(format);
        }
        record(QJsonObject{{QStringLiteral("event"), QStringLiteral("drag-start")},
                           {QStringLiteral("formats"), formats},
                           {QStringLiteral("urls"), urls}});

        QDrag drag(this);
        drag.setMimeData(mime);
        const Qt::DropAction action = drag.exec(Qt::CopyAction);
        record(QJsonObject{{QStringLiteral("event"), QStringLiteral("drag-finished")},
                           {QStringLiteral("action"), int(action)}});
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressPosition.reset();
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QStringList m_paths;
    QString m_eventsPath;
    std::optional<QPoint> m_pressPosition;
};

} // namespace

int main(int argc, char **argv)
{
    // app_id must be set before the QApplication creates its platform
    // integration, otherwise the first surface is committed with the binary
    // name as app_id.
    QString appId = QStringLiteral("org.kde.krema.testwindow");
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "--app-id") == 0) {
            appId = QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    QGuiApplication::setDesktopFileName(appId);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("krema-test-window"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("app-id"), QStringLiteral("Wayland app_id / desktop file id"), QStringLiteral("id"), appId});
    parser.addOption({QStringLiteral("title"), QStringLiteral("Window title"), QStringLiteral("title"), QStringLiteral("Krema Test Window")});
    parser.addOption({QStringLiteral("width"), QStringLiteral("Window width"), QStringLiteral("px"), QStringLiteral("400")});
    parser.addOption({QStringLiteral("height"), QStringLiteral("Window height"), QStringLiteral("px"), QStringLiteral("300")});
    parser.addOption({QStringLiteral("badge"), QStringLiteral("Unity LauncherEntry badge count"), QStringLiteral("n"), QStringLiteral("-1")});
    parser.addOption({QStringLiteral("color"), QStringLiteral("Solid background color of the window content (e.g. red, #00ff00)"), QStringLiteral("color")});
    parser.addOption({QStringLiteral("icon-path"), QStringLiteral("Explicit local test icon path"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("drag-source"), QStringLiteral("Run as an external MIME drag source")});
    parser.addOption({QStringLiteral("events"), QStringLiteral("JSONL event log path"), QStringLiteral("path")});
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Local files to include in the drag"));
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);
    parser.process(app);

    ::socketpair(AF_UNIX, SOCK_STREAM, 0, s_sigFd);
    QSocketNotifier sigNotifier(s_sigFd[1], QSocketNotifier::Read);
    QObject::connect(&sigNotifier, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    struct sigaction sa = {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    if (parser.isSet(QStringLiteral("drag-source"))) {
        const QString eventsPath = parser.value(QStringLiteral("events"));
        const QStringList paths = parser.positionalArguments();
        if (eventsPath.isEmpty() || paths.isEmpty()) {
            fprintf(stderr, "--drag-source requires --events and at least one path\n");
            return 2;
        }
        DragSourceWindow window(paths, eventsPath);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &window, [&window] {
            window.close();
        });
        window.show();
        window.record(QJsonObject{{QStringLiteral("event"), QStringLiteral("source-started")}});
        return app.exec();
    }


    const QString title = parser.value(QStringLiteral("title"));
    KeyLogger keyLogger(title);
    app.installEventFilter(&keyLogger);

    QMainWindow window;
    window.setWindowTitle(title);
    auto *central = new QWidget(&window);
    if (const QColor color(parser.value(QStringLiteral("color"))); color.isValid()) {
        QPalette pal = central->palette();
        pal.setColor(QPalette::Window, color);
        central->setPalette(pal);
        central->setAutoFillBackground(true);
    }
    auto *layout = new QVBoxLayout(central);
    auto *label = new QLabel(title, central);
    label->setObjectName(QStringLiteral("titleLabel"));
    layout->addWidget(label);
    auto *quit = new QPushButton(QStringLiteral("Quit"), central);
    QObject::connect(quit, &QPushButton::clicked, &app, &QCoreApplication::quit);
    layout->addWidget(quit);
    window.setCentralWidget(central);
    window.resize(parser.value(QStringLiteral("width")).toInt(), parser.value(QStringLiteral("height")).toInt());
    if (const QString iconPath = parser.value(QStringLiteral("icon-path")); !iconPath.isEmpty()) {
        // This is a raw-render test stimulus, not a claim about Wayland app_id semantics.
        window.setWindowIcon(QIcon(iconPath));
    }
    window.show();

    if (const int badge = parser.value(QStringLiteral("badge")).toInt(); badge >= 0) {
        sendBadge(appId, badge);
    }

    return app.exec();
}
