// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "app/application.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QStringList>

int main(int argc, char *argv[])
{
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("krema"));

    KAboutData aboutData = krema::Application::makeAboutData();
    // QCommandLineParser builds --version's output and its error prefix from
    // these; set them directly because no QApplication exists yet
    // (Application::run() installs the KAboutData itself).
    QCoreApplication::setApplicationName(aboutData.componentName());
    QCoreApplication::setApplicationVersion(aboutData.version());
    QCommandLineParser parser;
    // setupCommandLine adds --help, --author, --license and --desktopfile, but
    // the version option only once an instance exists.
    const QCommandLineOption versionOption = parser.addVersionOption();
    aboutData.setupCommandLine(&parser);

    // Pass 1, before QApplication: its constructor aborts without a display,
    // yet --help/--version/--author/--license must work headless (package
    // smoke checks) and before KDBusService hands the call to a running
    // instance. Tolerant: Qt's own options are still in argv here, and are
    // read as words so that e.g. -reverse is not taken for -v.
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    (void)parser.parse(arguments); // unknown options are reported by pass 2
    if (parser.isSet(QStringLiteral("help"))) {
        parser.showHelp();
    }
    if (parser.isSet(versionOption)) {
        parser.showVersion();
    }
    // --author/--license print and exit. Only these: processCommandLine also
    // applies --desktopfile through setApplicationData, which needs the
    // instance and is left to pass 2.
    if (parser.isSet(QStringLiteral("author")) || parser.isSet(QStringLiteral("license"))) {
        aboutData.processCommandLine(&parser);
    }

    krema::Application app(argc, argv);
    // Pass 2: QApplication has removed its own options; anything left that
    // the parser does not know is an error.
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsCompactedShortOptions);
    parser.process(app);
    aboutData.processCommandLine(&parser); // applies --desktopfile to aboutData
    return app.run(aboutData);
}
