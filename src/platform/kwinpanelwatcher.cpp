// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "kwinpanelwatcher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTemporaryFile>

#include <cmath>

Q_LOGGING_CATEGORY(lcPanelWatcher, "krema.platform.panelwatcher")

namespace krema
{

namespace
{
const QString kKWinService = QStringLiteral("org.kde.KWin");
const QString kScriptingPath = QStringLiteral("/Scripting");
const QString kScriptingInterface = QStringLiteral("org.kde.kwin.Scripting");
const QString kScriptInterface = QStringLiteral("org.kde.kwin.Script");
const QString kWatcherInterface = QStringLiteral("com.bhyoo.krema.PanelWatcher");
const QString kObjectPath = QStringLiteral("/com/bhyoo/krema/PanelWatcher");

// Reports dock-type surfaces per output. Krema's own probe surface is the
// tiny (2x1 or 1x2) dock surface of this process; the dock's other surfaces
// are skipped.
// The report is debounced so a burst of geometry changes (a panel resizing,
// KWin re-placing every layer surface) is sent once, and only when it differs.
// A panel frame is the only supported geometry exposed by KWin: Plasma may
// include transparent floating padding in it, and KWin has no supported
// painted-surface mask API.
const char kScript[] = R"JS(
var kremaPid = %1;
var lastReport = "";
var lastX = null, lastY = null;
var debounce = new QTimer();
debounce.singleShot = true;
debounce.interval = 50;

function rectOf(g) {
    return [Math.round(g.x), Math.round(g.y), Math.round(g.width), Math.round(g.height)];
}

function report() {
    var outputs = {};
    var screens = workspace.screens;
    for (var i = 0; i < screens.length; ++i) {
        outputs[screens[i].name] = { geometry: rectOf(screens[i].geometry), anchor: null, panels: [] };
    }
    var windows = workspace.windowList();
    for (var j = 0; j < windows.length; ++j) {
        var w = windows[j];
        if (!w.dock || w.deleted || w.hidden || !w.output) {
            continue;
        }
        var entry = outputs[w.output.name];
        if (!entry) {
            continue;
        }
        var frame = rectOf(w.frameGeometry);
        if (w.pid === kremaPid) {
            if (frame[2] <= 3 && frame[3] <= 3) {
                entry.anchor = frame;
            }
        } else {
            entry.panels.push(frame);
        }
    }
    var json = JSON.stringify(outputs);
    if (json === lastReport) {
        return;
    }
    lastReport = json;
    callDBus("%2", "%3", "%4", "updatePanels", json);
}

function reportCursor() {
    var p = workspace.cursorPos;
    var x = Math.round(p.x), y = Math.round(p.y);
    if (x === lastX && y === lastY) {
        return;
    }
    lastX = x;
    lastY = y;
    callDBus("%2", "%3", "%4", "notifyCursorMoved", x, y);
}

function schedule() {
    debounce.start();
}

function track(w) {
    if (w.dock) {
        w.frameGeometryChanged.connect(schedule);
        w.outputChanged.connect(schedule);
        w.hiddenChanged.connect(schedule);
    }
}

debounce.timeout.connect(report);
workspace.windowAdded.connect(function (w) {
    track(w);
    schedule();
});
workspace.windowRemoved.connect(schedule);
workspace.screensChanged.connect(schedule);
workspace.virtualScreenGeometryChanged.connect(schedule);
var existing = workspace.windowList();
for (var k = 0; k < existing.length; ++k) {
    track(existing[k]);
}
report();
workspace.cursorPosChanged.connect(reportCursor);
reportCursor();
)JS";

QDBusMessage scriptingCall(const QString &method)
{
    return QDBusMessage::createMethodCall(kKWinService, kScriptingPath, kScriptingInterface, method);
}

QRect rectFrom(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 4) {
        return {};
    }
    return QRect(std::lround(a.at(0).toDouble()), std::lround(a.at(1).toDouble()), std::lround(a.at(2).toDouble()), std::lround(a.at(3).toDouble()));
}

std::weak_ptr<KWinPanelWatcher> s_shared;
} // namespace

std::shared_ptr<KWinPanelWatcher> KWinPanelWatcher::acquire()
{
    if (auto existing = s_shared.lock()) {
        return existing;
    }
    std::shared_ptr<KWinPanelWatcher> created(new KWinPanelWatcher());
    s_shared = created;
    created->load();
    return created;
}

KWinPanelWatcher::KWinPanelWatcher()
    : m_objectPath(kObjectPath)
{
    // Reload the script when KWin (and with it every loaded script) restarts.
    m_kwinWatcher = std::make_unique<QDBusServiceWatcher>(kKWinService, QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForRegistration);
    connect(m_kwinWatcher.get(), &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        m_pluginName.clear();
        m_cursorPos.reset();
        load();
    });
}

