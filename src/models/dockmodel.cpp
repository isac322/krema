// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/tasktools.h>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QScreen>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcModel, "krema.model")

namespace krema
{

DockModel::DockModel(QObject *parent)
    : QObject(parent)
    , m_tasksModel(std::make_unique<TaskManager::TasksModel>(this))
    , m_virtualDesktopInfo(std::make_shared<TaskManager::VirtualDesktopInfo>(this))
    , m_activityInfo(std::make_shared<TaskManager::ActivityInfo>(this))
{
    // TasksModel implements QQmlParserStatus. When created from C++ (not QML),
    // classBegin/componentComplete are not called by the QML engine.
    // We must call them manually to trigger internal model initialization.
    //
    // CRITICAL: Mirror QML's initialization order:
    //   1. classBegin()
    //   2. All properties set (including virtualDesktop, activity, screenGeometry)
    //   3. componentComplete() — activates internal source models
    //
    // Setting virtualDesktop/activity/screenGeometry AFTER componentComplete()
    // causes the Wayland window backend to miss running windows.
    m_tasksModel->classBegin();

    // Configure for dock-style behavior:
    // - Group windows by application (pinned + running merged)
    // - Manual sort for drag reordering
    // - Hide activated launchers (avoid duplicates: pinned + running)
    m_tasksModel->setGroupMode(TaskManager::TasksModel::GroupApplications);
    m_tasksModel->setSortMode(TaskManager::TasksModel::SortManual);
    m_tasksModel->setHideActivatedLaunchers(true);
    m_tasksModel->setSeparateLaunchers(false);
    m_tasksModel->setLaunchInPlace(true);
    m_tasksModel->setGroupInline(false);
    m_tasksModel->setTaskReorderingEnabled(true);

    // VirtualDesktopInfo and ActivityInfo are required for the Wayland
    // window backend to properly detect running windows on KDE Plasma.
    // These MUST be set BEFORE componentComplete() — mirrors QML property binding order.
    m_tasksModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
    m_tasksModel->setActivity(m_activityInfo->currentActivity());

    // Screen geometry — Plasma Task Manager always sets this.
    if (auto *screen = QGuiApplication::primaryScreen()) {
        m_tasksModel->setScreenGeometry(screen->geometry());
    }

    // Show all windows regardless of desktop/screen/activity.
    // Filtering can be enabled later (M8: multi-monitor + virtual desktop).
    m_tasksModel->setFilterByVirtualDesktop(false);
    m_tasksModel->setFilterByScreen(false);
    m_tasksModel->setFilterByActivity(false);
    m_tasksModel->setFilterHidden(false);

    // NOW activate internal source models (launcher model, window model, etc.)
    // All properties are set, so the backends will correctly discover running windows.
    m_tasksModel->componentComplete();

    // Track desktop/activity changes so the model stays up to date.
    connect(m_virtualDesktopInfo.get(), &TaskManager::VirtualDesktopInfo::currentDesktopChanged, this, [this]() {
        m_tasksModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
        Q_EMIT currentDesktopChanged();
    });
    connect(m_activityInfo.get(), &TaskManager::ActivityInfo::currentActivityChanged, this, [this]() {
        m_tasksModel->setActivity(m_activityInfo->currentActivity());
    });

    // Debug logging for model row changes
    connect(m_tasksModel.get(), &QAbstractItemModel::rowsInserted, this, [this]() {
        qCDebug(lcModel) << "Model rows after insert:" << m_tasksModel->rowCount();
    });
    connect(m_tasksModel.get(), &QAbstractItemModel::rowsRemoved, this, [this]() {
        qCDebug(lcModel) << "Model rows after remove:" << m_tasksModel->rowCount();
    });
}

DockModel::~DockModel() = default;

TaskManager::TasksModel *DockModel::tasksModel() const
{
    return m_tasksModel.get();
}

TaskManager::VirtualDesktopInfo *DockModel::virtualDesktopInfo() const
{
    return m_virtualDesktopInfo.get();
}

TaskManager::ActivityInfo *DockModel::activityInfo() const
{
    return m_activityInfo.get();
}

QStringList DockModel::pinnedLaunchers() const
{
    return m_tasksModel->launcherList();
}

void DockModel::setPinnedLaunchers(const QStringList &launchers)
{
    m_tasksModel->setLauncherList(launchers);
    Q_EMIT pinnedLaunchersChanged();
}

QString DockModel::iconName(int index) const
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }

    // 1. Theme name from the model's decoration role. This works for pinned
    //    launchers (icon resolved from the .desktop file) but is usually
    //    EMPTY for window tasks: their icon arrives via the Wayland
    //    window-management pipe as a serialized pixmap QIcon with no theme
    //    name (e.g. apps launched from krunner or windows opened by the app
    //    itself).
    const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
    if (!icon.name().isEmpty()) {
        return icon.name();
    }

    // 2. Resolve from the task's launcher URL (.desktop file) — same approach
    //    Plasma's taskmanager uses for window tasks without a themed icon.
    const QUrl launcherUrl = idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    if (launcherUrl.isValid()) {
        const auto appData = TaskManager::appDataFromUrl(launcherUrl, icon);
        if (!appData.icon.isNull() && !appData.icon.name().isEmpty()) {
            return appData.icon.name();
        }
    }

    // 3. Fall back to the app id from the URL (may itself be an icon name for
    //    applications: URLs, e.g. "applications:org.kde.dolphin.desktop").
    if (launcherUrl.isValid()) {
        QString appId = launcherUrl.fileName();
        if (appId.endsWith(QLatin1String(".desktop"))) {
            appId.chop(8);
        }
        if (!appId.isEmpty() && QIcon::hasThemeIcon(appId)) {
            return appId;
        }
    }

    // 4. Icon from the .desktop file. Browser-created web apps (Vivaldi/Chrome
    //    "Install as app") point Icon= at a PNG file — not a theme name.
    //    TaskIconProvider handles absolute paths via QIcon(path).
    //    LauncherUrlWithoutIcon is usually an applications: URL — resolve it
    //    to the real file path first.
    if (launcherUrl.isValid()) {
        QString desktopPath;
        if (launcherUrl.scheme() == QLatin1String("applications")) {
            // applications:org.kde.dolphin.desktop → locate in app dirs
            desktopPath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, launcherUrl.fileName());
        } else if (launcherUrl.isLocalFile()) {
            desktopPath = launcherUrl.toLocalFile();
        }
        if (!desktopPath.isEmpty() && desktopPath.endsWith(QLatin1String(".desktop"))) {
            const QString iconFromDesktop = iconFromDesktopFile(desktopPath);
            if (!iconFromDesktop.isEmpty()) {
                return iconFromDesktop;
            }
        }
    }

    // 5. Match by StartupWMClass: browser web-app windows report a Wayland
    //    app_id like "crx_<extension-id>" (from StartupWMClass in the .desktop
    //    file) which has no theme icon. Scan user + system application dirs
    //    for a .desktop file whose StartupWMClass matches and use its Icon=.
    const QString appIdRole = idx.data(TaskManager::AbstractTasksModel::AppId).toString();
    if (appIdRole.startsWith(QLatin1String("crx_"))) {
        const QString iconByWmClass = iconByStartupWMClass(appIdRole);
        if (!iconByWmClass.isEmpty()) {
            return iconByWmClass;
        }
    }

    return {};
}

