// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QUrl>

namespace krema
{

/**
 * Task model for Hyprland.
 *
 * Implements window tracking via Hyprland IPC and provides roles
 * compatible with TaskManager::AbstractTasksModel.
 */
class HyprlandTasksModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        AppId = Qt::UserRole + 1,
        AppName,
        GenericName,
        LauncherUrl,
        LauncherUrlWithoutIcon,
        WinIdList,
        MimeType,
        MimeData,
        IsWindow,
        IsStartup,
        IsLauncher,
        HasLauncher,
        IsGroupParent,
        ChildCount,
        IsGroupable,
        IsActive,
        IsClosable,
        IsMovable,
        IsResizable,
        IsMaximizable,
        IsMaximized,
        IsMinimizable,
        IsMinimized,
        IsKeepAbove,
        IsKeepBelow,
        IsFullScreenable,
        IsFullScreen,
        IsShadeable,
        IsShaded,
        IsVirtualDesktopsChangeable,
        VirtualDesktops,
        IsOnAllVirtualDesktops,
        Geometry,
        ScreenGeometry,
        Activities,
        IsDemandingAttention,
        ActiveChildIndex,
    };

    explicit HyprlandTasksModel(QObject *parent = nullptr);
    ~HyprlandTasksModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Actions
    void requestActivate(int row);
    void requestNewInstance(int row);
    void requestClose(int row);
    void requestCycle(int row, bool forward);
    bool requestMove(int from, int to);

    // Launcher management (pinned apps)
    QStringList launcherList() const;
    void setLauncherList(const QStringList &launchers);

    /// Check if any window overlaps the given screen rect (used for DodgeWindows)
    bool hasOverlappingWindow(const QRect &dockRect, bool activeOnly = false) const;

private:
    void refresh();
    void handleEvent(const QString &name, const QString &data);

    struct WindowInfo {
        QString address;
        QString title;
        int workspace = 0;
        QRect geometry;
        bool isActive = false;
        bool isMinimized = false;
    };

    struct Task {
        QString appId;
        QString name;
        QList<WindowInfo> windows;
        bool isLauncher = false;
        QUrl launcherUrl;
    };

    QList<Task> m_tasks;
    QStringList m_pinnedLaunchers;
    QString m_activeWindowAddress;
};

} // namespace krema
