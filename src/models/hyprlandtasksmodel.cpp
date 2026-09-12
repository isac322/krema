// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "hyprlandtasksmodel.h"
#include "utils/debugmanager.h"
#include "utils/hyprlandipc.h"
#include "utils/identitymanager.h"

#include <KService>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcHyprModel, "krema.models.hyprlandtasks")

namespace krema
{

HyprlandTasksModel::HyprlandTasksModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(HyprlandIpc::self(), &HyprlandIpc::eventReceived, this, &HyprlandTasksModel::handleEvent);
    refresh();
}

HyprlandTasksModel::~HyprlandTasksModel() = default;

int HyprlandTasksModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_tasks.size();
}

QVariant HyprlandTasksModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tasks.size())
        return {};

    const Task &task = m_tasks[index.row()];

    bool hasWindows = !task.windows.isEmpty();
    bool isActive = false;
    bool isMinimized = true;
    bool isOnAllDesktops = false;
    QRect geometry;
    QVariantList winIds;
    QVariantList workspaces;

    for (const WindowInfo &w : task.windows) {
        winIds.append(w.address);
        workspaces.append(w.workspace);
        if (w.isActive)
            isActive = true;
        if (!w.isMinimized)
            isMinimized = false;
        geometry = w.geometry; // Use the last one or active one ideally
        if (w.workspace == -1)
            isOnAllDesktops = true;
    }

    if (!hasWindows)
        isMinimized = false;

    switch (role) {
    case Qt::DisplayRole:
    case AppName:
        return task.name;
    case AppId:
        return task.appId;
    case LauncherUrl:
    case LauncherUrlWithoutIcon:
        return task.launcherUrl;
    case IsWindow:
        return hasWindows;
    case IsLauncher:
        if (DebugManager::self()->modelEnabled()) {
            DebugManager::self()->model(QStringLiteral("[MODEL] IsLauncher for row %1: %2 name: %3")
                                            .arg(index.row())
                                            .arg(task.isLauncher ? QStringLiteral("true") : QStringLiteral("false"))
                                            .arg(task.name));
        }
        return task.isLauncher;
    case IsActive:
        return isActive;
    case IsMinimized:
        return isMinimized;
    case ChildCount:
        return task.windows.size();
    case WinIdList:
        return winIds;
    case VirtualDesktops:
        return workspaces;
    case IsOnAllVirtualDesktops:
        return isOnAllDesktops;
    case Geometry:
        return geometry;
    case IsDemandingAttention:
        return false; // TODO
    case ActiveChildIndex: {
        for (int i = 0; i < task.windows.size(); ++i) {
            if (task.windows[i].isActive)
                return i;
        }
        return -1;
    }
    }

    return {};
}

QHash<int, QByteArray> HyprlandTasksModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[AppId] = "AppId";
    roles[AppName] = "AppName";
    roles[LauncherUrl] = "LauncherUrl";
    roles[LauncherUrlWithoutIcon] = "LauncherUrlWithoutIcon";
    roles[WinIdList] = "WinIdList";
    roles[MimeType] = "MimeType";
    roles[MimeData] = "MimeData";
    roles[IsWindow] = "IsWindow";
    roles[IsStartup] = "IsStartup";
    roles[IsLauncher] = "IsLauncher";
    roles[HasLauncher] = "HasLauncher";
    roles[IsGroupParent] = "IsGroupParent";
    roles[ChildCount] = "ChildCount";
    roles[IsGroupable] = "IsGroupable";
    roles[IsActive] = "IsActive";
    roles[IsClosable] = "IsClosable";
    roles[IsMovable] = "IsMovable";
    roles[IsResizable] = "IsResizable";
    roles[IsMaximizable] = "IsMaximizable";
    roles[IsMaximized] = "IsMaximized";
    roles[IsMinimizable] = "IsMinimizable";
    roles[IsMinimized] = "IsMinimized";
    roles[IsKeepAbove] = "IsKeepAbove";
    roles[IsKeepBelow] = "IsKeepBelow";
    roles[IsFullScreenable] = "IsFullScreenable";
    roles[IsFullScreen] = "IsFullScreen";
    roles[IsShadeable] = "IsShadeable";
    roles[IsShaded] = "IsShaded";
    roles[IsVirtualDesktopsChangeable] = "IsVirtualDesktopsChangeable";
    roles[VirtualDesktops] = "VirtualDesktops";
    roles[IsOnAllVirtualDesktops] = "IsOnAllVirtualDesktops";
    roles[Geometry] = "Geometry";
    roles[ScreenGeometry] = "ScreenGeometry";
    roles[Activities] = "Activities";
    roles[IsDemandingAttention] = "IsDemandingAttention";
    roles[ActiveChildIndex] = "ActiveChildIndex";
    return roles;
}

