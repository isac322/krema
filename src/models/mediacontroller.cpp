// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "mediacontroller.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <algorithm>

Q_LOGGING_CATEGORY(lcMedia, "krema.media")

namespace krema
{

namespace
{
const QString s_mprisPrefix = QStringLiteral("org.mpris.MediaPlayer2.");
const QString s_mprisPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString s_propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString s_mprisInterface = QStringLiteral("org.mpris.MediaPlayer2");
const QString s_playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");

QString normalizedIdentifier(QString value)
{
    value = QFileInfo(value.trimmed()).fileName().toLower();
    if (value.endsWith(QStringLiteral(".desktop"))) {
        value.chop(8);
    }
    return value;
}

QString lastIdentifierComponent(const QString &value)
{
    return value.section(QLatin1Char('.'), -1);
}

bool identifiersMatch(const QString &left, const QString &right)
{
    const QString normalizedLeft = normalizedIdentifier(left);
    const QString normalizedRight = normalizedIdentifier(right);
    if (normalizedLeft.isEmpty() || normalizedRight.isEmpty()) {
        return false;
    }
    if (normalizedLeft == normalizedRight) {
        return true;
    }

    // TaskManager may return a reverse-DNS desktop id while Pulse exposes the
    // executable name (for example org.videolan.VLC versus vlc). Snap desktop
    // IDs can also be generated as snap_app.desktop (for example
    // brave_brave.desktop) while MPRIS and Pulse report only "brave".
    const QString leftLeaf = lastIdentifierComponent(normalizedLeft);
    const QString rightLeaf = lastIdentifierComponent(normalizedRight);
    if (leftLeaf == rightLeaf) {
        return true;
    }

    const auto hasUnderscoreAlias = [](const QString &longer, const QString &shorter) {
        return longer.startsWith(shorter + QLatin1Char('_')) || longer.endsWith(QLatin1Char('_') + shorter);
    };
    const auto hasDistroSuffix = [](const QString &longer, const QString &shorter) {
        return longer.startsWith(shorter + QLatin1Char('-'));
    };
    return hasUnderscoreAlias(leftLeaf, rightLeaf) || hasUnderscoreAlias(rightLeaf, leftLeaf)
        || hasDistroSuffix(leftLeaf, rightLeaf) || hasDistroSuffix(rightLeaf, leftLeaf);
}

int parentProcessId(int processId)
{
    QFile statFile(QStringLiteral("/proc/%1/stat").arg(processId));
    if (!statFile.open(QIODevice::ReadOnly)) {
        return 0;
    }

    const QByteArray stat = statFile.readAll();
    const qsizetype commandEnd = stat.lastIndexOf(')');
    if (commandEnd < 0 || commandEnd + 2 >= stat.size()) {
        return 0;
    }

    // /proc/<pid>/stat starts with "pid (comm) state ppid ...". comm can
    // contain spaces, so split only after the closing parenthesis.
    const QList<QByteArray> fields = stat.mid(commandEnd + 2).simplified().split(' ');
    bool ok = false;
    const int parentId = fields.value(1).toInt(&ok);
    return ok ? parentId : 0;
}

bool processHasAncestor(int processId, int ancestorProcessId)
{
    int currentProcessId = processId;
    for (int depth = 0; currentProcessId > 1 && depth < 16; ++depth) {
        if (currentProcessId == ancestorProcessId) {
            return true;
        }
        currentProcessId = parentProcessId(currentProcessId);
    }
    return false;
}

QVariant unwrapDbusVariant(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant();
    }
    return value;
}

QStringList stringListValue(const QVariant &value)
{
    const QVariant unwrapped = unwrapDbusVariant(value);
    if (unwrapped.metaType() == QMetaType::fromType<QStringList>()) {
        return unwrapped.toStringList();
    }
    if (unwrapped.canConvert<QString>()) {
        const QString stringValue = unwrapped.toString();
        return stringValue.isEmpty() ? QStringList{} : QStringList{stringValue};
    }
    return {};
}

} // namespace

