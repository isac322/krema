// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "notificationtracker.h"

#include <settings.h>

#include <KService>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QColor>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimer>

Q_LOGGING_CATEGORY(lcNotif, "krema.notifications")

namespace krema
{
namespace
{
constexpr auto kNotificationService = "org.freedesktop.Notifications";
constexpr auto kSniWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kSniKeySeparator = QLatin1Char('|');
constexpr auto kFerdiumService = "org.ferdium.Ferdium";
constexpr auto kFerdiumPath = "/org/ferdium";
constexpr auto kFerdiumInterface = "org.ferdium.Ferdium";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto kPropertiesChangedSignal = "PropertiesChanged";
constexpr auto kFerdiumDirectProperty = "UnreadDirectMessageCount";
constexpr auto kFerdiumIndirectProperty = "UnreadIndirectMessageCount";
constexpr auto kFerdiumAppId = "org.ferdium.ferdium";
constexpr auto kFerdiumAlias = "ferdium";

static QVariant unwrapDbusVariant(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant();
    }
    return value;
}

static QString sniKey(const QString &service, const QString &objectPath)
{
    return service + kSniKeySeparator + objectPath;
}

// Strip ".desktop" suffix if present.
static QString stripDesktopSuffix(const QString &id)
{
    static const QLatin1String suffix(".desktop");
    if (id.endsWith(suffix)) {
        return id.left(id.size() - suffix.size());
    }
    return id;
}

// Canonicalize the identity used by notification storage, queries, and
// blacklist checks. KService resolves aliases to the installed storage ID;
// unresolved hints/app names deliberately fall back to their lower-case,
// suffix-stripped value rather than using fuzzy last-segment matching.
static QString canonicalDesktopId(const QString &rawId)
{
    QString id = stripDesktopSuffix(rawId.trimmed().toLower());
    static const QRegularExpression statusIconSuffix(QStringLiteral("_[sS]tatus_[iI]con_[0-9]+$"));
    id.remove(statusIconSuffix);
    id = stripDesktopSuffix(id);
    if (id.isEmpty()) {
        return {};
    }

    KService::Ptr service = KService::serviceByStorageId(id);
    if (!service) {
        service = KService::serviceByStorageId(id + QStringLiteral(".desktop"));
    }
    if (!service) {
        service = KService::serviceByDesktopName(id);
    }
    if (service) {
        id = stripDesktopSuffix(service->storageId().toLower());
    }
    return id;
}

struct SniToolTip {
    QString title;
    QString subTitle;
};

static SniToolTip parseSniToolTip(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        if (argument.currentType() == QDBusArgument::VariantType) {
            QDBusVariant nested;
            argument >> nested;
            return parseSniToolTip(nested.variant());
        }
        if (argument.currentType() == QDBusArgument::MapType) {
            if (argument.currentSignature() != QLatin1String("a{sv}")) {
                return {};
            }
            SniToolTip result;
            argument.beginMap();
            while (!argument.atEnd()) {
                QString key;
                QDBusVariant mapValue;
                argument >> key >> mapValue;
                if (key == QLatin1String("title")) {
                    result.title = mapValue.variant().toString();
                } else if (key == QLatin1String("description")) {
                    result.subTitle = mapValue.variant().toString();
                }
            }
            argument.endMap();
            return result;
        }
        if (argument.currentType() != QDBusArgument::StructureType) {
            return {};
        }
        if (argument.currentSignature() != QLatin1String("(sa(iiay)ss)")) {
            return {};
        }
        SniToolTip result;
        QString iconName;
        argument.beginStructure();
        argument >> iconName;
        argument.beginArray();
        while (!argument.atEnd()) {
            argument.beginStructure();
            qint32 width = 0;
            qint32 height = 0;
            QByteArray pixmap;
            argument >> width >> height >> pixmap;
            argument.endStructure();
        }
        argument.endArray();
        argument >> result.title >> result.subTitle;
        argument.endStructure();
        return result;
    }
    if (const auto map = value.toMap(); !map.isEmpty()) {
        return {map.value(QStringLiteral("title")).toString(), map.value(QStringLiteral("description")).toString()};
    }
    const QStringList list = value.toStringList();
    if (list.size() >= 2) {
        return {list.at(list.size() - 2), list.at(list.size() - 1)};
    }
    return {};
}
struct SniPixmap {
    qint32 width = 0;
    qint32 height = 0;
    QByteArray data;
};

static QList<SniPixmap> parseSniPixmaps(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        if (argument.currentType() == QDBusArgument::VariantType) {
            QDBusVariant nested;
            argument >> nested;
            return parseSniPixmaps(nested.variant());
        }
        if (argument.currentType() == QDBusArgument::ArrayType && argument.currentSignature() == QLatin1String("a(iiay)")) {
            QList<SniPixmap> result;
            argument.beginArray();
            while (!argument.atEnd()) {
                if (argument.currentType() != QDBusArgument::StructureType) {
                    return {};
                }
                SniPixmap pixmap;
                argument.beginStructure();
                argument >> pixmap.width >> pixmap.height >> pixmap.data;
                argument.endStructure();
                result.append(std::move(pixmap));
            }
            argument.endArray();
            return result;
        }
        return {};
    }
    const QVariantList list = value.toList();
    QList<SniPixmap> result;
    for (const QVariant &item : list) {
        const QVariantMap map = item.toMap();
        if (!map.isEmpty()) {
            result.append({map.value(QStringLiteral("width")).toInt(),
                           map.value(QStringLiteral("height")).toInt(),
                           map.value(QStringLiteral("data")).toByteArray()});
        }
    }
    return result;
}

static QImage decodeSniPixmap(const QVariant &value)
{
    const auto pixmaps = parseSniPixmaps(value);
    const SniPixmap *best = nullptr;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    for (const auto &pixmap : pixmaps) {
        if (pixmap.width < 1 || pixmap.width > 256 || pixmap.height < 1 || pixmap.height > 256) {
            continue;
        }
        const qint64 expected = qint64(pixmap.width) * pixmap.height * 4;
        if (pixmap.data.size() != expected) {
            continue;
        }
        const qint64 distance = std::abs(qint64(pixmap.width) - 64) + std::abs(qint64(pixmap.height) - 64);
        if (!best || distance < bestDistance) {
            best = &pixmap;
            bestDistance = distance;
        }
    }
    if (!best) {
        return {};
    }
    QImage image(best->width, best->height, QImage::Format_ARGB32);
    for (int y = 0; y < best->height; ++y) {
        for (int x = 0; x < best->width; ++x) {
            const int offset = (y * best->width + x) * 4;
            image.setPixelColor(x, y, QColor(uchar(best->data.at(offset + 1)),
                                             uchar(best->data.at(offset + 2)),
                                             uchar(best->data.at(offset + 3)),
                                             uchar(best->data.at(offset))));
        }
    }
    return image;
}

static QImage loadSniIconFallback(const QString &themePath, const QString &iconName)
{
    if (themePath.isEmpty() || iconName.isEmpty()) {
        return {};
    }
    const QString fileName = iconName.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ? iconName : iconName + QStringLiteral(".png");
    QFile file(QDir(themePath).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 262144) {
        return {};
    }
    const QImage image = QImage::fromData(file.readAll(), "PNG");
    if (image.isNull() || image.width() < 1 || image.width() > 256 || image.height() < 1 || image.height() > 256) {
        return {};
    }
    return image;
}

enum class SniChatClient : quint8 {
    None,
    Slack,
    Discord,
    Vesktop,
    WebCord,
    Mattermost,
    Telegram,
    Ferdium,
};

static QString normalizedSniId(const QString &rawId)
{
    QString id = stripDesktopSuffix(rawId.trimmed().toLower());
    static const QRegularExpression statusIconSuffix(QStringLiteral("_[sS]tatus_[iI]con_[0-9]+$"));
    id.remove(statusIconSuffix);
    return stripDesktopSuffix(id);
}

