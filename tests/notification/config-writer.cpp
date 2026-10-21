// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    const bool clear = arguments.size() == 2 && arguments.at(1) == QStringLiteral("--clear");
    if (!clear && (arguments.size() != 3 || arguments.at(1) != QStringLiteral("--until"))) {
        qCritical("Usage: krema_notification_config_writer --until <ISO8601 datetime> | --clear");
        return 2;
    }

    QDateTime until;
    if (!clear) {
        until = QDateTime::fromString(arguments.at(2), Qt::ISODateWithMs);
        if (!until.isValid()) {
            qCritical("Until must be a valid ISO8601 datetime");
            return 2;
        }
    }

    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCritical("Cannot connect to the session D-Bus");
        return 1;
    }
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmanotifyrc"), KConfig::NoGlobals);
    KConfigGroup dnd = config->group(QStringLiteral("DoNotDisturb"));
    if (clear) {
        dnd.deleteEntry(QStringLiteral("Until"), KConfig::Notify);
    } else {
        dnd.writeEntry(QStringLiteral("Until"), until.toUTC(), KConfig::Notify);
    }
    if (!config->sync()) {
        qCritical("Cannot sync plasmanotifyrc");
        return 1;
    }

    // KConfig queues its notification on this same session connection. A
    // blocking daemon round trip drains that send before this process exits.
    const auto request = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                        QStringLiteral("/org/freedesktop/DBus"),
                                                        QStringLiteral("org.freedesktop.DBus"),
                                                        QStringLiteral("GetId"));
    const auto reply = bus.call(request, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qCritical().noquote() << "Session D-Bus flush barrier failed:" << reply.errorMessage();
        return 1;
    }
    return 0;
}
