// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QScreen>

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

    const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
    return icon.name();
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

} // namespace krema