MediaController::MediaController(QObject *parent)
    : QObject(parent)
    , m_audioSubscription(this)
{
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    const bool watchingServiceChanges = sessionBus.connect(QStringLiteral("org.freedesktop.DBus"),
                                                           QStringLiteral("/org/freedesktop/DBus"),
                                                           QStringLiteral("org.freedesktop.DBus"),
                                                           QStringLiteral("NameOwnerChanged"),
                                                           this,
                                                           SLOT(onMprisNameOwnerChanged(QString,QString,QString)));
    if (!watchingServiceChanges) {
        qCWarning(lcMedia) << "Unable to watch session D-Bus service changes";
    }

    const auto busInterface = sessionBus.interface();
    if (busInterface) {
        const QDBusReply<QStringList> services = busInterface->registeredServiceNames();
        if (services.isValid()) {
            for (const QString &service : services.value()) {
                if (service.startsWith(s_mprisPrefix)) {
                    watchMprisService(service);
                }
            }
        }
    }

    startAudioMonitor();
}

MediaController::~MediaController()
{
    if (m_audioSubscription.state() != QProcess::NotRunning) {
        m_audioSubscription.terminate();
        if (!m_audioSubscription.waitForFinished(200)) {
            m_audioSubscription.kill();
        }
    }
}

int MediaController::revision() const
{
    return m_revision;
}

QVariantMap MediaController::playerForApp(const QString &appId, int processId) const
{
    const QString taskId = normalizedIdentifier(appId);
    if (taskId.isEmpty()) {
        return {};
    }

    const Player *bestPlayer = nullptr;
    QString bestService;
    int bestMatchScore = 0;
    int bestPlaybackScore = -1;
    for (auto it = m_players.cbegin(); it != m_players.cend(); ++it) {
        const Player &player = it.value();
        const QString desktopEntry = normalizedIdentifier(player.desktopEntry);
        const QString serviceEntry = normalizedIdentifier(it.key().mid(s_mprisPrefix.size()));
        // MPRIS instance names commonly look like "vlc.instance1234". The
        // first component is the application identifier; the final component
        // is only an instance token.
        const QString serviceApplication = serviceEntry.section(QLatin1Char('.'), 0, 0);
        const QString identity = normalizedIdentifier(player.identity);
        const bool processMatches = processId > 1 && player.processId == static_cast<uint>(processId);

        int matchScore = 0;
        if (desktopEntry == taskId) {
            matchScore = 3;
        } else if (identifiersMatch(desktopEntry, taskId)) {
            matchScore = 2;
        } else if (processMatches) {
            matchScore = 2;
        } else if (serviceEntry == taskId || identifiersMatch(serviceApplication, taskId)
                   || identifiersMatch(serviceEntry, taskId)) {
            matchScore = 1;
        } else if (identifiersMatch(identity, taskId)) {
            matchScore = 1;
        }
        if (matchScore == 0) {
            continue;
        }

        const int playbackScore = player.playbackStatus == QLatin1String("Playing") ? 2
            : (player.playbackStatus == QLatin1String("Paused") ? 1 : 0);
        if (matchScore > bestMatchScore || (matchScore == bestMatchScore && playbackScore > bestPlaybackScore)) {
            bestPlayer = &player;
            bestService = it.key();
            bestMatchScore = matchScore;
            bestPlaybackScore = playbackScore;
        }
    }

    if (!bestPlayer) {
        return {};
    }

    // Latte's tooltip exposes a controllable MPRIS player even while it is
    // stopped, so the user can start it from the dock preview.
    const bool available = bestPlayer->canControl;
    return {
        {QStringLiteral("service"), bestPlayer->canControl ? bestService : QString{}},
        {QStringLiteral("identity"), bestPlayer->identity},
        {QStringLiteral("title"), bestPlayer->title},
        {QStringLiteral("artist"), bestPlayer->artist},
        {QStringLiteral("playbackStatus"), bestPlayer->playbackStatus},
        {QStringLiteral("available"), available},
        {QStringLiteral("canControl"), bestPlayer->canControl},
        {QStringLiteral("canPlay"), bestPlayer->canPlay},
        {QStringLiteral("canPause"), bestPlayer->canPause},
        {QStringLiteral("canSeek"), bestPlayer->canSeek},
        {QStringLiteral("canGoPrevious"), bestPlayer->canGoPrevious},
        {QStringLiteral("canGoNext"), bestPlayer->canGoNext},
    };
}