void HyprlandTasksModel::requestActivate(int row)
{
    if (row < 0 || row >= m_tasks.size())
        return;
    const Task &task = m_tasks[row];
    qCInfo(lcHyprModel) << "Activating task:" << task.appId << "windows:" << task.windows.size();

    if (task.windows.isEmpty()) {
        requestNewInstance(row);
    } else {
        // Find active window or just focus the first
        QString address = task.windows.first().address;
        for (const WindowInfo &w : task.windows) {
            if (w.isActive) {
                // If it's already active, we stay here for focus
                address = w.address;
                break;
            }
        }
        qCInfo(lcHyprModel) << "Focusing window address:" << address;
        HyprlandIpc::self()->dispatch(QStringLiteral("focuswindow address:") + address);
    }
}

void HyprlandTasksModel::requestCycle(int row, bool forward)
{
    if (row < 0 || row >= m_tasks.size())
        return;
    const Task &task = m_tasks[row];
    if (task.windows.size() <= 1) {
        requestActivate(row);
        return;
    }

    int activeIdx = -1;
    for (int i = 0; i < task.windows.size(); ++i) {
        if (task.windows[i].isActive) {
            activeIdx = i;
            break;
        }
    }

    int targetIdx;
    if (activeIdx < 0) {
        targetIdx = 0;
    } else {
        targetIdx = forward ? (activeIdx + 1) % task.windows.size() : (activeIdx - 1 + task.windows.size()) % task.windows.size();
    }

    QString address = task.windows[targetIdx].address;
    qCInfo(lcHyprModel) << "Cycling window to:" << address << "(index:" << targetIdx << ")";
    HyprlandIpc::self()->dispatch(QStringLiteral("focuswindow address:") + address);
}

void HyprlandTasksModel::requestNewInstance(int row)
{
    if (row < 0 || row >= m_tasks.size())
        return;
    const Task &task = m_tasks[row];

    if (!task.launcherUrl.isValid()) {
        qCWarning(lcHyprModel) << "Cannot launch task with invalid URL:" << task.appId;
        return;
    }

    qCInfo(lcHyprModel) << "Requesting new instance for:" << task.launcherUrl;

    QString desktopId = task.launcherUrl.scheme() == QLatin1String("applications") ? task.launcherUrl.path() : task.launcherUrl.fileName();

    KService::Ptr service = KService::serviceByStorageId(desktopId);
    if (service) {
        QString exec = service->exec();
        // Remove field codes (%u, %f, etc) as we are launching a new instance without specific files
        static const QRegularExpression fieldCodes(QStringLiteral("%[fFuUnNmicck]"));
        exec.remove(fieldCodes);

        QStringList args = QProcess::splitCommand(exec.trimmed());
        if (!args.isEmpty()) {
            QString program = args.takeFirst();
            qCInfo(lcHyprModel) << "Launching via KService exec:" << program << args;
            if (QProcess::startDetached(program, args)) {
                return;
            }
        }
        qCWarning(lcHyprModel) << "Failed to start process from exec line:" << exec;
    }

    // Fallback for non-service URLs or if service execution failed
    QUrl url = task.launcherUrl;
    if (url.scheme() == QLatin1String("applications")) {
        QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, url.path());
        if (!path.isEmpty()) {
            url = QUrl::fromLocalFile(path);
        }
    }

    qCInfo(lcHyprModel) << "Falling back to QDesktopServices for:" << url;
    QDesktopServices::openUrl(url);
}

void HyprlandTasksModel::requestClose(int row)
{
    if (row < 0 || row >= m_tasks.size())
        return;
    const Task &task = m_tasks[row];
    for (const WindowInfo &w : task.windows) {
        HyprlandIpc::self()->dispatch(QStringLiteral("closewindow address:") + w.address);
    }
}

QStringList HyprlandTasksModel::launcherList() const
{
    return m_pinnedLaunchers;
}

void HyprlandTasksModel::setLauncherList(const QStringList &launchers)
{
    m_pinnedLaunchers = launchers;
    refresh();
}

