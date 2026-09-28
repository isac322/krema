// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "kwinpointermotionwatcher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QLoggingCategory>
#include <QTemporaryFile>

Q_LOGGING_CATEGORY(lcPointerMotion, "krema.platform.pointermotion")

namespace krema
{

namespace
{
const QString kKWinService = QStringLiteral("org.kde.KWin");
const QString kScriptingPath = QStringLiteral("/Scripting");
const QString kScriptingInterface = QStringLiteral("org.kde.kwin.Scripting");
const QString kScriptInterface = QStringLiteral("org.kde.kwin.Script");
const QString kWatcherInterface = QStringLiteral("com.bhyoo.krema.PointerMotionWatcher");

int s_instanceCount = 0;

QDBusMessage scriptingCall(const QString &method)
{
    return QDBusMessage::createMethodCall(kKWinService, kScriptingPath, kScriptingInterface, method);
}
} // namespace

KWinPointerMotionWatcher::KWinPointerMotionWatcher(QObject *parent)
    : QObject(parent)
    , m_instance(s_instanceCount++)
    , m_objectPath(QStringLiteral("/com/bhyoo/krema/PointerMotionWatcher/%1").arg(m_instance))
{
}

KWinPointerMotionWatcher::~KWinPointerMotionWatcher()
{
    disarm();
    if (m_scriptFile) {
        QDBusConnection::sessionBus().unregisterObject(m_objectPath);
    }
}

bool KWinPointerMotionWatcher::ensureScriptFile()
{
    if (m_scriptFile) {
        return true;
    }

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    if (!bus.registerObject(m_objectPath, this, QDBusConnection::ExportScriptableSlots)) {
        qCWarning(lcPointerMotion) << "Failed to register" << m_objectPath << bus.lastError().message();
        return false;
    }

    // KWin reads the script from a file. It reports the first cursor motion
    // back to this object and then stays silent until it is unloaded.
    auto file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/krema-pointer-motion-XXXXXX.js"));
    if (!file->open()) {
        qCWarning(lcPointerMotion) << "Failed to create the KWin script file:" << file->errorString();
        bus.unregisterObject(m_objectPath);
        return false;
    }
    const QString script = QStringLiteral(
                               "function kremaPointerMoved() {\n"
                               "    workspace.cursorPosChanged.disconnect(kremaPointerMoved);\n"
                               "    callDBus(\"%1\", \"%2\", \"%3\", \"notifyPointerMoved\");\n"
                               "}\n"
                               "workspace.cursorPosChanged.connect(kremaPointerMoved);\n")
                               .arg(bus.baseService(), m_objectPath, kWatcherInterface);
    file->write(script.toUtf8());
    file->flush();
    m_scriptFile = std::move(file);
    return true;
}

void KWinPointerMotionWatcher::arm()
{
    if (!m_pluginName.isEmpty() || !ensureScriptFile()) {
        return;
    }

    // A fresh plugin name per arm: KWin unloads scripts with deleteLater(), so
    // the previous name may still be taken right after disarm().
    const quint64 generation = ++m_generation;
    m_pluginName = QStringLiteral("krema-pointer-motion-%1-%2-%3").arg(QCoreApplication::applicationPid()).arg(m_instance).arg(generation);

    auto load = scriptingCall(QStringLiteral("loadScript"));
    load << m_scriptFile->fileName() << m_pluginName;
    auto *call = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(load), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<int> reply = *w;
        if (reply.isError()) {
            qCDebug(lcPointerMotion) << "KWin scripting unavailable:" << reply.error().message();
            return;
        }
        // Disarmed (and the script unloaded) while loading.
        if (generation != m_generation || m_pluginName.isEmpty()) {
            return;
        }
        const int id = reply.value();
        if (id < 0) {
            qCWarning(lcPointerMotion) << "KWin refused to load" << m_pluginName;
            return;
        }
        auto run = QDBusMessage::createMethodCall(kKWinService, QStringLiteral("/Scripting/Script%1").arg(id), kScriptInterface, QStringLiteral("run"));
        QDBusConnection::sessionBus().call(run, QDBus::NoBlock);
        qCDebug(lcPointerMotion) << "Watching pointer motion with KWin script" << id;
    });
}

void KWinPointerMotionWatcher::disarm()
{
    if (m_pluginName.isEmpty()) {
        return;
    }
    auto unload = scriptingCall(QStringLiteral("unloadScript"));
    unload << m_pluginName;
    QDBusConnection::sessionBus().call(unload, QDBus::NoBlock);
    m_pluginName.clear();
}

void KWinPointerMotionWatcher::notifyPointerMoved()
{
    if (m_pluginName.isEmpty()) {
        return; // stale report from a script being unloaded
    }
    qCDebug(lcPointerMotion) << "Pointer moved";
    disarm();
    Q_EMIT pointerMoved();
}

} // namespace krema