void MediaController::playPause(const QString &service)
{
    const auto it = m_players.constFind(service);
    if (it != m_players.cend() && it->canControl && (it->canPlay || it->canPause)) {
        callPlayerMethod(service, QStringLiteral("PlayPause"));
    }
}

void MediaController::seekBackward(const QString &service)
{
    seek(service, -10'000'000);
}

void MediaController::seekForward(const QString &service)
{
    seek(service, 10'000'000);
}

void MediaController::previous(const QString &service)
{
    const auto it = m_players.constFind(service);
    if (it != m_players.cend() && it->canControl && it->canGoPrevious) {
        callPlayerMethod(service, QStringLiteral("Previous"));
    }
}

void MediaController::next(const QString &service)
{
    const auto it = m_players.constFind(service);
    if (it != m_players.cend() && it->canControl && it->canGoNext) {
        callPlayerMethod(service, QStringLiteral("Next"));
    }
}

QVariantMap MediaController::audioState(const QString &appId, int processId) const
{
    const QVector<AudioStream> streams = matchingAudioStreams(appId, processId);
    const bool hasAudio = !streams.isEmpty();
    const bool muted = hasAudio && std::all_of(streams.cbegin(), streams.cend(), [](const AudioStream &stream) {
        return stream.muted;
    });
    return {
        {QStringLiteral("hasAudio"), hasAudio},
        {QStringLiteral("muted"), muted},
        {QStringLiteral("streamCount"), streams.size()},
    };
}

void MediaController::toggleAudioMute(const QString &appId, int processId)
{
    const QVector<AudioStream> streams = matchingAudioStreams(appId, processId);
    if (streams.isEmpty() || m_pactlPath.isEmpty()) {
        return;
    }

    const bool shouldMute = std::any_of(streams.cbegin(), streams.cend(), [](const AudioStream &stream) {
        return !stream.muted;
    });
    for (const AudioStream &stream : streams) {
        auto *process = new QProcess(this);
        connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
            process->deleteLater();
            refreshAudioStreams();
        });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                process->deleteLater();
                refreshAudioStreams();
            }
        });
        process->start(m_pactlPath,
                       {QStringLiteral("set-sink-input-mute"),
                        QString::number(stream.index),
                        shouldMute ? QStringLiteral("1") : QStringLiteral("0")});
    }
}

void MediaController::onMprisNameOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner)
{
    if (!service.startsWith(s_mprisPrefix) || oldOwner == newOwner) {
        return;
    }

    if (!oldOwner.isEmpty()) {
        onMprisServiceUnregistered(service);
    }
    if (!newOwner.isEmpty()) {
        watchMprisService(service);
    }
}

void MediaController::onMprisServiceUnregistered(const QString &service)
{
    auto it = m_players.find(service);
    if (it == m_players.end()) {
        return;
    }

    if (!it->owner.isEmpty()) {
        m_ownerToService.remove(it->owner);
    }
    QDBusConnection::sessionBus().disconnect(service,
                                             s_mprisPath,
                                             s_propertiesInterface,
                                             QStringLiteral("PropertiesChanged"),
                                             this,
                                             SLOT(onMprisPropertiesChanged(QString,
                                                                            QVariantMap,
                                                                            QStringList,
                                                                            QDBusMessage)));
    m_players.erase(it);
    bumpRevision();
}

void MediaController::onMprisPropertiesChanged(const QString &interfaceName,
                                               const QVariantMap &changed,
                                               const QStringList &invalidated,
                                               const QDBusMessage &message)
{
    if (interfaceName != s_mprisInterface && interfaceName != s_playerInterface) {
        return;
    }

    QString service = message.service();
    if (!m_players.contains(service)) {
        service = m_ownerToService.value(service);
    }
    const auto player = m_players.constFind(service);
    if (player == m_players.cend()) {
        return;
    }

    applyPlayerProperties(service, interfaceName, changed);
    if (!invalidated.isEmpty()) {
        queryPlayerProperties(service, interfaceName, player->generation);
    }
}