bool HyprlandTasksModel::hasOverlappingWindow(const QRect &dockRect, bool activeOnly) const
{
    if (!dockRect.isValid())
        return false;

    for (const Task &task : m_tasks) {
        for (const WindowInfo &w : task.windows) {
            if (activeOnly && !w.isActive)
                continue;
            if (w.isMinimized)
                continue;
            if (w.geometry.intersects(dockRect))
                return true;
        }
    }
    return false;
}

void HyprlandTasksModel::refresh()
{
    beginResetModel();
    m_tasks.clear();

    // 1. Add pinned launchers
    for (const QString &urlStr : m_pinnedLaunchers) {
        QUrl url = IdentityManager::canonicalLauncherUrl(QUrl(urlStr));
        Task t;
        t.isLauncher = true;
        t.launcherUrl = url;
        t.appId = IdentityManager::appIdFromUrl(url);

        QString desktopId = url.path(); // canonicalLauncherUrl ensures applications: scheme
        KService::Ptr service = KService::serviceByStorageId(desktopId);
        if (service) {
            t.name = service->name();
        } else {
            t.name = IdentityManager::stripDesktopSuffix(desktopId);
        }
        m_tasks.append(t);
    }

    // 2. Add running windows and group them
    QJsonArray clients = HyprlandIpc::self()->getClients();
    for (const QJsonValue &v : clients) {
        QJsonObject obj = v.toObject();

        QString rawClass = obj[QLatin1String("class")].toString();
        QString appId = IdentityManager::normalizeAppId(rawClass);
        if (appId.isEmpty())
            continue; // Ignore windows with no class

        QUrl windowLauncherUrl = IdentityManager::canonicalLauncherUrl(QUrl(QStringLiteral("applications:") + appId + QLatin1String(".desktop")));

        WindowInfo w;
        w.address = obj[QLatin1String("address")].toString();
        w.title = obj[QLatin1String("title")].toString();
        w.workspace = obj[QLatin1String("workspace")].toObject()[QLatin1String("id")].toInt();
        w.isActive = obj[QLatin1String("focusHistoryID")].toInt() == 0;
        w.geometry = QRect(obj[QLatin1String("at")].toArray()[0].toInt(),
                           obj[QLatin1String("at")].toArray()[1].toInt(),
                           obj[QLatin1String("size")].toArray()[0].toInt(),
                           obj[QLatin1String("size")].toArray()[1].toInt());

        // Find existing task to group into
        bool merged = false;
        for (int i = 0; i < m_tasks.size(); ++i) {
            bool appIdMatch = m_tasks[i].appId.compare(appId, Qt::CaseInsensitive) == 0 || m_tasks[i].appId.compare(rawClass, Qt::CaseInsensitive) == 0;
            bool urlMatch = m_tasks[i].launcherUrl.isValid() && m_tasks[i].launcherUrl == windowLauncherUrl;

            if (appIdMatch || urlMatch) {
                m_tasks[i].windows.append(w);
                if (m_tasks[i].name.isEmpty() || m_tasks[i].name == m_tasks[i].launcherUrl.fileName()) {
                    m_tasks[i].name = w.title; // Update name to active window title
                }
                merged = true;
                break;
            }
        }

        if (!merged) {
            Task t;
            t.isLauncher = false;
            t.appId = appId;
            t.name = w.title;
            // Attempt to resolve launcher URL from appId to support middle-click 'New Instance'
            t.launcherUrl = IdentityManager::canonicalLauncherUrl(QUrl(QStringLiteral("applications:") + appId + QStringLiteral(".desktop")));
            t.windows.append(w);
            m_tasks.append(t);
        }
    }

    endResetModel();
}

void HyprlandTasksModel::handleEvent(const QString &name, const QString &data)
{
    Q_UNUSED(data);
    // Refresh on any relevant window or workspace event
    if (name == QLatin1String("openwindow") || name == QLatin1String("closewindow") || name == QLatin1String("activewindow")
        || name == QLatin1String("movewindow") || name == QLatin1String("windowtitle") || name == QLatin1String("fullscreen")
        || name == QLatin1String("changefloatingmode") || name == QLatin1String("pin") || name == QLatin1String("minimize")
        || name == QLatin1String("workspace") || name == QLatin1String("focusedmon") || name == QLatin1String("urgent")) {
        refresh();
    }
}

} // namespace krema