QString DockModel::iconFromDesktopFile(const QString &desktopFile) const
{
    QFile file(desktopFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1String("Icon="))) {
            const QString iconValue = line.mid(5);
            if (iconValue.startsWith(QLatin1Char('/')) && QFile::exists(iconValue)) {
                return iconValue; // absolute path (web-app PNG etc.)
            }
            if (!iconValue.isEmpty() && QIcon::hasThemeIcon(iconValue)) {
                return iconValue; // theme name
            }
            break;
        }
    }
    return {};
}

QString DockModel::iconByStartupWMClass(const QString &wmClass) const
{
    const QStringList dirs = {
        QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation),
        QStringLiteral("/usr/share/applications"),
        QStringLiteral("/usr/local/share/applications"),
    };
    for (const QString &dir : dirs) {
        QDir appDir(dir);
        const auto entries = appDir.entryList(QStringList() << QStringLiteral("*.desktop"), QDir::Files);
        for (const QString &entry : entries) {
            QFile file(appDir.filePath(entry));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.startsWith(QLatin1String("StartupWMClass=")) && line.mid(15) == wmClass) {
                    file.close();
                    return iconFromDesktopFile(appDir.filePath(entry));
                }
            }
        }
    }
    return {};
}

QUrl DockModel::launcherUrl(int index) const
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
}

