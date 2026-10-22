// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QVariantMap>
#include <QVector>

namespace krema
{

/**
 * Exposes MPRIS transport controls and per-application PulseAudio mute state
 * to the dock and window preview.
 */
class MediaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit MediaController(QObject *parent = nullptr);
    ~MediaController() override;

    [[nodiscard]] int revision() const;

    /// Return the active MPRIS player matching a task's desktop identity or process.
    [[nodiscard]] Q_INVOKABLE QVariantMap playerForApp(const QString &appId, int processId) const;
    Q_INVOKABLE void playPause(const QString &service);
    Q_INVOKABLE void seekBackward(const QString &service);
    Q_INVOKABLE void seekForward(const QString &service);
    Q_INVOKABLE void previous(const QString &service);
    Q_INVOKABLE void next(const QString &service);

    /// Return {hasAudio, muted, streamCount} for the app identity/process.
    [[nodiscard]] Q_INVOKABLE QVariantMap audioState(const QString &appId, int processId) const;
    Q_INVOKABLE void toggleAudioMute(const QString &appId, int processId);

Q_SIGNALS:
    void revisionChanged();

private Q_SLOTS:
    void onMprisNameOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void onMprisServiceUnregistered(const QString &service);
    void onMprisPropertiesChanged(const QString &interfaceName,
                                  const QVariantMap &changed,
                                  const QStringList &invalidated,
                                  const QDBusMessage &message);

private:
    struct Player {
        QString owner;
        QString desktopEntry;
        QString identity;
        QString playbackStatus;
        QString title;
        QString artist;
        QString trackId;
        uint processId = 0;
        quint64 generation = 0;
        bool canControl = false;
        bool canPlay = false;
        bool canPause = false;
        bool canSeek = false;
        bool canGoPrevious = false;
        bool canGoNext = false;
    };

    struct AudioStream {
        int index = -1;
        int processId = 0;
        QString applicationId;
        QString processBinary;
        QString applicationName;
        bool muted = false;
        bool corked = false;
    };

    void watchMprisService(const QString &service);
    void queryPlayerProcessId(const QString &service, quint64 generation);
    void queryPlayerProperties(const QString &service, const QString &interfaceName, quint64 generation);
    void applyPlayerProperties(const QString &service, const QString &interfaceName, const QVariantMap &properties);
    void callPlayerMethod(const QString &service, const QString &method);
    void seek(const QString &service, qlonglong offsetMicroseconds);
    void startAudioMonitor();
    void refreshAudioStreams();
    void parseAudioStreams(const QByteArray &json);
    void handleAudioSubscriptionOutput();
    [[nodiscard]] QVector<AudioStream> matchingAudioStreams(const QString &appId, int processId) const;
    [[nodiscard]] static bool sameAudioStreams(const QVector<AudioStream> &left, const QVector<AudioStream> &right);
    void bumpRevision();

    QHash<QString, Player> m_players;
    QHash<QString, QString> m_ownerToService;
    quint64 m_playerGeneration = 0;

    QString m_pactlPath;
    QProcess m_audioSubscription;
    QByteArray m_audioSubscriptionBuffer;
    QProcess *m_audioQuery = nullptr;
    bool m_audioRefreshPending = false;
    QVector<AudioStream> m_audioStreams;

    int m_revision = 0;
};

} // namespace krema
