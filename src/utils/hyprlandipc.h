// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>

namespace krema
{

/**
 * Utility for communicating with the Hyprland compositor.
 *
 * Uses the Hyprland Unix sockets for commands and events.
 * Sockets are located at $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock
 */
class HyprlandIpc : public QObject
{
    Q_OBJECT

public:
    explicit HyprlandIpc(QObject *parent = nullptr);
    ~HyprlandIpc() override;

    static HyprlandIpc *self();

    /// Execute a hyprctl-style command and return the JSON response.
    QJsonArray sendCommand(const QString &cmd);

    /// Dispatch a command without waiting for a response (e.g. focuswindow).
    void dispatch(const QString &cmd);

    /// Get the list of all open windows (clients).
    QJsonArray getClients();

    /// Get the list of workspaces.
    QJsonArray getWorkspaces();

    /// Get the active window.
    QJsonObject getActiveWindow();

Q_SIGNALS:
    /// Emitted when a Hyprland event occurs (e.g. openWindow, activeWindow).
    void eventReceived(const QString &name, const QString &data);

private:
    void connectToEventSocket();
    void readEvents();

    QString m_socketPath;
    QString m_eventSocketPath;
    QPointer<QLocalSocket> m_eventSocket;

    static HyprlandIpc *s_instance;
};

} // namespace krema