void MediaController::watchMprisService(const QString &service)
{
    if (!service.startsWith(s_mprisPrefix) || m_players.contains(service)) {
        return;
    }

    Player player;
    player.generation = ++m_playerGeneration;
    if (const auto busInterface = QDBusConnection::sessionBus().interface()) {
        const QDBusReply<QString> ownerReply = busInterface->serviceOwner(service);
        if (ownerReply.isValid()) {
            player.owner = ownerReply.value();
            m_ownerToService.insert(player.owner, service);
        }
    }

    const quint64 generation = player.generation;
    m_players.insert(service, player);
    QDBusConnection::sessionBus().connect(service,
                                          s_mprisPath,
                                          s_propertiesInterface,
                                          QStringLiteral("PropertiesChanged"),
                                          this,
                                          SLOT(onMprisPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    queryPlayerProperties(service, s_mprisInterface, generation);
    queryPlayerProperties(service, s_playerInterface, generation);
    queryPlayerProcessId(service, generation);
    bumpRevision();
}

void MediaController::queryPlayerProcessId(const QString &service, quint64 generation)
{
    const auto player = m_players.constFind(service);
    if (player == m_players.cend() || player->owner.isEmpty()) {
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                          QStringLiteral("/org/freedesktop/DBus"),
                                                          QStringLiteral("org.freedesktop.DBus"),
                                                          QStringLiteral("GetConnectionUnixProcessID"));
    message << player->owner;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, service, generation] {
        watcher->deleteLater();
        const auto player = m_players.find(service);
        if (player == m_players.end() || player->generation != generation) {
            return;
        }

        const QDBusPendingReply<uint> reply = *watcher;
        if (reply.isError()) {
            qCDebug(lcMedia) << "MPRIS owner PID query failed for" << service << reply.error().message();
            return;
        }

        if (player->processId != reply.value()) {
            player->processId = reply.value();
            bumpRevision();
        }
    });
}

void MediaController::queryPlayerProperties(const QString &service, const QString &interfaceName, quint64 generation)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service,
                                                          s_mprisPath,
                                                          s_propertiesInterface,
                                                          QStringLiteral("GetAll"));
    message << interfaceName;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, service, interfaceName, generation] {
        watcher->deleteLater();
        const auto player = m_players.constFind(service);
        if (player == m_players.cend() || player->generation != generation) {
            return;
        }

        const QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError()) {
            qCDebug(lcMedia) << "MPRIS property query failed for" << service << interfaceName
                             << reply.error().message();
            return;
        }
        applyPlayerProperties(service, interfaceName, reply.value());
    });
}