static SniChatClient sniChatClientForId(const QString &rawId)
{
    const QString id = normalizedSniId(rawId);
    if (id == QLatin1String("slack") || id == QLatin1String("com.slack.slack") || id == QLatin1String("com.slack")) {
        return SniChatClient::Slack;
    }
    if (id == QLatin1String("discord") || id == QLatin1String("com.discordapp.discord")
        || id == QLatin1String("com.discord.app")) {
        return SniChatClient::Discord;
    }
    if (id == QLatin1String("vesktop") || id == QLatin1String("dev.vencord.vesktop") || id == QLatin1String("dev.vencord")) {
        return SniChatClient::Vesktop;
    }
    if (id == QLatin1String("webcord") || id == QLatin1String("io.github.spacingbat3.webcord")) {
        return SniChatClient::WebCord;
    }
    if (id == QLatin1String("mattermost") || id == QLatin1String("mattermost-desktop") || id == QLatin1String("com.mattermost") || id == QLatin1String("com.mattermost.desktop")) {
        return SniChatClient::Mattermost;
    }
    if (id == QLatin1String(kFerdiumAppId) || id == QLatin1String(kFerdiumAlias)) {
        return SniChatClient::Ferdium;
    }
    if (id == QLatin1String("org.telegram") || id == QLatin1String("telegram") || id == QLatin1String("telegram-desktop")
        || id == QLatin1String("telegramdesktop")) {
        return SniChatClient::Telegram;
    }
    return SniChatClient::None;
}

static bool sniIdsMatch(const QString &left, const QString &right)
{
    const auto leftClient = sniChatClientForId(left);
    return leftClient != SniChatClient::None && leftClient == sniChatClientForId(right);
}
static int ferdiumPixelState(const QImage &image)
{
    if (image.isNull()) {
        return 0;
    }
    const int xStart = (image.width() + 1) / 2;
    const int yEnd = (image.height() + 1) / 2;
    int redPixels = 0;
    int tealPixels = 0;
    for (int y = 0; y < yEnd; ++y) {
        for (int x = xStart; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() >= 180 && pixel.red() >= 200 && pixel.green() <= 90 && pixel.blue() <= 90) {
                ++redPixels;
            } else if (pixel.alpha() >= 100 && pixel.red() <= 35 && pixel.green() >= 70 && pixel.green() <= 120 && pixel.blue() >= 115) {
                ++tealPixels;
            }
        }
    }
    if (redPixels >= 25) {
        return 2;
    }
    return tealPixels >= 25 ? 1 : 0;
}

enum class SniPixelColor : quint8 {
    WebCordRed,
    WebCordBlue,
    DiscordRed,
};

static int countSniPixels(const QImage &image, bool topRight, SniPixelColor color)
{
    if (image.isNull()) {
        return 0;
    }
    const int xStart = (image.width() + 1) / 2;
    const int yStart = topRight ? 0 : (image.height() + 1) / 2;
    const int yEnd = topRight ? (image.height() + 1) / 2 : image.height();
    int count = 0;
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const bool red = pixel.alpha() >= 180 && pixel.red() >= 200
                && pixel.green() <= (color == SniPixelColor::WebCordRed ? 90 : 100)
                && pixel.blue() <= (color == SniPixelColor::WebCordRed ? 90 : 100);
            const bool blue = pixel.alpha() >= 180 && pixel.red() <= 170
                && pixel.green() >= 150 && pixel.blue() >= 210
                && pixel.blue() - pixel.red() >= 50;
            if ((color == SniPixelColor::WebCordBlue && blue)
                || (color != SniPixelColor::WebCordBlue && red)) {
                ++count;
            }
        }
    }
    return count;
}

static int webCordPixelState(const QImage &image)
{
    if (countSniPixels(image, true, SniPixelColor::WebCordRed) >= 25) {
        return 2;
    }
    return countSniPixels(image, true, SniPixelColor::WebCordBlue) >= 25 ? 1 : 0;
}

static int discordPixelState(const QImage &image)
{
    const int topRightRed = countSniPixels(image, true, SniPixelColor::DiscordRed);
    const int bottomRightRed = countSniPixels(image, false, SniPixelColor::DiscordRed);
    return topRightRed >= 12 && bottomRightRed == 0 ? 1 : 0;
}

static int slackPixelState(const QImage &image)
{
    int mentionPixels = 0;
    int unreadPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        if (y * 100 < image.height() * 40) {
            continue;
        }
        for (int x = 0; x < image.width(); ++x) {
            if (x * 100 < image.width() * 40) {
                continue;
            }
            const QColor color = image.pixelColor(x, y);
            if (color.alpha() >= 180 && color.red() >= 200 && color.green() <= 100 && color.blue() >= 90) {
                ++mentionPixels;
            } else if (color.alpha() >= 180 && color.red() <= 60 && color.green() >= 170 && color.blue() >= 200) {
                ++unreadPixels;
            }
        }
    }
    return mentionPixels >= 10 ? 2 : (unreadPixels >= 10 ? 1 : 0);
}

static int slackMentionCount(const QString &title, const QString &subTitle)
{
    static const QRegularExpression pattern(
        QStringLiteral("([1-9][0-9]*(?:[.,\\x{00A0}\\x{202F}' ][0-9]{3})*)\\s*(?:notifications?\\b|개의\\s*알림|件の通知|Benachrichtigung(?:en)?\\b|notificaci(?:ón|ones)\\b|notific(?:a|he)\\b|notificaç(?:ão|ões)\\b|(?:条(?:[1-9][0-9]*)?|則)?\\s*通知)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    const auto match = pattern.match(title + QLatin1Char(' ') + subTitle);
    if (!match.hasMatch()) {
        return 0;
    }
    QString digits = match.captured(1);
    digits.removeIf([](QChar ch) {
        return !ch.isDigit();
    });
    bool ok = false;
    const int count = digits.toInt(&ok);
    return (ok && count > 0) ? count : 0;
}

static int mattermostPixelState(const QImage &image, const QString &toolTipTitle)
{
    int mentionPixels = 0;
    int unreadPixels = 0;
    if (!image.isNull()) {
        const int xStart = (image.width() + 1) / 2;
        const int yStart = (image.height() + 1) / 2;
        for (int y = yStart; y < image.height(); ++y) {
            for (int x = xStart; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() >= 180 && pixel.red() >= 210 && pixel.green() <= 90 && pixel.blue() <= 80) {
                    ++mentionPixels;
                } else if (pixel.alpha() >= 180 && pixel.red() <= 120 && pixel.green() >= 130 && pixel.blue() >= 220
                           && pixel.blue() - pixel.red() >= 90) {
                    ++unreadPixels;
                }
            }
        }
    }
    if (mentionPixels >= 10 || toolTipTitle.contains(QStringLiteral("mentioned"), Qt::CaseInsensitive)) {
        return 2;
    }
    return unreadPixels >= 10 || toolTipTitle.contains(QStringLiteral("unread channel"), Qt::CaseInsensitive) ? 1 : 0;
}
static int telegramPixelState(const QImage &image, const QString &iconName)
{
    if (iconName.contains(QStringLiteral("attention"), Qt::CaseInsensitive)) {
        return 2;
    }
    if (iconName.contains(QStringLiteral("mute"), Qt::CaseInsensitive)) {
        return 1;
    }
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return 0;
    }

    int activePixels = 0;
    int grayPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        if (y * 10 < image.height() * 5) {
            continue;
        }
        for (int x = 0; x < image.width(); ++x) {
            if (x * 10 < image.width() * 5) {
                continue;
            }
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() < 200) {
                continue;
            }

            const bool red = pixel.red() >= 200 && pixel.green() <= 110 && pixel.blue() <= 120;
            if (red) {
                ++activePixels;
            } else if (std::abs(pixel.red() - pixel.green()) <= 10
                       && std::abs(pixel.green() - pixel.blue()) <= 10
                       && pixel.red() >= 70 && pixel.red() <= 190) {
                ++grayPixels;
            }
        }
    }

    if (activePixels >= 12 && activePixels > grayPixels) {
        return 2;
    }
    return grayPixels >= 12 ? 1 : 0;
}

} // namespace

NotificationTracker::NotificationTracker(QObject *parent)
    : QObject(parent)
    , m_notificationServiceWatcher(QStringLiteral("org.freedesktop.Notifications"), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange)
    , m_sniWatcherServiceWatcher(QStringLiteral("org.kde.StatusNotifierWatcher"), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange)
    , m_sniServiceWatcher(QString(), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange)
    , m_ferdiumServiceWatcher(QString::fromLatin1(kFerdiumService), QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange)
    , m_dndExpiryTimer(this)
{
    // Settings — must load() explicitly (constructor doesn't auto-load)
    m_notifSettings = std::make_unique<NotificationManager::Settings>(this);
    m_notifSettings->load();

    qCInfo(lcNotif) << "NotificationTracker init: badgesInTaskManager=" << (m_notifSettings ? m_notifSettings->badgesInTaskManager() : true)
                    << "blacklist=" << (m_notifSettings ? m_notifSettings->badgeBlacklistedApplications() : QStringList());

    // DND and badge settings tracking
    connect(m_notifSettings.get(), &NotificationManager::Settings::settingsChanged, this, [this] {
        updateDndState();
        bumpRevision();
    });
    connect(m_notifSettings.get(), &NotificationManager::Settings::notificationsInhibitedByApplicationChanged, this, &NotificationTracker::updateDndState);
    m_dndExpiryTimer.setSingleShot(true);
    connect(&m_dndExpiryTimer, &QTimer::timeout, this, &NotificationTracker::updateDndState);
    setupNotificationWatcher();
    setupSniWatcher();
    setupFerdiumWatcher();
    updateDndState();
}

