// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <QCursor>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QMenu>
#include <QScreen>
#include <QUrl>

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

void DockModel::activate(int index)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const bool isLauncher = idx.data(TaskManager::AbstractTasksModel::IsLauncher).toBool();
    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();

    if (isWindow) {
        m_tasksModel->requestActivate(idx);
    } else if (isLauncher) {
        m_tasksModel->requestNewInstance(idx);
        Q_EMIT taskLaunching(index);
    }
}

void DockModel::newInstance(int index)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (idx.isValid()) {
        m_tasksModel->requestNewInstance(idx);
        Q_EMIT taskLaunching(index);
    }
}

void DockModel::closeTask(int index)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (idx.isValid()) {
        m_tasksModel->requestClose(idx);
    }
}

void DockModel::togglePinned(int index)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const QUrl launcherUrl = idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    if (launcherUrl.isValid()) {
        if (m_tasksModel->launcherList().contains(launcherUrl.toString())) {
            m_tasksModel->requestRemoveLauncher(launcherUrl);
        } else {
            m_tasksModel->requestAddLauncher(launcherUrl);
        }
        Q_EMIT pinnedLaunchersChanged();
    }
}

void DockModel::cycleWindows(int index, bool forward)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
    if (!isWindow) {
        return;
    }

    const int childCount = m_tasksModel->rowCount(idx);
    if (childCount <= 1) {
        // Single window or non-grouped: just activate/focus it
        m_tasksModel->requestActivate(idx);
        return;
    }

    // Find the currently active child window
    int activeChild = -1;
    for (int i = 0; i < childCount; ++i) {
        const QModelIndex child = m_tasksModel->makeModelIndex(index, i);
        if (child.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            activeChild = i;
            break;
        }
    }

    // Cycle to next/previous child
    int target;
    if (activeChild < 0) {
        target = 0;
    } else {
        target = forward ? (activeChild + 1) % childCount : (activeChild - 1 + childCount) % childCount;
    }

    const QModelIndex targetIdx = m_tasksModel->makeModelIndex(index, target);
    if (targetIdx.isValid()) {
        m_tasksModel->requestActivate(targetIdx);
    }
}

void DockModel::showContextMenu(int index)
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return;
    }

    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
    const QString name = idx.data(Qt::DisplayRole).toString();

    // Check pinned state via launcher list directly, not IsLauncher role.
    // IsLauncher doesn't update immediately for running apps with hideActivatedLaunchers.
    const QUrl launcherUrl = idx.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    const bool isPinned = launcherUrl.isValid() && m_tasksModel->launcherList().contains(launcherUrl.toString());

    auto *menu = new QMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // App name header (disabled, bold)
    QAction *header = menu->addAction(name);
    header->setEnabled(false);
    QFont boldFont = header->font();
    boldFont.setBold(true);
    header->setFont(boldFont);

    menu->addSeparator();

    // Pin / Unpin
    if (isPinned) {
        menu->addAction(QStringLiteral("Unpin from Dock"), this, [this, index]() {
            togglePinned(index);
        });
    } else {
        menu->addAction(QStringLiteral("Pin to Dock"), this, [this, index]() {
            togglePinned(index);
        });
    }

    // New Instance
    menu->addAction(QStringLiteral("New Instance"), this, [this, index]() {
        newInstance(index);
    });

    // Close (only for running windows)
    if (isWindow) {
        menu->addSeparator();
        menu->addAction(QStringLiteral("Close"), this, [this, index]() {
            closeTask(index);
        });
    }

    menu->popup(QCursor::pos());
}

} // namespace krema
