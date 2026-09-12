// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "hyprlandipc.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(lcHyprIpc, "krema.utils.hyprlandipc")

namespace krema
{

HyprlandIpc *HyprlandIpc::s_instance = nullptr;

HyprlandIpc::HyprlandIpc(QObject *parent)
    : QObject(parent)
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString signature = env.value(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"));
    const QString runtimeDir = env.value(QStringLiteral("XDG_RUNTIME_DIR"), QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));

    if (signature.isEmpty()) {
        qCWarning(lcHyprIpc) << "HYPRLAND_INSTANCE_SIGNATURE not found!";
        return;
    }

    const QString base = runtimeDir + QStringLiteral("/hypr/") + signature;
    m_socketPath = base + QStringLiteral("/.socket.sock");
    m_eventSocketPath = base + QStringLiteral("/.socket2.sock");

    connectToEventSocket();
}

HyprlandIpc::~HyprlandIpc()
{
    if (m_eventSocket) {
        m_eventSocket->close();
    }
}

HyprlandIpc *HyprlandIpc::self()
{
    if (!s_instance) {
        s_instance = new HyprlandIpc(qApp);
    }
    return s_instance;
}

QJsonArray HyprlandIpc::sendCommand(const QString &cmd)
{
    QLocalSocket socket;
    socket.connectToServer(m_socketPath);
    if (!socket.waitForConnected(100)) {
        return {};
    }

    // Hyprland expects -j for JSON output
    socket.write((QStringLiteral("j/") + cmd).toUtf8());
    socket.flush();

    QByteArray response;
    while (socket.waitForReadyRead(500)) {
        response += socket.readAll();
    }

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isArray()) {
        return doc.array();
    } else if (doc.isObject()) {
        QJsonArray arr;
        arr.append(doc.object());
        return arr;
    }

    return {};
}

void HyprlandIpc::dispatch(const QString &cmd)
{
    qCInfo(lcHyprIpc) << "Dispatching command via socket:" << cmd;

    QLocalSocket socket;
    socket.connectToServer(m_socketPath);
    if (!socket.waitForConnected(100)) {
        qCWarning(lcHyprIpc) << "Failed to connect to Hyprland IPC socket for dispatch";
        return;
    }

    socket.write((QStringLiteral("/dispatch ") + cmd).toUtf8());
    socket.flush();
    socket.waitForReadyRead(100);
    QByteArray response = socket.readAll().trimmed();

    if (!response.isEmpty() && response != "ok") {
        qCWarning(lcHyprIpc) << "Hyprland dispatch error response:" << response;

        // Auto-fallback for Lua-based Hyprland plugins (e.g. hl.dispatch interception)
        if (response.contains("lua") || response.contains("hl.dispatch")) {
            if (cmd.startsWith(QLatin1String("focuswindow address:"))) {
                QString address = cmd.mid(20);
                QString luaCmd = QStringLiteral("hl.dsp.focus({window=\"address:") + address + QStringLiteral("\", follow=false})");
                qCInfo(lcHyprIpc) << "Retrying dispatch with Lua syntax:" << luaCmd;

                QLocalSocket retrySocket;
                retrySocket.connectToServer(m_socketPath);
                if (retrySocket.waitForConnected(100)) {
                    retrySocket.write((QStringLiteral("/dispatch ") + luaCmd).toUtf8());
                    retrySocket.flush();
                    retrySocket.waitForReadyRead(100);
                    QByteArray retryResponse = retrySocket.readAll().trimmed();
                    qCInfo(lcHyprIpc) << "Lua fallback response:" << retryResponse;
                } else {
                    qCWarning(lcHyprIpc) << "Failed to connect to Hyprland IPC socket for fallback retry";
                }
            } else if (cmd.startsWith(QLatin1String("closewindow address:"))) {
                QString address = cmd.mid(20);
                QString luaCmd = QStringLiteral("hl.dsp.window.close({window=\"address:") + address + QStringLiteral("\"})");
                qCInfo(lcHyprIpc) << "Retrying close dispatch with Lua syntax:" << luaCmd;

                QLocalSocket retrySocket;
                retrySocket.connectToServer(m_socketPath);
                if (retrySocket.waitForConnected(100)) {
                    retrySocket.write((QStringLiteral("/dispatch ") + luaCmd).toUtf8());
                    retrySocket.flush();
                    retrySocket.waitForReadyRead(100);
                    QByteArray retryResponse = retrySocket.readAll().trimmed();
                    qCInfo(lcHyprIpc) << "Lua fallback response:" << retryResponse;
                } else {
                    qCWarning(lcHyprIpc) << "Failed to connect to Hyprland IPC socket for fallback retry";
                }
            }
        }
    }
}

QJsonArray HyprlandIpc::getClients()
{
    return sendCommand(QStringLiteral("clients"));
}

QJsonArray HyprlandIpc::getWorkspaces()
{
    return sendCommand(QStringLiteral("workspaces"));
}

QJsonObject HyprlandIpc::getActiveWindow()
{
    QJsonArray arr = sendCommand(QStringLiteral("activewindow"));
    if (!arr.isEmpty()) {
        return arr.first().toObject();
    }
    return {};
}

void HyprlandIpc::connectToEventSocket()
{
    m_eventSocket = new QLocalSocket(this);
    connect(m_eventSocket, &QLocalSocket::readyRead, this, &HyprlandIpc::readEvents);
    connect(m_eventSocket, &QLocalSocket::disconnected, this, [this]() {
        // Retry connection after a delay if disconnected
        QTimer::singleShot(1000, this, &HyprlandIpc::connectToEventSocket);
    });

    m_eventSocket->connectToServer(m_eventSocketPath);
}

void HyprlandIpc::readEvents()
{
    if (!m_eventSocket)
        return;

    while (m_eventSocket->canReadLine()) {
        QByteArray line = m_eventSocket->readLine().trimmed();
        int separator = line.indexOf(">>");
        if (separator != -1) {
            QString name = QString::fromUtf8(line.left(separator));
            QString data = QString::fromUtf8(line.mid(separator + 2));
            Q_EMIT eventReceived(name, data);
        }
    }
}

} // namespace krema