NotificationTracker::~NotificationTracker()
{
    m_dndExpiryTimer.stop();
    if (!m_notificationWatcherRegistered) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                      QStringLiteral("/org/freedesktop/Notifications"),
                                                      QStringLiteral("org.kde.NotificationManager"),
                                                      QStringLiteral("UnRegisterWatcher"));
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
}

int NotificationTracker::revision() const
{
    return m_revision;
}

bool NotificationTracker::dndActive() const
{
    return m_dndActive;
}

bool NotificationTracker::badgesEnabled(const QString &appId) const
{
    if (appId.isEmpty() || (m_notifSettings && !m_notifSettings->badgesInTaskManager())) {
        return false;
    }
    const QString normalized = canonicalDesktopId(appId);
    if (normalized.isEmpty() || !m_notifSettings) {
        return !normalized.isEmpty();
    }
    const auto blacklist = m_notifSettings->badgeBlacklistedApplications();
    return !std::any_of(blacklist.cbegin(), blacklist.cend(), [&normalized, &appId](const QString &entry) {
        return canonicalDesktopId(entry) == normalized || sniIdsMatch(entry, appId);
    });
}

int NotificationTracker::sniChatUnreadState(const QString &appId) const
{
    if (!badgesEnabled(appId)) {
        return 0;
    }
    if (sniChatClientForId(appId) == SniChatClient::Ferdium) {
        if (m_ferdiumDirectCount > 0) {
            return 2;
        }
        if (m_ferdiumIndirectCount > 0) {
            return 1;
        }
    }
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->chatUnreadState : 0;
}

int NotificationTracker::sniChatMentionCount(const QString &appId) const
{
    if (!badgesEnabled(appId)) {
        return 0;
    }
    if (sniChatClientForId(appId) == SniChatClient::Ferdium) {
        return static_cast<int>(qMin(m_ferdiumDirectCount, static_cast<quint32>(std::numeric_limits<int>::max())));
    }
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->chatMentionCount : 0;
}


void NotificationTracker::updateDndState()
{
    m_dndExpiryTimer.stop();
    bool active = false;
    if (m_notifSettings) {
        active = m_notifSettings->notificationsInhibitedByApplication();
        if (!active) {
            const auto until = m_notifSettings->notificationsInhibitedUntil();
            const auto now = QDateTime::currentDateTime();
            if (until.isValid() && until > now) {
                active = true;
                const qint64 msUntil = now.msecsTo(until);
                // QTimer uses int milliseconds on older Qt releases. A capped
                // timeout rechecks the absolute deadline before ending DND.
                const auto delay = std::chrono::milliseconds(std::clamp<qint64>(msUntil, 1, std::numeric_limits<int>::max()));
                m_dndExpiryTimer.start(delay);
                qCDebug(lcNotif) << "DND expiry scheduled in" << delay.count() << "ms";
            }
        }
    }
    if (m_dndActive != active) {
        m_dndActive = active;
        qCInfo(lcNotif) << "DND state changed:" << active;
        Q_EMIT dndActiveChanged();
    }
}

int NotificationTracker::unreadCount(const QString &desktopEntry) const
{
    if (desktopEntry.isEmpty() || sniChatClientForId(desktopEntry) == SniChatClient::Slack) {
        return 0;
    }

    if (m_notifSettings && !m_notifSettings->badgesInTaskManager()) {
        return 0;
    }

    const QString normalized = canonicalDesktopId(desktopEntry);
    if (normalized.isEmpty()) {
        return 0;
    }

    if (m_notifSettings) {
        const auto blacklist = m_notifSettings->badgeBlacklistedApplications();
        const bool blacklisted = std::any_of(blacklist.cbegin(), blacklist.cend(), [&normalized](const QString &entry) {
            return canonicalDesktopId(entry) == normalized;
        });
        if (blacklisted) {
            qCInfo(lcNotif) << "  unreadCount(" << desktopEntry << ") = 0 (blacklisted)";
            return 0;
        }
    }

    const auto it = m_unreadNotifs.constFind(normalized);
    if (it != m_unreadNotifs.cend()) {
        qCInfo(lcNotif) << "  unreadCount(" << desktopEntry << ") =" << it->size() << "(canonical=" << normalized << ")";
        return it->size();
    }

    if (!m_unreadNotifs.isEmpty()) {
        qCDebug(lcNotif) << "  unreadCount(" << desktopEntry << ") = 0, no exact match. stored keys:" << m_unreadNotifs.keys();
    }
    return 0;
}

const NotificationTracker::SniEntry *NotificationTracker::sniEntryForApp(const QString &appId) const
{
    if (appId.isEmpty()) {
        return nullptr;
    }
    const QString normalized = canonicalDesktopId(appId);
    if (normalized.isEmpty()) {
        return nullptr;
    }
    const SniEntry *best = nullptr;
    QString bestKey;
    int bestRank = -1;
    const QString slackId = canonicalDesktopId(QStringLiteral("slack"));
    for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
        const QString rawId = it->id.trimmed().toLower();
        const QString titleId = canonicalDesktopId(it->title);
        const QString toolTipId = canonicalDesktopId(it->toolTipTitle);
        const bool exactMatch = canonicalDesktopId(it->id) == normalized || sniIdsMatch(it->id, appId);
        const bool chromeTitleMatch = rawId.startsWith(QStringLiteral("chrome_status_icon_"))
            && (titleId == normalized || toolTipId == normalized
                || (sniChatClientForId(appId) == SniChatClient::Slack
                    && (sniChatClientForId(it->title) == SniChatClient::Slack || sniChatClientForId(it->toolTipTitle) == SniChatClient::Slack)));
        if (!exactMatch && !chromeTitleMatch) {
            continue;
        }
        const int rank = it->status == QLatin1String("NeedsAttention") ? 2 : (it->status == QLatin1String("Active") ? 1 : 0);
        if (!best || rank > bestRank || (rank == bestRank && it.key() < bestKey)) {
            best = &it.value();
            bestKey = it.key();
            bestRank = rank;
        }
    }
    return best;
}

bool NotificationTracker::sniNeedsAttention(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    if (entry && entry->status == QLatin1String("NeedsAttention")) {
        qCInfo(lcNotif) << "  sniNeedsAttention(" << appId << ") = true (sniId=" << entry->id << ")";
        return true;
    }
    return false;
}

QString NotificationTracker::sniTitle(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->title : QString();
}

QString NotificationTracker::sniAttentionIconName(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->attentionIconName : QString();
}

QString NotificationTracker::sniOverlayIconName(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->overlayIconName : QString();
}

QString NotificationTracker::sniToolTipTitle(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->toolTipTitle : QString();
}

QString NotificationTracker::sniToolTipSubTitle(const QString &appId) const
{
    const auto *entry = sniEntryForApp(appId);
    return entry ? entry->toolTipSubTitle : QString();
}

void NotificationTracker::clearUnreadNotifications(const QString &appId)
{
    if (appId.isEmpty()) {
        return;
    }

    const QString normalized = canonicalDesktopId(appId);
    const auto unreadIt = m_unreadNotifs.find(normalized);
    if (unreadIt == m_unreadNotifs.end()) {
        return;
    }

    const QSet<uint> notificationIds = unreadIt.value();
    m_unreadNotifs.erase(unreadIt);
    for (uint id : notificationIds) {
        m_notifIdToEntry.remove(id);
    }
    bumpRevision();
}