bool DockModel::isDesktopFile(const QUrl &url) const
{
    if (url.scheme() == QLatin1String("applications")) {
        return true;
    }
    if (url.isLocalFile() && url.toLocalFile().endsWith(QLatin1String(".desktop"))) {
        return true;
    }
    return false;
}

bool DockModel::isPinned(int index) const
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return false;
    }

    const QUrl url = idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    return url.isValid() && m_tasksModel->launcherList().contains(url.toString());
}

QVariantList DockModel::windowIds(int index) const
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(TaskManager::AbstractTasksModel::WinIdList).toList();
}

int DockModel::childCount(int index) const
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return 0;
    }
    return m_tasksModel->rowCount(idx);
}

QModelIndex DockModel::taskModelIndex(int index) const
{
    return m_tasksModel->index(index, 0);
}

void DockModel::publishDelegateGeometry(int index, const QRectF &globalRect, QObject *delegate)
{
    if (globalRect.isEmpty()) {
        return;
    }

    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    // Only window/group tasks have icon geometry that KWin can animate into.
    // Launchers and startup tasks have no window to minimize.
    const bool isLauncher = idx.data(TaskManager::AbstractTasksModel::IsLauncher).toBool();
    const bool isStartup = idx.data(TaskManager::AbstractTasksModel::IsStartup).toBool();
    if (isLauncher || isStartup) {
        return;
    }

    m_tasksModel->requestPublishDelegateGeometry(idx, globalRect.toRect(), delegate);
}

QString DockModel::appId(int index) const
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(TaskManager::AbstractTasksModel::AppId).toString();
}

int DockModel::virtualDesktopMode() const
{
    return m_virtualDesktopMode;
}

void DockModel::setVirtualDesktopMode(int mode)
{
    if (m_virtualDesktopMode == mode) {
        return;
    }
    m_virtualDesktopMode = mode;
    // Mode 2 (CurrentOnly): use TasksModel built-in filter
    m_tasksModel->setFilterByVirtualDesktop(mode == 2);
    Q_EMIT virtualDesktopModeChanged();
}

QVariant DockModel::currentDesktop() const
{
    return m_virtualDesktopInfo->currentDesktop();
}

bool DockModel::isOnCurrentDesktop(int index) const
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return true;
    }

    // Launchers (no window) are always considered "on current desktop"
    if (!idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool()) {
        return true;
    }

    // Windows on all desktops are always visible
    if (idx.data(TaskManager::AbstractTasksModel::IsOnAllVirtualDesktops).toBool()) {
        return true;
    }

    // Check if any of the task's desktops match the current desktop
    const QVariant currentDesktop = m_virtualDesktopInfo->currentDesktop();
    const QVariantList desktops = idx.data(TaskManager::AbstractTasksModel::VirtualDesktops).toList();
    return desktops.contains(currentDesktop);
}

} // namespace krema