void MediaController::applyPlayerProperties(const QString &service,
                                            const QString &interfaceName,
                                            const QVariantMap &properties)
{
    auto it = m_players.find(service);
    if (it == m_players.end()) {
        return;
    }

    bool changed = false;
    auto updateString = [&changed](QString &target, const QVariant &value) {
        const QString nextValue = unwrapDbusVariant(value).toString();
        if (target != nextValue) {
            target = nextValue;
            changed = true;
        }
    };
    auto updateBool = [&changed](bool &target, const QVariant &value) {
        const bool nextValue = unwrapDbusVariant(value).toBool();
        if (target != nextValue) {
            target = nextValue;
            changed = true;
        }
    };

    if (interfaceName == s_mprisInterface) {
        if (properties.contains(QStringLiteral("DesktopEntry"))) {
            updateString(it->desktopEntry, properties.value(QStringLiteral("DesktopEntry")));
        }
        if (properties.contains(QStringLiteral("Identity"))) {
            updateString(it->identity, properties.value(QStringLiteral("Identity")));
        }
    } else if (interfaceName == s_playerInterface) {
        if (properties.contains(QStringLiteral("PlaybackStatus"))) {
            updateString(it->playbackStatus, properties.value(QStringLiteral("PlaybackStatus")));
        }
        if (properties.contains(QStringLiteral("CanControl"))) {
            updateBool(it->canControl, properties.value(QStringLiteral("CanControl")));
        }
        if (properties.contains(QStringLiteral("CanPlay"))) {
            updateBool(it->canPlay, properties.value(QStringLiteral("CanPlay")));
        }
        if (properties.contains(QStringLiteral("CanPause"))) {
            updateBool(it->canPause, properties.value(QStringLiteral("CanPause")));
        }
        if (properties.contains(QStringLiteral("CanSeek"))) {
            updateBool(it->canSeek, properties.value(QStringLiteral("CanSeek")));
        }
        if (properties.contains(QStringLiteral("CanGoPrevious"))) {
            updateBool(it->canGoPrevious, properties.value(QStringLiteral("CanGoPrevious")));
        }
        if (properties.contains(QStringLiteral("CanGoNext"))) {
            updateBool(it->canGoNext, properties.value(QStringLiteral("CanGoNext")));
        }
        if (properties.contains(QStringLiteral("Metadata"))) {
            const QVariant metadataValue = unwrapDbusVariant(properties.value(QStringLiteral("Metadata")));
            const QVariantMap metadata = metadataValue.toMap();
            if (metadataValue.metaType() == QMetaType::fromType<QVariantMap>()) {
                const QString nextTitle = unwrapDbusVariant(metadata.value(QStringLiteral("xesam:title"))).toString();
                const QString nextArtist = stringListValue(metadata.value(QStringLiteral("xesam:artist")))
                                               .join(QStringLiteral(", "));
                const QVariant trackValue = unwrapDbusVariant(metadata.value(QStringLiteral("mpris:trackid")));
                const QString nextTrackId = trackValue.toString();
                if (it->title != nextTitle || it->artist != nextArtist || it->trackId != nextTrackId) {
                    it->title = nextTitle;
                    it->artist = nextArtist;
                    it->trackId = nextTrackId;
                    changed = true;
                }
            }
        }
    }

    if (changed) {
        bumpRevision();
    }
}

void MediaController::callPlayerMethod(const QString &service, const QString &method)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, s_mprisPath, s_playerInterface, method);
    QDBusConnection::sessionBus().asyncCall(message);
}

void MediaController::seek(const QString &service, qlonglong offsetMicroseconds)
{
    const auto it = m_players.constFind(service);
    if (it == m_players.cend() || !it->canControl || !it->canSeek) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(service,
                                                          s_mprisPath,
                                                          s_playerInterface,
                                                          QStringLiteral("Seek"));
    message << offsetMicroseconds;
    QDBusConnection::sessionBus().asyncCall(message);
}

void MediaController::startAudioMonitor()
{
    m_pactlPath = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (m_pactlPath.isEmpty()) {
        qCDebug(lcMedia) << "pactl is unavailable; per-application mute controls are disabled";
        return;
    }

    connect(&m_audioSubscription,
            &QProcess::readyReadStandardOutput,
            this,
            &MediaController::handleAudioSubscriptionOutput);
    connect(&m_audioSubscription, &QProcess::finished, this, [this] {
        handleAudioSubscriptionOutput();
        if (!m_audioStreams.isEmpty()) {
            m_audioStreams.clear();
            bumpRevision();
        }
    });
    m_audioSubscription.start(m_pactlPath, {QStringLiteral("subscribe")});
    refreshAudioStreams();
}

void MediaController::refreshAudioStreams()
{
    if (m_pactlPath.isEmpty()) {
        return;
    }
    if (m_audioQuery && m_audioQuery->state() != QProcess::NotRunning) {
        m_audioRefreshPending = true;
        return;
    }

    auto *query = new QProcess(this);
    m_audioQuery = query;
    connect(query, &QProcess::finished, this, [this, query](int exitCode, QProcess::ExitStatus exitStatus) {
        const QByteArray output = query->readAllStandardOutput();
        if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
            parseAudioStreams(output);
        }

        if (m_audioQuery == query) {
            m_audioQuery = nullptr;
        }
        query->deleteLater();

        if (m_audioRefreshPending) {
            m_audioRefreshPending = false;
            refreshAudioStreams();
        }
    });
    connect(query, &QProcess::errorOccurred, this, [this, query](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        if (m_audioQuery == query) {
            m_audioQuery = nullptr;
        }
        query->deleteLater();
    });
    query->start(m_pactlPath, {QStringLiteral("--format=json"), QStringLiteral("list"), QStringLiteral("sink-inputs")});
}