void NotificationTracker::dumpState() const
{
    qCInfo(lcNotif) << "=== Krema Notification State ===";

    // Notification watcher data
    const bool badgesEnabled = m_notifSettings ? m_notifSettings->badgesInTaskManager() : true;
    qCInfo(lcNotif).noquote() << QStringLiteral("[Notification Watcher] (tracked IDs: %1, badgesEnabled: %2)")
                                     .arg(m_notifIdToEntry.size())
                                     .arg(badgesEnabled ? QStringLiteral("true") : QStringLiteral("false"));

    for (auto it = m_unreadNotifs.cbegin(); it != m_unreadNotifs.cend(); ++it) {
        qCInfo(lcNotif).noquote() << QStringLiteral("  %1 : %2 unread (IDs: %3)").arg(it.key()).arg(it->size()).arg([&]() {
            QStringList ids;
            for (uint id : *it) {
                ids << QString::number(id);
            }
            return ids.join(QStringLiteral(", "));
        }());
    }
    if (m_unreadNotifs.isEmpty()) {
        qCInfo(lcNotif).noquote() << QStringLiteral("  (none)");
    }

    // SNI items
    qCInfo(lcNotif).noquote() << QStringLiteral("[SNI Items] (tracked: %1)").arg(m_sniItems.size());
    for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
        QString line = QStringLiteral("  %1 (%2)").arg(it.value().id, it.value().status);
        if (it.value().status == QLatin1String("NeedsAttention")) {
            line += QStringLiteral(" <- ATTENTION");
        }
        qCInfo(lcNotif).noquote() << line;
    }

    // Blacklisted apps
    if (m_notifSettings) {
        const auto blacklist = m_notifSettings->badgeBlacklistedApplications();
        if (!blacklist.isEmpty()) {
            qCInfo(lcNotif).noquote() << QStringLiteral("[Blacklisted] %1").arg(blacklist.join(QStringLiteral(", ")));
        }
    }
}

// --- D-Bus notification watcher ---

void NotificationTracker::setupNotificationWatcher()
{
    auto bus = QDBusConnection::sessionBus();
    connect(&m_notificationServiceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &NotificationTracker::onNotificationServiceOwnerChanged);

    // The notification server hardcodes this object path/interface for
    // RegisterWatcher callbacks; it is not the freedesktop server path.
    if (!m_notificationObjectRegistered) {
        if (!bus.registerObject(QStringLiteral("/NotificationWatcher"),
                                QStringLiteral("org.kde.NotificationWatcher"),
                                this,
                                QDBusConnection::ExportScriptableSlots)) {
            qCWarning(lcNotif) << "Failed to register D-Bus notification watcher object:" << bus.lastError().message();
        } else {
            m_notificationObjectRegistered = true;
            qCInfo(lcNotif) << "D-Bus watcher object registered at /NotificationWatcher";
        }
    }

    connectNotificationClosedMatch();

    const auto *dbusInterface = bus.interface();
    if (dbusInterface && dbusInterface->isServiceRegistered(QString::fromLatin1(kNotificationService))) {
        registerNotificationWatcher();
    } else {
        qCInfo(lcNotif) << "Notification server is not owned yet; waiting for service registration";
    }
}

void NotificationTracker::connectNotificationClosedMatch()
{
    if (m_notificationClosedMatchConnected) {
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    if (!bus.connect(QString::fromLatin1(kNotificationService),
                     QStringLiteral("/org/freedesktop/Notifications"),
                     QStringLiteral("org.freedesktop.Notifications"),
                     QStringLiteral("NotificationClosed"),
                     this,
                     SLOT(NotificationClosed(uint, uint)))) {
        qCWarning(lcNotif) << "Failed to subscribe to NotificationClosed:" << bus.lastError().message();
        return;
    }
    m_notificationClosedMatchConnected = true;
}

void NotificationTracker::registerNotificationWatcher()
{
    if (!m_notificationObjectRegistered || m_notificationRegisterPending || m_notificationWatcherRegistered) {
        return;
    }

    auto bus = QDBusConnection::sessionBus();
    const auto *dbusInterface = bus.interface();
    if (!dbusInterface || !dbusInterface->isServiceRegistered(QString::fromLatin1(kNotificationService))) {
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(QString::fromLatin1(kNotificationService),
                                                      QStringLiteral("/org/freedesktop/Notifications"),
                                                      QStringLiteral("org.kde.NotificationManager"),
                                                      QStringLiteral("RegisterWatcher"));
    const quint64 generation = ++m_notificationOwnerGeneration;
    m_notificationRegisterPending = true;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_notificationOwnerGeneration) {
            return;
        }
        m_notificationRegisterPending = false;
        if (reply.isError()) {
            qCWarning(lcNotif) << "RegisterWatcher FAILED:" << reply.error().message();
            return;
        }
        m_notificationWatcherRegistered = true;
        qCInfo(lcNotif) << "RegisterWatcher succeeded — now receiving notifications";
    });
}

void NotificationTracker::onNotificationServiceOwnerChanged(const QString &service, const QString &, const QString &newOwner)
{
    if (service != QString::fromLatin1(kNotificationService)) {
        return;
    }
    ++m_notificationOwnerGeneration;
    m_notificationWatcherRegistered = false;
    m_notificationRegisterPending = false;
    qCInfo(lcNotif) << "Notification server owner changed; watcher registration invalidated";
    if (!newOwner.isEmpty()) {
        registerNotificationWatcher();
    }
}

bool NotificationTracker::removeActiveNotification(uint id)
{
    const auto it = m_notifIdToEntry.find(id);
    if (it == m_notifIdToEntry.end()) {
        return false;
    }

    const QString entry = it.value();
    if (auto unreadIt = m_unreadNotifs.find(entry); unreadIt != m_unreadNotifs.end()) {
        unreadIt->remove(id);
        if (unreadIt->isEmpty()) {
            m_unreadNotifs.erase(unreadIt);
        }
    }
    m_notifIdToEntry.erase(it);
    return true;
}

void NotificationTracker::Notify(uint id,
                                 const QString &appName,
                                 uint replacesId,
                                 const QString &,
                                 const QString &,
                                 const QString &,
                                 const QStringList &,
                                 const QVariantMap &hints,
                                 int)
{
    QString rawEntry = hints.value(QStringLiteral("desktop-entry")).toString();
    if (rawEntry.isEmpty()) {
        rawEntry = appName;
    }
    const QString desktopEntry = canonicalDesktopId(rawEntry);
    if (desktopEntry.isEmpty() || sniChatClientForId(desktopEntry) == SniChatClient::Slack) {
        return;
    }

    removeActiveNotification(id);
    if (replacesId > 0) {
        removeActiveNotification(replacesId);
    }

    m_notifIdToEntry[id] = desktopEntry;
    m_unreadNotifs[desktopEntry].insert(id);
    bumpRevision();
}

void NotificationTracker::CloseNotification(uint id)
{
    if (removeActiveNotification(id)) {
        bumpRevision();
    }
}

void NotificationTracker::NotificationClosed(uint id, uint)
{
    if (removeActiveNotification(id)) {
        bumpRevision();
    }
}

