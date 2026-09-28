// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Qt Quick Test runner for Krema's QML components.
//
// The real QML files under src/qml are loaded unchanged. Their C++ backends
// are replaced by test doubles:
//  - mocks/com/bhyoo/krema/*.qml  — QML singletons for DockModel, DockSettings,
//    DockView, DockVisibility, DockActions, DockContextMenu, PreviewController,
//    NotificationTracker (production registers these as singletons/context
//    properties from C++).
//  - qmltestmocks.cpp            — org.kde.taskmanager / org.kde.pipewire types.
// i18n() is provided by the real KLocalizedContext, exactly as in DockView.

#include "qmltestmocks.h"

#include <KLocalizedQmlContext>
#include <QQmlEngine>
#include <QtQuickTest>

class KremaQmlTestSetup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void applicationAvailable()
    {
        KremaQmlTest::registerMockTypes();
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        // Prepend so the mock modules shadow any installed real plugins.
        QStringList paths = engine->importPathList();
        paths.prepend(QStringLiteral(KREMA_QML_MOCKS_DIR));
        engine->setImportPathList(paths);

        KLocalization::setupLocalizedContext(engine);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(krema_qml, KremaQmlTestSetup)

#include "main.moc"