void MediaController::parseAudioStreams(const QByteArray &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        qCDebug(lcMedia) << "Unable to read pactl sink-input response:" << parseError.errorString();
        return;
    }

    QVector<AudioStream> streams;
    const QJsonArray inputs = document.array();
    streams.reserve(inputs.size());
    for (const QJsonValue &value : inputs) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QJsonObject properties = object.value(QStringLiteral("properties")).toObject();
        AudioStream stream;
        stream.index = object.value(QStringLiteral("index")).toInt(-1);
        stream.processId = properties.value(QStringLiteral("application.process.id")).toString().toInt();
        if (stream.processId <= 0) {
            stream.processId = properties.value(QStringLiteral("application.process.id")).toInt();
        }
        stream.applicationId = properties.value(QStringLiteral("application.id")).toString();
        stream.processBinary = QFileInfo(properties.value(QStringLiteral("application.process.binary")).toString())
                                   .fileName();
        stream.applicationName = properties.value(QStringLiteral("application.name")).toString();
        stream.muted = object.value(QStringLiteral("mute")).toBool();
        stream.corked = object.value(QStringLiteral("corked")).toBool();
        if (stream.index >= 0) {
            streams.append(std::move(stream));
        }
    }

    std::sort(streams.begin(), streams.end(), [](const AudioStream &left, const AudioStream &right) {
        return left.index < right.index;
    });

    if (!sameAudioStreams(m_audioStreams, streams)) {
        m_audioStreams = std::move(streams);
        bumpRevision();
    }
}

void MediaController::handleAudioSubscriptionOutput()
{
    m_audioSubscriptionBuffer.append(m_audioSubscription.readAllStandardOutput());
    qsizetype lineEnd = -1;
    while ((lineEnd = m_audioSubscriptionBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_audioSubscriptionBuffer.left(lineEnd).trimmed();
        m_audioSubscriptionBuffer.remove(0, lineEnd + 1);
        if (line.contains("sink-input")) {
            refreshAudioStreams();
        }
    }
}

QVector<MediaController::AudioStream> MediaController::matchingAudioStreams(const QString &appId, int processId) const
{
    QVector<AudioStream> matches;
    const QString taskId = normalizedIdentifier(appId);
    for (const AudioStream &stream : m_audioStreams) {
        const bool idMatches = identifiersMatch(taskId, stream.applicationId)
            || identifiersMatch(taskId, stream.processBinary);
        const bool nameMatches = identifiersMatch(taskId, stream.applicationName);
        // Match stream subprocesses to their owning task, as Latte does. The
        // bounded ancestry walk avoids relying on a browser's changing child PID.
        const bool processMatches = processId > 1
            && (processId == stream.processId || processHasAncestor(stream.processId, processId));
        // TaskManager's AppPid can be KWin's PID on some Wayland setups. Use a
        // non-exact display-name match only when that PID confirms the task.
        const bool processConfirms = processId > 1 && processId == stream.processId;
        const bool attributable = idMatches || processMatches || (nameMatches && (processId <= 1 || processConfirms));
        if (attributable && (!stream.corked || stream.muted)) {
            matches.append(stream);
        }
    }
    return matches;
}

bool MediaController::sameAudioStreams(const QVector<AudioStream> &left, const QVector<AudioStream> &right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (qsizetype i = 0; i < left.size(); ++i) {
        const AudioStream &a = left.at(i);
        const AudioStream &b = right.at(i);
        if (a.index != b.index || a.processId != b.processId || a.applicationId != b.applicationId
            || a.processBinary != b.processBinary || a.applicationName != b.applicationName
            || a.muted != b.muted || a.corked != b.corked) {
            return false;
        }
    }
    return true;
}

void MediaController::bumpRevision()
{
    ++m_revision;
    Q_EMIT revisionChanged();
}

} // namespace krema