void NotificationTracker::setupFerdiumWatcher()
{
    auto bus = QDBusConnection::sessionBus();
    connect(&m_ferdiumServiceWatcher,
            &QDBusServiceWatcher::serviceOwnerChanged,
            this,
            &NotificationTracker::onFerdiumServiceOwnerChanged);

    if (!m_ferdiumPropertiesMatchConnected) {
        m_ferdiumPropertiesMatchConnected =
            bus.connect(QString::fromLatin1(kFerdiumService),
                       QString::fromLatin1(kFerdiumPath),
                       QString::fromLatin1(kPropertiesInterface),
                       QString::fromLatin1(kPropertiesChangedSignal),
                       this,
                       SLOT(onFerdiumPropertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
        if (!m_ferdiumPropertiesMatchConnected) {
            qCWarning(lcNotif) << "Failed to subscribe to Ferdium PropertiesChanged:" << bus.lastError().message();
        }
    }

    const auto *dbusInterface = bus.interface();
    if (dbusInterface && dbusInterface->isServiceRegistered(QString::fromLatin1(kFerdiumService))) {
        m_ferdiumServiceOwned = true;
        queryFerdiumCounts(m_ferdiumOwnerGeneration);
    } else {
        qCInfo(lcNotif) << "Ferdium service is not owned yet; waiting for service registration";
    }
}

void NotificationTracker::setFerdiumCounts(quint32 directCount, quint32 indirectCount)
{
    if (m_ferdiumDirectCount == directCount && m_ferdiumIndirectCount == indirectCount) {
        return;
    }

    m_ferdiumDirectCount = directCount;
    m_ferdiumIndirectCount = indirectCount;
    for (auto &entry : m_sniItems) {
        if (sniChatClientForId(entry.id) == SniChatClient::Ferdium) {
            updateSniChatState(entry);
        }
    }
    qCInfo(lcNotif) << "Ferdium unread counts changed: direct=" << directCount << "indirect=" << indirectCount;
    bumpRevision();
}

void NotificationTracker::queryFerdiumCounts(quint64 generation)
{
    if (generation != m_ferdiumOwnerGeneration || !m_ferdiumServiceOwned) {
        return;
    }
    if (m_ferdiumQueryInFlight) {
        m_ferdiumQueryPending = true;
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kFerdiumService),
                                                          QString::fromLatin1(kFerdiumPath),
                                                          QString::fromLatin1(kPropertiesInterface),
                                                          QStringLiteral("GetAll"));
    message << QString::fromLatin1(kFerdiumInterface);
    m_ferdiumQueryInFlight = true;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QVariantMap> reply = *w;
        w->deleteLater();
        if (generation != m_ferdiumOwnerGeneration) {
            return;
        }
        m_ferdiumQueryInFlight = false;
        if (!m_ferdiumServiceOwned) {
            return;
        }
        if (reply.isError()) {
            qCWarning(lcNotif) << "Ferdium count query failed:" << reply.error().message();
        } else {
            const QVariantMap properties = reply.value();
            const quint32 directCount = unwrapDbusVariant(properties.value(QString::fromLatin1(kFerdiumDirectProperty))).toUInt();
            const quint32 indirectCount = unwrapDbusVariant(properties.value(QString::fromLatin1(kFerdiumIndirectProperty))).toUInt();
            setFerdiumCounts(directCount, indirectCount);
        }
        if (m_ferdiumQueryPending) {
            m_ferdiumQueryPending = false;
            queryFerdiumCounts(generation);
        }
    });
}

void NotificationTracker::onFerdiumServiceOwnerChanged(const QString &service, const QString &, const QString &newOwner)
{
    if (service != QString::fromLatin1(kFerdiumService)) {
        return;
    }

    ++m_ferdiumOwnerGeneration;
    m_ferdiumQueryInFlight = false;
    m_ferdiumQueryPending = false;
    m_ferdiumServiceOwned = !newOwner.isEmpty();
    m_ferdiumFallbackSuppressed = newOwner.isEmpty();
    bool sniFallbackCleared = false;
    QStringList ferdiumSniKeys;
    QStringList pendingIdSniKeys;
    for (auto it = m_sniItems.begin(); it != m_sniItems.end(); ++it) {
        const bool isFerdium = sniChatClientForId(it->id) == SniChatClient::Ferdium;
        const bool isPendingId = it->id.isEmpty();
        if (isFerdium || (m_ferdiumServiceOwned && isPendingId)) {
            if (!it->iconPixmapImage.isNull() || !it->iconImage.isNull() || it->chatUnreadState != 0 || it->chatMentionCount != 0) {
                sniFallbackCleared = true;
            }
            it->generation = ++m_sniItemGeneration;
            it->ferdiumOwnerGeneration = m_ferdiumOwnerGeneration;
            it->iconQueryInFlight = false;
            it->iconQueryPending = false;
            it->iconQueryOutstanding = 0;
            it->iconPixmapImage = {};
            it->iconImage = {};
            it->chatUnreadState = 0;
            it->chatMentionCount = 0;
            if (isFerdium) {
                ferdiumSniKeys.append(it.key());
            } else {
                it->initialQueryFailed = true;
                pendingIdSniKeys.append(it.key());
            }
        }
    }
    const int revBefore = m_revision;
    setFerdiumCounts(0, 0);
    if (sniFallbackCleared && m_revision == revBefore) {
        bumpRevision();
    }
    qCInfo(lcNotif) << "Ferdium owner changed; owned=" << m_ferdiumServiceOwned;
    if (m_ferdiumServiceOwned) {
        queryFerdiumCounts(m_ferdiumOwnerGeneration);
        for (const QString &key : pendingIdSniKeys) {
            if (auto it = m_sniItems.find(key); it != m_sniItems.end()) {
                ensureSniMetadataQueried(key, *it);
            }
        }
        for (const QString &key : ferdiumSniKeys) {
            if (auto it = m_sniItems.find(key); it != m_sniItems.end()) {
                scheduleSniIconRefresh(key, it->iconQueryInterface);
            }
        }
    }
}

void NotificationTracker::onFerdiumPropertiesChanged(const QString &interfaceName,
                                                      const QVariantMap &changedProperties,
                                                      const QStringList &invalidatedProperties,
                                                      const QDBusMessage &message)
{
    if (interfaceName != QString::fromLatin1(kFerdiumInterface) || message.path() != QString::fromLatin1(kFerdiumPath)) {
        return;
    }
    bool changed = false;
    quint32 directCount = m_ferdiumDirectCount;
    quint32 indirectCount = m_ferdiumIndirectCount;
    const QString directProperty = QString::fromLatin1(kFerdiumDirectProperty);
    const QString indirectProperty = QString::fromLatin1(kFerdiumIndirectProperty);
    if (changedProperties.contains(directProperty)) {
        directCount = unwrapDbusVariant(changedProperties.value(directProperty)).toUInt();
        changed = true;
    }
    if (changedProperties.contains(indirectProperty)) {
        indirectCount = unwrapDbusVariant(changedProperties.value(indirectProperty)).toUInt();
        changed = true;
    }
    const bool invalidated = invalidatedProperties.contains(directProperty)
        || invalidatedProperties.contains(indirectProperty);
    if (!changed && !invalidated) {
        return;
    }
    if (changed) {
        ++m_ferdiumOwnerGeneration;
        m_ferdiumQueryInFlight = false;
        m_ferdiumQueryPending = false;
        setFerdiumCounts(directCount, indirectCount);
    }
    if (invalidated) {
        queryFerdiumCounts(m_ferdiumOwnerGeneration);
    }
}

// --- SNI watcher ---

