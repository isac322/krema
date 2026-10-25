// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dbuspeers.h"
#include <QDBusConnectionInterface>
#include <QDBusMessage>

#include <utility>
namespace krema::test
{

const QDBusArgument &operator>>(const QDBusArgument &argument, ToolTipPixmap &pixmap)
{
    argument.beginStructure();
    argument >> pixmap.width >> pixmap.height >> pixmap.data;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const ToolTipPixmap &pixmap)
{
    argument.beginStructure();
    argument << pixmap.width << pixmap.height << pixmap.data;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, ToolTipData &toolTip)
{
    argument.beginStructure();
    argument >> toolTip.iconName >> toolTip.iconPixmap >> toolTip.title >> toolTip.description;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const ToolTipData &toolTip)
{
    argument.beginStructure();
    argument << toolTip.iconName << toolTip.iconPixmap << toolTip.title << toolTip.description;
    argument.endStructure();
    return argument;
}
namespace
{
constexpr auto kNotificationsService = "org.freedesktop.Notifications";
constexpr auto kNotificationsPath = "/org/freedesktop/Notifications";
constexpr auto kWatcherPath = "/NotificationWatcher";
constexpr auto kSniWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kSniWatcherPath = "/StatusNotifierWatcher";
constexpr auto kFerdiumService = "org.ferdium.Ferdium";
constexpr auto kFerdiumPath = "/org/ferdium";
}


NotificationServerPeer::NotificationServerPeer(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
}

NotificationServerPeer::~NotificationServerPeer()
{
    stop();
}

bool NotificationServerPeer::start(bool replaceExisting)
{
    if (m_serviceRegistered && m_objectRegistered) {
        return true;
    }
    const auto registration =
        m_bus.interface()->registerService(QString::fromLatin1(kNotificationsService),
                                           replaceExisting ? QDBusConnectionInterface::ReplaceExistingService : QDBusConnectionInterface::DontQueueService,
                                           QDBusConnectionInterface::AllowReplacement);
    m_serviceRegistered = registration.isValid() && registration.value() == QDBusConnectionInterface::ServiceRegistered;
    if (!m_serviceRegistered) {
        return false;
    }
    m_objectRegistered = m_bus.registerObject(QString::fromLatin1(kNotificationsPath),
                                              QStringLiteral("org.kde.NotificationManager"),
                                              this,
                                              QDBusConnection::ExportScriptableSlots);
    if (!m_objectRegistered) {
        m_bus.unregisterService(QString::fromLatin1(kNotificationsService));
        m_serviceRegistered = false;
    }
    return m_objectRegistered;
}

void NotificationServerPeer::stop()
{
    if (m_objectRegistered) {
        m_bus.unregisterObject(QString::fromLatin1(kNotificationsPath));
        m_objectRegistered = false;
    }
    if (m_serviceRegistered) {
        m_bus.unregisterService(QString::fromLatin1(kNotificationsService));
        m_serviceRegistered = false;
    }
}

bool NotificationServerPeer::isRegistered() const
{
    return m_serviceRegistered && m_objectRegistered;
}

int NotificationServerPeer::registerWatcherCalls() const
{
    return m_registerWatcherCalls;
}

int NotificationServerPeer::unregisterWatcherCalls() const
{
    return m_unregisterWatcherCalls;
}

bool NotificationServerPeer::sendNotify(const QString &appName,
                                        uint id,
                                        uint replacesId,
                                        const QString &desktopEntry,
                                        const QString &summary,
                                        const QString &body,
                                        const QStringList &actions,
                                        int timeout,
                                        uint urgency) const
{
    if (!isRegistered() || m_registerWatcherCalls == 0) {
        return false;
    }
    QVariantMap hints;
    if (!desktopEntry.isEmpty()) {
        hints.insert(QStringLiteral("desktop-entry"), desktopEntry);
    }
    if (urgency > 0) {
        hints.insert(QStringLiteral("urgency"), urgency);
    }
    QDBusMessage message = QDBusMessage::createMethodCall(QDBusConnection::sessionBus().baseService(),
                                                           QString::fromLatin1(kWatcherPath),
                                                           QStringLiteral("org.kde.NotificationWatcher"),
                                                           QStringLiteral("Notify"));
    message << id << appName << replacesId << QStringLiteral("fixture-icon") << summary << body << actions << hints << timeout;
    return m_bus.send(message);
}

bool NotificationServerPeer::sendClose(uint id) const
{
    if (!isRegistered()) {
        return false;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(QDBusConnection::sessionBus().baseService(),
                                                           QString::fromLatin1(kWatcherPath),
                                                           QStringLiteral("org.kde.NotificationWatcher"),
                                                           QStringLiteral("CloseNotification"));
    message << id;
    return m_bus.send(message);
}

bool NotificationServerPeer::broadcastClose(uint id, uint reason) const
{
    if (!isRegistered()) {
        return false;
    }
    QDBusMessage signal = QDBusMessage::createSignal(QString::fromLatin1(kNotificationsPath),
                                                      QStringLiteral("org.freedesktop.Notifications"),
                                                      QStringLiteral("NotificationClosed"));
    signal << id << reason;
    return m_bus.send(signal);
}

void NotificationServerPeer::RegisterWatcher()
{
    ++m_registerWatcherCalls;
}

void NotificationServerPeer::UnRegisterWatcher()
{
    ++m_unregisterWatcherCalls;
}

StatusNotifierWatcherPeer::StatusNotifierWatcherPeer(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
}

StatusNotifierWatcherPeer::~StatusNotifierWatcherPeer()
{
    stop();
}

bool StatusNotifierWatcherPeer::start(bool replaceExisting)
{
    if (m_serviceRegistered && m_objectRegistered) {
        return true;
    }
    const auto registration =
        m_bus.interface()->registerService(QString::fromLatin1(kSniWatcherService),
                                           replaceExisting ? QDBusConnectionInterface::ReplaceExistingService : QDBusConnectionInterface::DontQueueService,
                                           QDBusConnectionInterface::AllowReplacement);
    m_serviceRegistered = registration.isValid() && registration.value() == QDBusConnectionInterface::ServiceRegistered;
    if (!m_serviceRegistered) {
        return false;
    }
    m_objectRegistered = m_bus.registerObject(QString::fromLatin1(kSniWatcherPath),
                                              this,
                                              QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties);
    if (!m_objectRegistered) {
        m_bus.unregisterService(QString::fromLatin1(kSniWatcherService));
        m_serviceRegistered = false;
    }
    return m_objectRegistered;
}

void StatusNotifierWatcherPeer::stop()
{
    if (m_objectRegistered) {
        m_bus.unregisterObject(QString::fromLatin1(kSniWatcherPath));
        m_objectRegistered = false;
    }
    if (m_serviceRegistered) {
        m_bus.unregisterService(QString::fromLatin1(kSniWatcherService));
        m_serviceRegistered = false;
    }
}

bool StatusNotifierWatcherPeer::isRegistered() const
{
    return m_serviceRegistered && m_objectRegistered;
}

void StatusNotifierWatcherPeer::setRegisteredItems(const QStringList &items)
{
    if (m_registeredItems == items) {
        return;
    }
    m_registeredItems = items;
    Q_EMIT registeredItemsChanged();
}

void StatusNotifierWatcherPeer::addItem(const QString &item)
{
    if (m_registeredItems.contains(item)) {
        return;
    }
    m_registeredItems.append(item);
    Q_EMIT registeredItemsChanged();
    Q_EMIT StatusNotifierItemRegistered(item);
}

void StatusNotifierWatcherPeer::removeItem(const QString &item)
{
    if (!m_registeredItems.removeOne(item)) {
        return;
    }
    Q_EMIT registeredItemsChanged();
    Q_EMIT StatusNotifierItemUnregistered(item);
}

QStringList StatusNotifierWatcherPeer::registeredItems() const
{
    return m_registeredItems;
}

StatusNotifierItemPeer::StatusNotifierItemPeer(QString serviceName, QString objectPath, QObject *parent)
    : StatusNotifierItemPeer(std::move(serviceName), std::move(objectPath), QDBusConnection::sessionBus(), parent)
{
}

StatusNotifierItemPeer::StatusNotifierItemPeer(QString serviceName,
                                               QString objectPath,
                                               const QDBusConnection &bus,
                                               QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_serviceName(std::move(serviceName))
    , m_objectPath(std::move(objectPath))
{
    qDBusRegisterMetaType<ToolTipPixmap>();
    qDBusRegisterMetaType<ToolTipData>();
    qDBusRegisterMetaType<QVector<ToolTipPixmap>>();
}

StatusNotifierItemPeer::~StatusNotifierItemPeer()
{
    stop();
}

bool StatusNotifierItemPeer::start(bool replaceExisting)
{
    if (m_serviceRegistered && m_objectRegistered) {
        return true;
    }
    if (!m_objectRegistered) {
        m_objectRegistered = m_bus.registerObject(m_objectPath,
                                                  this,
                                                  QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties);
    }
    if (!m_objectRegistered || !claimServiceWithoutObject(replaceExisting)) {
        if (m_objectRegistered && !m_serviceRegistered) {
            m_bus.unregisterObject(m_objectPath);
            m_objectRegistered = false;
        }
        return false;
    }
    Q_EMIT NewStatus(m_status);
    Q_EMIT NewTitle();
    Q_EMIT NewIcon();
    Q_EMIT NewIconThemePath();
    Q_EMIT NewAttentionIcon();
    Q_EMIT NewOverlayIcon();
    Q_EMIT NewToolTip();
    return true;
}

bool StatusNotifierItemPeer::claimServiceWithoutObject(bool replaceExisting)
{
    if (m_serviceRegistered) {
        return true;
    }
    const auto registration = m_bus.interface()->registerService(
        m_serviceName,
        replaceExisting ? QDBusConnectionInterface::ReplaceExistingService : QDBusConnectionInterface::DontQueueService,
        QDBusConnectionInterface::AllowReplacement);
    m_serviceRegistered = registration.isValid() && registration.value() == QDBusConnectionInterface::ServiceRegistered;
    return m_serviceRegistered;
}

bool StatusNotifierItemPeer::exportObjectAndEmitReady()
{
    if (!m_serviceRegistered) {
        return false;
    }
    if (!m_objectRegistered) {
        m_objectRegistered = m_bus.registerObject(m_objectPath,
                                                  this,
                                                  QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties);
    }
    if (!m_objectRegistered) {
        return false;
    }
    Q_EMIT NewStatus(m_status);
    Q_EMIT NewTitle();
    Q_EMIT NewIcon();
    Q_EMIT NewIconThemePath();
    Q_EMIT NewAttentionIcon();
    Q_EMIT NewOverlayIcon();
    Q_EMIT NewToolTip();
    return true;
}

void StatusNotifierItemPeer::stop()
{
    if (m_objectRegistered) {
        m_bus.unregisterObject(m_objectPath);
        m_objectRegistered = false;
    }
    if (m_serviceRegistered) {
        m_bus.unregisterService(m_serviceName);
        m_serviceRegistered = false;
    }
}

bool StatusNotifierItemPeer::isRegistered() const
{
    return m_serviceRegistered && m_objectRegistered;
}

QString StatusNotifierItemPeer::serviceName() const
{
    return m_serviceName;
}

QString StatusNotifierItemPeer::objectPath() const
{
    return m_objectPath;
}

QString StatusNotifierItemPeer::itemAddress() const
{
    return m_serviceName + m_objectPath;
}

QString StatusNotifierItemPeer::id() const
{
    return m_id;
}

QString StatusNotifierItemPeer::status() const
{
    return m_status;
}

QString StatusNotifierItemPeer::title() const
{
    return m_title;
}

QString StatusNotifierItemPeer::iconName() const
{
    return m_iconName;
}

QVector<ToolTipPixmap> StatusNotifierItemPeer::iconPixmap() const
{
    return m_iconPixmap;
}

QString StatusNotifierItemPeer::iconThemePath() const
{
    return m_iconThemePath;
}

QString StatusNotifierItemPeer::attentionIconName() const
{
    return m_attentionIconName;
}

QString StatusNotifierItemPeer::overlayIconName() const
{
    return m_overlayIconName;
}

QVariant StatusNotifierItemPeer::toolTip() const
{
    return m_toolTip;
}

void StatusNotifierItemPeer::setId(const QString &id)
{
    if (m_id == id) {
        return;
    }
    m_id = id;
    Q_EMIT idChanged();
}

void StatusNotifierItemPeer::setStatus(const QString &status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    Q_EMIT statusChanged();
    Q_EMIT NewStatus(status);
}

void StatusNotifierItemPeer::setTitle(const QString &title)
{
    if (m_title == title) {
        return;
    }
    m_title = title;
    Q_EMIT titleChanged();
    Q_EMIT NewTitle();
}

void StatusNotifierItemPeer::setIconName(const QString &name)
{
    if (m_iconName == name) {
        return;
    }
    m_iconName = name;
    Q_EMIT iconNameChanged();
    Q_EMIT NewIcon();
}

void StatusNotifierItemPeer::setIconPixmap(const QImage &image)
{
    QVector<ToolTipPixmap> pixmaps;
    if (!image.isNull()) {
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        ToolTipPixmap pixmap;
        pixmap.width = converted.width();
        pixmap.height = converted.height();
        pixmap.data.reserve(converted.width() * converted.height() * 4);
        for (int y = 0; y < converted.height(); ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x) {
                const QRgb pixel = line[x];
                pixmap.data.append(char(qAlpha(pixel)));
                pixmap.data.append(char(qRed(pixel)));
                pixmap.data.append(char(qGreen(pixel)));
                pixmap.data.append(char(qBlue(pixel)));
            }
        }
        pixmaps.append(std::move(pixmap));
    }

    if (m_iconPixmap.size() == pixmaps.size()) {
        bool equal = true;
        for (qsizetype i = 0; i < pixmaps.size(); ++i) {
            equal = equal && m_iconPixmap.at(i).width == pixmaps.at(i).width && m_iconPixmap.at(i).height == pixmaps.at(i).height
                && m_iconPixmap.at(i).data == pixmaps.at(i).data;
        }
        if (equal) {
            return;
        }
    }
    m_iconPixmap = std::move(pixmaps);
    Q_EMIT iconPixmapChanged();
    Q_EMIT NewIcon();
}
void StatusNotifierItemPeer::setIconPixmap(const QVector<ToolTipPixmap> &pixmaps)
{
    setRawIconPixmap(pixmaps);
}

void StatusNotifierItemPeer::setRawIconPixmap(const QVector<ToolTipPixmap> &pixmaps)
{
    if (m_iconPixmap.size() == pixmaps.size()) {
        bool equal = true;
        for (qsizetype i = 0; i < pixmaps.size(); ++i) {
            equal = equal && m_iconPixmap.at(i).width == pixmaps.at(i).width && m_iconPixmap.at(i).height == pixmaps.at(i).height
                && m_iconPixmap.at(i).data == pixmaps.at(i).data;
        }
        if (equal) {
            return;
        }
    }
    m_iconPixmap = pixmaps;
    Q_EMIT iconPixmapChanged();
    Q_EMIT NewIcon();
}

void StatusNotifierItemPeer::setIconThemePath(const QString &path)
{
    if (m_iconThemePath == path) {
        return;
    }
    m_iconThemePath = path;
    Q_EMIT iconThemePathChanged();
    Q_EMIT NewIconThemePath();
}

void StatusNotifierItemPeer::setAttentionIconName(const QString &name)
{
    if (m_attentionIconName == name) {
        return;
    }
    m_attentionIconName = name;
    Q_EMIT attentionIconNameChanged();
    Q_EMIT NewAttentionIcon();
}

void StatusNotifierItemPeer::setOverlayIconName(const QString &name)
{
    if (m_overlayIconName == name) {
        return;
    }
    m_overlayIconName = name;
    Q_EMIT overlayIconNameChanged();
    Q_EMIT NewOverlayIcon();
}

void StatusNotifierItemPeer::setToolTip(const QVariant &toolTip)
{
    if (m_toolTip == toolTip) {
        return;
    }
    m_toolTip = toolTip;
    Q_EMIT toolTipChanged();
    Q_EMIT NewToolTip();
}

FerdiumPeer::FerdiumPeer(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
}

FerdiumPeer::~FerdiumPeer()
{
    stop();
}

bool FerdiumPeer::start(bool replaceExisting)
{
    if (m_serviceRegistered && m_objectRegistered) {
        return true;
    }
    const auto registration =
        m_bus.interface()->registerService(QString::fromLatin1(kFerdiumService),
                                           replaceExisting ? QDBusConnectionInterface::ReplaceExistingService : QDBusConnectionInterface::DontQueueService,
                                           QDBusConnectionInterface::AllowReplacement);
    m_serviceRegistered = registration.isValid() && registration.value() == QDBusConnectionInterface::ServiceRegistered;
    if (!m_serviceRegistered) {
        return false;
    }
    m_objectRegistered = m_bus.registerObject(QString::fromLatin1(kFerdiumPath),
                                              this,
                                              QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties);
    if (!m_objectRegistered) {
        m_bus.unregisterService(QString::fromLatin1(kFerdiumService));
        m_serviceRegistered = false;
    }
    return m_objectRegistered;
}

void FerdiumPeer::stop()
{
    if (m_objectRegistered) {
        m_bus.unregisterObject(QString::fromLatin1(kFerdiumPath));
        m_objectRegistered = false;
    }
    if (m_serviceRegistered) {
        m_bus.unregisterService(QString::fromLatin1(kFerdiumService));
        m_serviceRegistered = false;
    }
}

bool FerdiumPeer::isRegistered() const
{
    return m_serviceRegistered && m_objectRegistered;
}

uint FerdiumPeer::unreadDirectMessageCount() const
{
    return m_unreadDirectMessageCount;
}

uint FerdiumPeer::unreadIndirectMessageCount() const
{
    return m_unreadIndirectMessageCount;
}

void FerdiumPeer::setUnreadDirectMessageCount(uint count)
{
    if (m_unreadDirectMessageCount == count) {
        return;
    }
    m_unreadDirectMessageCount = count;
    Q_EMIT unreadDirectMessageCountChanged();
    if (m_objectRegistered) {
        QDBusMessage signal = QDBusMessage::createSignal(QString::fromLatin1(kFerdiumPath),
                                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                                         QStringLiteral("PropertiesChanged"));
        signal << QStringLiteral("org.ferdium.Ferdium")
               << QVariantMap{{QStringLiteral("UnreadDirectMessageCount"), m_unreadDirectMessageCount}}
               << QStringList{};
        m_bus.send(signal);
    }
}

void FerdiumPeer::setUnreadIndirectMessageCount(uint count)
{
    if (m_unreadIndirectMessageCount == count) {
        return;
    }
    m_unreadIndirectMessageCount = count;
    Q_EMIT unreadIndirectMessageCountChanged();
    if (m_objectRegistered) {
        QDBusMessage signal = QDBusMessage::createSignal(QString::fromLatin1(kFerdiumPath),
                                                         QStringLiteral("org.freedesktop.DBus.Properties"),
                                                         QStringLiteral("PropertiesChanged"));
        signal << QStringLiteral("org.ferdium.Ferdium")
               << QVariantMap{{QStringLiteral("UnreadIndirectMessageCount"), m_unreadIndirectMessageCount}}
               << QStringList{};
        m_bus.send(signal);
    }
}

} // namespace krema::test