KWinPanelWatcher::~KWinPanelWatcher()
{
    unload();
    if (m_scriptFile) {
        QDBusConnection::sessionBus().unregisterObject(m_objectPath);
    }
}

bool KWinPanelWatcher::isAvailable() const
{
    return m_available;
}

KWinPanelWatcher::OutputPanels KWinPanelWatcher::output(const QString &outputName) const
{
    return m_outputs.value(outputName);
}

std::optional<QPoint> KWinPanelWatcher::cursorPos() const
{
    return m_cursorPos;
}

bool KWinPanelWatcher::ensureScriptFile()
{
    if (m_scriptFile) {
        return true;
    }

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    if (!bus.registerObject(m_objectPath, this, QDBusConnection::ExportScriptableSlots)) {
        qCWarning(lcPanelWatcher) << "Failed to register" << m_objectPath << bus.lastError().message();
        return false;
    }

    auto file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/krema-panels-XXXXXX.js"));
    if (!file->open()) {
        qCWarning(lcPanelWatcher) << "Failed to create the KWin script file:" << file->errorString();
        bus.unregisterObject(m_objectPath);
        return false;
    }
    const QString script =
        QString::fromLatin1(kScript).arg(QString::number(QCoreApplication::applicationPid()), bus.baseService(), m_objectPath, kWatcherInterface);
    file->write(script.toUtf8());
    file->flush();
    m_scriptFile = std::move(file);
    return true;
}

void KWinPanelWatcher::load()
{
    if (!m_pluginName.isEmpty()) {
        return;
    }
    if (!ensureScriptFile()) {
        setAvailable(false);
        return;
    }

    // A fresh plugin name per load: KWin unloads scripts with deleteLater(),
    // so a previous name may still be taken.
    const quint64 generation = ++m_generation;
    m_pluginName = QStringLiteral("krema-panels-%1-%2").arg(QCoreApplication::applicationPid()).arg(generation);

    auto loadCall = scriptingCall(QStringLiteral("loadScript"));
    loadCall << m_scriptFile->fileName() << m_pluginName;
    auto *call = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(loadCall), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (generation != m_generation) {
            return; // superseded by a reload
        }
        const QDBusPendingReply<int> reply = *w;
        if (reply.isError() || reply.value() < 0) {
            qCWarning(lcPanelWatcher) << "KWin scripting unavailable; measuring the dock gap from the screen edge:"
                                      << (reply.isError() ? reply.error().message() : QStringLiteral("script refused"));
            m_pluginName.clear();
            setAvailable(false);
            return;
        }
        auto run =
            QDBusMessage::createMethodCall(kKWinService, QStringLiteral("/Scripting/Script%1").arg(reply.value()), kScriptInterface, QStringLiteral("run"));
        QDBusConnection::sessionBus().call(run, QDBus::NoBlock);
        qCDebug(lcPanelWatcher) << "Watching panels with KWin script" << reply.value();
        setAvailable(true);
    });
}

void KWinPanelWatcher::unload()
{
    if (m_pluginName.isEmpty()) {
        return;
    }
    auto unloadCall = scriptingCall(QStringLiteral("unloadScript"));
    unloadCall << m_pluginName;
    // Wait briefly so the script does not outlive Krema when it quits.
    QDBusConnection::sessionBus().call(unloadCall, QDBus::Block, 1000);
    m_pluginName.clear();
}

void KWinPanelWatcher::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged();
}

void KWinPanelWatcher::updatePanels(const QString &json)
{
    if (m_pluginName.isEmpty()) {
        return; // stale report from a script being unloaded
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcPanelWatcher) << "Malformed panel report:" << error.errorString();
        return;
    }

    QHash<QString, OutputPanels> outputs;
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        OutputPanels panels;
        panels.geometry = rectFrom(entry.value(QStringLiteral("geometry")));
        panels.anchor = rectFrom(entry.value(QStringLiteral("anchor")));
        const QJsonArray list = entry.value(QStringLiteral("panels")).toArray();
        for (const QJsonValue &v : list) {
            panels.panels.append(rectFrom(v));
        }
        outputs.insert(it.key(), panels);
    }
    m_outputs = std::move(outputs);
    qCDebug(lcPanelWatcher) << "Panel report:" << json;
    Q_EMIT panelsChanged();
}

void KWinPanelWatcher::notifyCursorMoved(int x, int y)
{
    if (m_pluginName.isEmpty()) {
        return;
    }
    const QPoint pos(x, y);
    if (m_cursorPos == pos) {
        return;
    }
    m_cursorPos = pos;
    Q_EMIT cursorMoved(pos);
}

} // namespace krema