void NotificationTracker::setupSniWatcher()
{
    auto bus = QDBusConnection::sessionBus();

    if (!m_sniWatcherRegisteredMatchConnected) {
        m_sniWatcherRegisteredMatchConnected =
            bus.connect(QString::fromLatin1(kSniWatcherService),
                        QStringLiteral("/StatusNotifierWatcher"),
                        QStringLiteral("org.kde.StatusNotifierWatcher"),
                        QStringLiteral("StatusNotifierItemRegistered"),
                        this,
                        SLOT(onSniRegistered(QString)));
        if (!m_sniWatcherRegisteredMatchConnected) {
            qCWarning(lcNotif) << "Failed to subscribe to StatusNotifierItemRegistered:" << bus.lastError().message();
        }
    }
    if (!m_sniWatcherUnregisteredMatchConnected) {
        m_sniWatcherUnregisteredMatchConnected =
            bus.connect(QString::fromLatin1(kSniWatcherService),
                        QStringLiteral("/StatusNotifierWatcher"),
                        QStringLiteral("org.kde.StatusNotifierWatcher"),
                        QStringLiteral("StatusNotifierItemUnregistered"),
                        this,
                        SLOT(onSniUnregistered(QString)));
        if (!m_sniWatcherUnregisteredMatchConnected) {
            qCWarning(lcNotif) << "Failed to subscribe to StatusNotifierItemUnregistered:" << bus.lastError().message();
        }
    }
    if (!m_sniStatusMatchConnected) {
        m_sniStatusMatchConnected =
            bus.connect(QString(),
                        QString(),
                        QStringLiteral("org.kde.StatusNotifierItem"),
                        QStringLiteral("NewStatus"),
                        this,
                        SLOT(onSniNewStatus(QString, QDBusMessage)));
        if (!m_sniStatusMatchConnected) {
            qCWarning(lcNotif) << "Failed to subscribe to StatusNotifierItem.NewStatus:" << bus.lastError().message();
        }
    }
    const auto connectSniSignal = [this, &bus](bool &connected, const QString &interfaceName, const QString &signal, const char *slot) {
        if (connected) {
            return;
        }
        connected = bus.connect(QString(), QString(), interfaceName, signal, this, slot);
        if (!connected) {
            qCWarning(lcNotif) << "Failed to subscribe to" << interfaceName << signal << ":" << bus.lastError().message();
        }
    };
    connectSniSignal(m_sniTitleMatchConnected, QStringLiteral("org.kde.StatusNotifierItem"), QStringLiteral("NewTitle"), SLOT(onSniNewTitle(QDBusMessage)));
    connectSniSignal(m_sniAttentionIconMatchConnected, QStringLiteral("org.kde.StatusNotifierItem"), QStringLiteral("NewAttentionIcon"), SLOT(onSniNewAttentionIcon(QDBusMessage)));
    connectSniSignal(m_sniOverlayIconMatchConnected, QStringLiteral("org.kde.StatusNotifierItem"), QStringLiteral("NewOverlayIcon"), SLOT(onSniNewOverlayIcon(QDBusMessage)));
    connectSniSignal(m_sniToolTipMatchConnected, QStringLiteral("org.kde.StatusNotifierItem"), QStringLiteral("NewToolTip"), SLOT(onSniNewToolTip(QDBusMessage)));
    connectSniSignal(m_sniIconMatchConnected, QStringLiteral("org.kde.StatusNotifierItem"), QStringLiteral("NewIcon"), SLOT(onSniNewIcon(QDBusMessage)));
    connectSniSignal(m_sniIconThemePathMatchConnected, QStringLiteral("org.kde.StatusNotifierItem"), QStringLiteral("NewIconThemePath"), SLOT(onSniNewIconThemePath(QDBusMessage)));
    connectSniSignal(m_sniFreedesktopIconMatchConnected, QStringLiteral("org.freedesktop.StatusNotifierItem"), QStringLiteral("NewIcon"), SLOT(onSniNewIcon(QDBusMessage)));
    connectSniSignal(m_sniFreedesktopIconThemePathMatchConnected, QStringLiteral("org.freedesktop.StatusNotifierItem"), QStringLiteral("NewIconThemePath"), SLOT(onSniNewIconThemePath(QDBusMessage)));
    if (!m_sniPropertiesChangedMatchConnected) {
        m_sniPropertiesChangedMatchConnected =
            bus.connect(QString(),
                        QString(),
                        QStringLiteral("org.freedesktop.DBus.Properties"),
                        QStringLiteral("PropertiesChanged"),
                        this,
                        SLOT(onSniPropertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    }
    connect(&m_sniServiceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &NotificationTracker::onSniServiceOwnerChanged);
    connect(&m_sniWatcherServiceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &NotificationTracker::onSniWatcherServiceOwnerChanged);
    const auto *dbusInterface = bus.interface();
    if (dbusInterface && dbusInterface->isServiceRegistered(QString::fromLatin1(kSniWatcherService))) {
        queryRegisteredSniItems();
    } else {
        qCInfo(lcNotif) << "SNI watcher is not owned yet; waiting for service registration";
    }
}

void NotificationTracker::bumpRevision()
{
    ++m_revision;
    // Build state summary for debugging
    QStringList summary;
    for (auto it = m_unreadNotifs.cbegin(); it != m_unreadNotifs.cend(); ++it) {
        summary << QStringLiteral("%1=%2").arg(it.key()).arg(it->size());
    }
    int sniAttentionCount = 0;
    for (const auto &sni : m_sniItems) {
        if (sni.status == QLatin1String("NeedsAttention"))
            ++sniAttentionCount;
    }
    const bool badgesEnabled = m_notifSettings ? m_notifSettings->badgesInTaskManager() : true;
    qCInfo(lcNotif) << "bumpRevision:" << m_revision << "| unread:" << (summary.isEmpty() ? QStringLiteral("(none)") : summary.join(QStringLiteral(", ")))
                    << "| sniAttention:" << sniAttentionCount << "| badgesEnabled:" << badgesEnabled;

    Q_EMIT revisionChanged();
}

void NotificationTracker::queryRegisteredSniItems()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QString::fromLatin1(kSniWatcherService),
                                                      QStringLiteral("/StatusNotifierWatcher"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    msg << QStringLiteral("org.kde.StatusNotifierWatcher") << QStringLiteral("RegisteredStatusNotifierItems");

    const quint64 watcherGeneration = m_sniWatcherGeneration;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcherGeneration](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusVariant> reply = *w;
        w->deleteLater();
        if (watcherGeneration != m_sniWatcherGeneration) {
            return;
        }
        if (reply.isError()) {
            qCWarning(lcNotif) << "Failed to query SNI items:" << reply.error().message();
            return;
        }

        const QStringList items = reply.value().variant().toStringList();
        qCInfo(lcNotif) << "Registered SNI items:" << items.size() << items;
        for (const QString &item : items) {
            onSniRegistered(item);
        }
    });
}

