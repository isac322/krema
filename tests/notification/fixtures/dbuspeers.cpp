// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dbuspeers.h"
#include <QDBusMessage>

#include <utility>
namespace krema::test
{
namespace
{
constexpr auto kNotificationsService = "org.freedesktop.Notifications";
constexpr auto kNotificationsPath = "/org/freedesktop/Notifications";
constexpr auto kWatcherPath = "/NotificationWatcher";
constexpr auto kSniWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kSniWatcherPath = "/StatusNotifierWatcher";
}

NotificationServerPeer::NotificationServerPeer(QObject *parent)
    : QObject(parent)
    , m_bus(QDBusConnection::sessionBus())
{
}

NotificationServerPeer::~NotificationServerPeer()
{
    stop();
}

bool NotificationServerPeer::start()
{
    if (m_serviceRegistered && m_objectRegistered) {
        return true;
    }
    m_serviceRegistered = m_bus.registerService(QString::fromLatin1(kNotificationsService));
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
    if (!isRegistered()) {
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

StatusNotifierWatcherPeer::StatusNotifierWatcherPeer(QObject *parent)
    : QObject(parent)
    , m_bus(QDBusConnection::sessionBus())
{
}

StatusNotifierWatcherPeer::~StatusNotifierWatcherPeer()
{
    stop();
}

bool StatusNotifierWatcherPeer::start()
{
    if (m_serviceRegistered && m_objectRegistered) {
        return true;
    }
    m_serviceRegistered = m_bus.registerService(QString::fromLatin1(kSniWatcherService));
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
    : QObject(parent)
    , m_bus(QDBusConnection::sessionBus())
    , m_serviceName(std::move(serviceName))
    , m_objectPath(std::move(objectPath))
{
}

StatusNotifierItemPeer::~StatusNotifierItemPeer()
{
    stop();
}

bool StatusNotifierItemPeer::start()
{
    if (m_serviceRegistered && m_objectRegistered) {
        return true;
    }
    m_serviceRegistered = m_bus.registerService(m_serviceName);
    if (!m_serviceRegistered) {
        return false;
    }
    m_objectRegistered = m_bus.registerObject(m_objectPath,
                                              this,
                                              QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties);
    if (!m_objectRegistered) {
        m_bus.unregisterService(m_serviceName);
        m_serviceRegistered = false;
    }
    return m_objectRegistered;
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

} // namespace krema::test

#include "dbuspeers.moc"