void NotificationTracker::trackSniItem(const QString &service, const QString &objectPath)
{
    if (service.isEmpty() || objectPath.isEmpty() || !objectPath.startsWith(QLatin1Char('/'))) {
        qCWarning(lcNotif) << "Ignoring malformed SNI item:" << service << objectPath;
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    const auto *dbusInterface = bus.interface();
    if (!dbusInterface || !dbusInterface->isServiceRegistered(service)) {
        qCInfo(lcNotif) << "Ignoring SNI item whose service is no longer owned:" << service << objectPath;
        return;
    }
    const QString key = sniKey(service, objectPath);
    if (auto it = m_sniItems.find(key); it != m_sniItems.end()) {
        if (it->initialQueryFailed || it->id.isEmpty()) {
            ensureSniMetadataQueried(key, *it);
        }
        return;
    }

    m_sniServiceWatcher.addWatchedService(service);
    const quint64 generation = ++m_sniItemGeneration;
    SniEntry entry;
    entry.status = QStringLiteral("Passive");
    entry.generation = generation;
    entry.ferdiumOwnerGeneration = m_ferdiumOwnerGeneration;
    m_sniItems.insert(key, entry);
    queryAllSniProperties(key, generation, service, objectPath);
}
void NotificationTracker::queryAllSniProperties(const QString &key,
                                                 quint64 generation,
                                                 const QString &service,
                                                 const QString &objectPath)
{
    querySniProperty(key, generation, service, objectPath, QStringLiteral("Id"));
    querySniProperty(key, generation, service, objectPath, QStringLiteral("Status"));
    querySniProperty(key, generation, service, objectPath, QStringLiteral("Title"));
    querySniProperty(key, generation, service, objectPath, QStringLiteral("AttentionIconName"));
    querySniProperty(key, generation, service, objectPath, QStringLiteral("OverlayIconName"));
    querySniProperty(key, generation, service, objectPath, QStringLiteral("ToolTip"));
    querySniIconProperties(key, generation, service, objectPath, QStringLiteral("org.kde.StatusNotifierItem"));

}
void NotificationTracker::updateSniIconImage(SniEntry &entry)
{
    entry.iconImage = !entry.iconPixmapImage.isNull() ? entry.iconPixmapImage : loadSniIconFallback(entry.iconThemePath, entry.iconName);
}

void NotificationTracker::updateSniChatState(SniEntry &entry)
{
    entry.chatMentionCount = 0;
    const QString rawId = entry.id.trimmed().toLower();
    const bool isChromeStatusIcon = rawId.startsWith(QStringLiteral("chrome_status_icon_"));
    const auto chatClient = sniChatClientForId(entry.id);
    const bool isSlack = chatClient == SniChatClient::Slack
        || (isChromeStatusIcon && (sniChatClientForId(entry.title) == SniChatClient::Slack || sniChatClientForId(entry.toolTipTitle) == SniChatClient::Slack));
    if (isSlack) {
        const int pixelState = slackPixelState(entry.iconImage);
        entry.chatMentionCount = slackMentionCount(entry.toolTipTitle, entry.toolTipSubTitle);
        entry.chatUnreadState = entry.chatMentionCount > 0 ? 2 : pixelState;
        return;
    }
    if (chatClient == SniChatClient::Ferdium) {
        if (m_ferdiumDirectCount > 0) {
            entry.chatUnreadState = 2;
            entry.chatMentionCount = static_cast<int>(qMin(m_ferdiumDirectCount, static_cast<quint32>(std::numeric_limits<int>::max())));
        } else if (m_ferdiumIndirectCount > 0) {
            entry.chatUnreadState = 1;
            entry.chatMentionCount = 0;
        } else if (!m_ferdiumFallbackSuppressed) {
            entry.chatUnreadState = ferdiumPixelState(entry.iconImage);
            entry.chatMentionCount = 0;
        } else {
            entry.chatUnreadState = 0;
            entry.chatMentionCount = 0;
        }
        return;
    }
    if (chatClient == SniChatClient::Telegram) {
        entry.chatUnreadState = telegramPixelState(entry.iconImage, entry.iconName);
        entry.chatMentionCount = 0;
    } else if (chatClient == SniChatClient::Mattermost) {
        entry.chatUnreadState = mattermostPixelState(entry.iconImage, entry.toolTipTitle);
    } else if (chatClient == SniChatClient::WebCord) {
        entry.chatUnreadState = webCordPixelState(entry.iconImage);
    } else if (chatClient == SniChatClient::Discord || chatClient == SniChatClient::Vesktop) {
        entry.chatUnreadState = discordPixelState(entry.iconImage);
    } else {
        entry.chatUnreadState = 0;
    }
}

void NotificationTracker::querySniProperty(const QString &key,
                                            quint64 generation,
                                            const QString &service,
                                            const QString &objectPath,
                                            const QString &property,
                                            const QString &interfaceName,
                                            bool iconQuery)
{
    const QString interface = interfaceName.isEmpty() ? QStringLiteral("org.kde.StatusNotifierItem") : interfaceName;
    QDBusMessage msg = QDBusMessage::createMethodCall(service, objectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << interface << property;
    const quint64 ferdiumGeneration = m_ferdiumOwnerGeneration;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, key, generation, ferdiumGeneration, property, iconQuery](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusVariant> reply = *w;
        w->deleteLater();
        auto it = m_sniItems.find(key);
        const auto finishIconQuery = [this, key, generation] {
            auto current = m_sniItems.find(key);
            if (current == m_sniItems.end() || current->generation != generation) {
                return;
            }
            if (current->iconQueryOutstanding > 0) {
                --current->iconQueryOutstanding;
            }
            if (current->iconQueryOutstanding == 0) {
                const bool changed = current->iconImage != current->iconQueryPreviousImage
                    || current->chatUnreadState != current->iconQueryPreviousChatState
                    || current->chatMentionCount != current->iconQueryPreviousMentionCount;
                current->iconQueryInFlight = false;
                const bool pending = current->iconQueryPending;
                const QString pendingInterface = current->iconQueryInterface;
                current->iconQueryPending = false;
                if (changed) {
                    bumpRevision();
                }
                if (pending) {
                    if (auto next = m_sniItems.find(key); next != m_sniItems.end() && next->generation == generation) {
                        scheduleSniIconRefresh(key, pendingInterface);
                    }
                }
            }
        };
        if (reply.isError()) {
            if (it != m_sniItems.end() && it->generation == generation) {
                const int oldState = it->chatUnreadState;
                const int oldCount = it->chatMentionCount;
                const QString oldTitle = it->title;
                const QString oldAttentionIcon = it->attentionIconName;
                const QString oldOverlayIcon = it->overlayIconName;
                const QString oldToolTipTitle = it->toolTipTitle;
                const QString oldToolTipSubTitle = it->toolTipSubTitle;
                if (property == QLatin1String("ToolTip")) {
                    it->toolTipTitle.clear();
                    it->toolTipSubTitle.clear();
                } else if (property == QLatin1String("IconPixmap")) {
                    it->iconPixmapImage = {};
                    updateSniIconImage(*it);
                } else if (property == QLatin1String("IconName")) {
                    it->iconName.clear();
                    updateSniIconImage(*it);
                } else if (property == QLatin1String("IconThemePath")) {
                    it->iconThemePath.clear();
                    updateSniIconImage(*it);
                } else if (property == QLatin1String("Title")) {
                    it->title.clear();
                } else if (property == QLatin1String("AttentionIconName")) {
                    it->attentionIconName.clear();
                } else if (property == QLatin1String("OverlayIconName")) {
                    it->overlayIconName.clear();
                } else if (!iconQuery) {
                    it->initialQueryFailed = true;
                }
                updateSniChatState(*it);
                if (!iconQuery && (it->chatUnreadState != oldState || it->chatMentionCount != oldCount
                                   || it->title != oldTitle || it->attentionIconName != oldAttentionIcon
                                   || it->overlayIconName != oldOverlayIcon || it->toolTipTitle != oldToolTipTitle
                                   || it->toolTipSubTitle != oldToolTipSubTitle)) {
                    bumpRevision();
                }
            }
            if (iconQuery) {
                finishIconQuery();
            }
            qCInfo(lcNotif) << "SNI property query failed for" << key << property << ":" << reply.error().message();
            return;
        }
        if (it == m_sniItems.end() || it->generation != generation) {
            return;
        }
        const QVariant value = reply.value().variant();
        if (property == QLatin1String("ToolTip")) {
            const auto toolTip = parseSniToolTip(value);
            it->toolTipTitle = toolTip.title;
            it->toolTipSubTitle = toolTip.subTitle;
        } else if (property == QLatin1String("IconPixmap")) {
            it->iconPixmapImage = decodeSniPixmap(value);
            updateSniIconImage(*it);
        } else if (property == QLatin1String("IconName")) {
            it->iconName = value.toString();
            updateSniIconImage(*it);
        } else if (property == QLatin1String("IconThemePath")) {
            it->iconThemePath = value.toString();
            updateSniIconImage(*it);
        } else {
            QString *storedValue = nullptr;
            if (property == QLatin1String("Id")) {
                storedValue = &it->id;
            } else if (property == QLatin1String("Status")) {
                storedValue = &it->status;
            } else if (property == QLatin1String("Title")) {
                storedValue = &it->title;
            } else if (property == QLatin1String("AttentionIconName")) {
                storedValue = &it->attentionIconName;
            } else if (property == QLatin1String("OverlayIconName")) {
                storedValue = &it->overlayIconName;
            }
            if (storedValue) {
                *storedValue = value.toString();
            }
            if (property == QLatin1String("Id") && sniChatClientForId(it->id) == SniChatClient::Ferdium
                && ferdiumGeneration != m_ferdiumOwnerGeneration) {
                it->ferdiumOwnerGeneration = m_ferdiumOwnerGeneration;
                it->iconPixmapImage = {};
                it->iconImage = {};
                if (!m_ferdiumFallbackSuppressed) {
                    scheduleSniIconRefresh(key, it->iconQueryInterface);
                }
            }
        }
        updateSniChatState(*it);
        if (iconQuery) {
            finishIconQuery();
        } else {
            bumpRevision();
        }
    });
}

void NotificationTracker::querySniIconProperties(const QString &key,
                                                  quint64 generation,
                                                  const QString &service,
                                                  const QString &objectPath,
                                                  const QString &interfaceName)
{
    auto it = m_sniItems.find(key);
    if (it == m_sniItems.end() || it->generation != generation) {
        return;
    }
    if (it->iconQueryInFlight) {
        it->iconQueryPending = true;
        it->iconQueryInterface = interfaceName;
        return;
    }
    it->iconQueryInFlight = true;
    it->iconQueryPending = false;
    it->iconQueryInterface = interfaceName;
    it->iconQueryPreviousImage = it->iconImage;
    it->iconQueryPreviousChatState = it->chatUnreadState;
    it->iconQueryPreviousMentionCount = it->chatMentionCount;
    it->iconQueryOutstanding = 3;
    querySniProperty(key, generation, service, objectPath, QStringLiteral("IconPixmap"), interfaceName, true);
    querySniProperty(key, generation, service, objectPath, QStringLiteral("IconName"), interfaceName, true);
    querySniProperty(key, generation, service, objectPath, QStringLiteral("IconThemePath"), interfaceName, true);
}

void NotificationTracker::scheduleSniIconRefresh(const QString &key, const QString &interfaceName)
{
    if (auto it = m_sniItems.find(key); it != m_sniItems.end()) {
        it->iconQueryPending = true;
        it->iconQueryInterface = interfaceName.isEmpty() ? QStringLiteral("org.kde.StatusNotifierItem") : interfaceName;
        if (!m_sniIconRefreshQueued) {
            m_sniIconRefreshQueued = true;
            QMetaObject::invokeMethod(this, &NotificationTracker::processSniIconRefreshes, Qt::QueuedConnection);
        }
    }
}

void NotificationTracker::processSniIconRefreshes()
{
    m_sniIconRefreshQueued = false;
    const auto keys = m_sniItems.keys();
    for (const QString &key : keys) {
        auto it = m_sniItems.find(key);
        if (it == m_sniItems.end() || !it->iconQueryPending) {
            continue;
        }
        const int separator = key.indexOf(kSniKeySeparator);
        if (separator <= 0) {
            continue;
        }
        it->iconQueryPending = false;
        querySniIconProperties(key,
                               it->generation,
                               key.left(separator),
                               key.mid(separator + 1),
                               it->iconQueryInterface);
    }
}
void NotificationTracker::ensureSniMetadataQueried(const QString &key, SniEntry &entry)
{
    if (!entry.initialQueryFailed && !entry.id.isEmpty()) {
        return;
    }
    const int separator = key.indexOf(kSniKeySeparator);
    if (separator <= 0) {
        return;
    }
    entry.initialQueryFailed = false;
    queryAllSniProperties(key,
                          entry.generation,
                          key.left(separator),
                          key.mid(separator + 1));
}
void NotificationTracker::clearSniItems()
{
    if (m_sniItems.isEmpty()) {
        return;
    }
    for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
        const int separator = it.key().indexOf(kSniKeySeparator);
        if (separator > 0) {
            m_sniServiceWatcher.removeWatchedService(it.key().left(separator));
        }
    }
    m_sniItems.clear();
    bumpRevision();
}


// --- SNI D-Bus slot handlers ---

void NotificationTracker::onSniRegistered(const QString &item)
{
    qCInfo(lcNotif) << "SNI registered:" << item;

    const int slashIdx = item.indexOf(QLatin1Char('/'));
    QString service, objectPath;
    if (slashIdx > 0) {
        service = item.left(slashIdx);
        objectPath = item.mid(slashIdx);
    } else {
        service = item;
        objectPath = QStringLiteral("/StatusNotifierItem");
    }
    trackSniItem(service, objectPath);
}

void NotificationTracker::onSniUnregistered(const QString &item)
{
    const int slashIdx = item.indexOf(QLatin1Char('/'));
    const QString service = slashIdx > 0 ? item.left(slashIdx) : item;
    const QString objectPath = slashIdx > 0 ? item.mid(slashIdx) : QStringLiteral("/StatusNotifierItem");
    const QString key = sniKey(service, objectPath);

    if (m_sniItems.remove(key) > 0) {
        bool hasMoreItems = false;
        const QString prefix = service + kSniKeySeparator;
        for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
            if (it.key().startsWith(prefix)) {
                hasMoreItems = true;
                break;
            }
        }
        if (!hasMoreItems) {
            m_sniServiceWatcher.removeWatchedService(service);
        }
        bumpRevision();
        qCInfo(lcNotif) << "SNI unregistered:" << service << objectPath;
    }
}

QString NotificationTracker::sniKeyForMessage(const QDBusMessage &message) const
{
    const QString directKey = sniKey(message.service(), message.path());
    if (m_sniItems.contains(directKey)) {
        return directKey;
    }

    const auto *dbusInterface = QDBusConnection::sessionBus().interface();
    for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
        const int separator = it.key().indexOf(kSniKeySeparator);
        if (separator <= 0 || it.key().mid(separator + 1) != message.path()) {
            continue;
        }

        const QString registeredService = it.key().left(separator);
        if (registeredService == message.service()
            || (dbusInterface && dbusInterface->serviceOwner(registeredService) == message.service())) {
            return it.key();
        }
    }
    return directKey;
}

void NotificationTracker::onSniNewStatus(const QString &status, const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);

    if (auto it = m_sniItems.find(key); it != m_sniItems.end()) {
        ensureSniMetadataQueried(key, *it);
        const QString oldStatus = it->status;
        if (oldStatus != status) {
            qCInfo(lcNotif) << ">>> SNI status changed:" << it->id << "(" << key << ")" << oldStatus << "->" << status;
        }
        it->status = status;

        // When transitioning away from NeedsAttention, the user has attended
        // to the app (e.g. opened Slack). Clear notification watcher counts
        // for this app so badge count reflects the acknowledged state.
        if (oldStatus == QLatin1String("NeedsAttention") && status != QLatin1String("NeedsAttention") && !it->id.isEmpty()) {
            clearUnreadNotifications(it->id);
        }
        updateSniChatState(*it);
        bumpRevision();
    } else {
        qCInfo(lcNotif) << "SNI NewStatus from unknown item:" << key << "status=" << status;
    }
}
void NotificationTracker::onSniNewTitle(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    auto it = m_sniItems.find(key);
    if (it != m_sniItems.end()) {
        ensureSniMetadataQueried(key, *it);
        querySniProperty(key, it->generation, message.service(), message.path(), QStringLiteral("Title"));
    }
}

void NotificationTracker::onSniNewAttentionIcon(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    auto it = m_sniItems.find(key);
    if (it != m_sniItems.end()) {
        ensureSniMetadataQueried(key, *it);
        querySniProperty(key, it->generation, message.service(), message.path(), QStringLiteral("AttentionIconName"));
    }
}

void NotificationTracker::onSniNewOverlayIcon(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    auto it = m_sniItems.find(key);
    if (it != m_sniItems.end()) {
        ensureSniMetadataQueried(key, *it);
        querySniProperty(key, it->generation, message.service(), message.path(), QStringLiteral("OverlayIconName"));
    }
}

void NotificationTracker::onSniNewToolTip(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    auto it = m_sniItems.find(key);
    if (it != m_sniItems.end()) {
        ensureSniMetadataQueried(key, *it);
        querySniProperty(key, it->generation, message.service(), message.path(), QStringLiteral("ToolTip"));
    }
}
void NotificationTracker::onSniNewIcon(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    if (m_sniItems.contains(key)) {
        scheduleSniIconRefresh(key, message.interface());
    }
}

void NotificationTracker::onSniNewIconThemePath(const QDBusMessage &message)
{
    const QString key = sniKeyForMessage(message);
    if (m_sniItems.contains(key)) {
        scheduleSniIconRefresh(key, message.interface());
    }
}


void NotificationTracker::onSniPropertiesChanged(const QString &interfaceName,
                                                  const QVariantMap &changedProperties,
                                                  const QStringList &invalidatedProperties,
                                                  const QDBusMessage &message)
{
    if (interfaceName != QLatin1String("org.kde.StatusNotifierItem")
        && interfaceName != QLatin1String("org.freedesktop.StatusNotifierItem")) {
        return;
    }
    const QString key = sniKeyForMessage(message);
    auto it = m_sniItems.find(key);
    if (it == m_sniItems.end()) {
        return;
    }
    QSet<QString> properties;
    for (auto changed = changedProperties.cbegin(); changed != changedProperties.cend(); ++changed) {
        properties.insert(changed.key());
    }
    for (const QString &property : invalidatedProperties) {
        properties.insert(property);
    }
    const QStringList iconProperties{QStringLiteral("IconPixmap"), QStringLiteral("IconName"), QStringLiteral("IconThemePath")};
    bool iconRefresh = false;
    for (const QString &property : properties) {
        if (iconProperties.contains(property)) {
            iconRefresh = true;
        } else if (property == QLatin1String("Id") || property == QLatin1String("Status") || property == QLatin1String("Title")
                   || property == QLatin1String("AttentionIconName") || property == QLatin1String("OverlayIconName")
                   || property == QLatin1String("ToolTip")) {
            querySniProperty(key, it->generation, message.service(), message.path(), property, interfaceName);
        }
    }
    if (iconRefresh) {
        scheduleSniIconRefresh(key, interfaceName);
    }
}

void NotificationTracker::onSniServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner)
{
    if (oldOwner.isEmpty()) {
        return;
    }

    const QString prefix = service + kSniKeySeparator;
    QStringList objectPaths;
    for (auto it = m_sniItems.cbegin(); it != m_sniItems.cend(); ++it) {
        if (it.key().startsWith(prefix)) {
            objectPaths.append(it.key().mid(prefix.size()));
        }
    }

    const bool changed = !objectPaths.isEmpty();
    for (const QString &objectPath : std::as_const(objectPaths)) {
        m_sniItems.remove(sniKey(service, objectPath));
    }
    if (changed) {
        bumpRevision();
    }

    if (newOwner.isEmpty()) {
        m_sniServiceWatcher.removeWatchedService(service);
        return;
    }

    for (const QString &objectPath : std::as_const(objectPaths)) {
        trackSniItem(service, objectPath);
    }
}
void NotificationTracker::onSniWatcherServiceOwnerChanged(const QString &service, const QString &, const QString &newOwner)
{
    if (service != QString::fromLatin1(kSniWatcherService)) {
        return;
    }
    ++m_sniWatcherGeneration;
    qCInfo(lcNotif) << "SNI watcher owner changed; clearing tracked items";
    clearSniItems();
    if (!newOwner.isEmpty()) {
        queryRegisteredSniItems();
    }
}

} // namespace krema
