// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockmodel.h"

#include "shell/outputordermonitor.h"
#include "taskiconprovider.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <algorithm>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QRect>
#include <QVariant>
#include <QScreen>
#include <QSet>

Q_LOGGING_CATEGORY(lcModel, "krema.model")

namespace krema
{

namespace
{
// launcherList() serializes activity-scoped launchers as "[ids]\n<url>", so a
// string lookup misses them; launcherPosition() matches the URL itself.
bool isPinnedTask(const TaskManager::TasksModel &model, const QModelIndex &index)
{
    if (!index.isValid()) {
        return false;
    }
    const QUrl url = index.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
    return url.isValid() && model.launcherPosition(url) != -1;
}

QString rawIconKey(const QModelIndex &index, const QString &appId, const QIcon &icon)
{
    // Window IDs are the stable task identity across row movement. Grouped
    // tasks may expose the same IDs in a different order, so canonicalize
    // them before hashing. Launchers use their URL; AppId is only the final
    // TaskManager/KService identity fallback and is never exposed in the key.
    QStringList identityParts;
    const QVariantList windowIds = index.data(TaskManager::AbstractTasksModel::WinIdList).toList();
    identityParts.reserve(windowIds.size());
    for (const QVariant &windowId : windowIds) {
        identityParts.append(QString::fromLatin1(windowId.typeName()) + QLatin1Char('=')
            + windowId.toString());
    }
    std::sort(identityParts.begin(), identityParts.end());

    QByteArray identity;
    if (!identityParts.isEmpty()) {
        identity = identityParts.join(QLatin1Char('\x1f')).toUtf8();
    } else {
        const QUrl launcherUrl = index.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
        identity = launcherUrl.isValid() ? launcherUrl.toString().toUtf8() : appId.toUtf8();
    }

    QByteArray material = identity;
    material.append('\0');
    material.append(QByteArray::number(static_cast<qulonglong>(icon.cacheKey()), 16));
    const QByteArray digest = QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex();
    return QStringLiteral("raw:%1").arg(QString::fromLatin1(digest));
}
} // namespace

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

    // Screen geometry — Plasma Task Manager always sets this. Use the Plasma
    // primary (kde_output_order_v1), not the Qt primary which is just the
    // first wl_output and never tracks KWin's priority changes.
    if (auto *screen = krema::OutputOrderMonitor::instance()->primaryScreen()) {
        m_tasksModel->setScreenGeometry(screen->geometry());
    }
    connect(krema::OutputOrderMonitor::instance(), &krema::OutputOrderMonitor::primaryOutputChanged, this, [this] {
        if (auto *screen = krema::OutputOrderMonitor::instance()->primaryScreen()) {
            m_tasksModel->setScreenGeometry(screen->geometry());
        }
    });

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

    // Defer sorting until the source model has finished its current change.
    // Our own moves emit layout signals synchronously; ignore those while
    // reconciling so they cannot recursively queue another sorting pass.
    connect(m_tasksModel.get(), &QAbstractItemModel::rowsInserted, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel.get(), &QAbstractItemModel::rowsRemoved, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel.get(), &QAbstractItemModel::rowsMoved, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel.get(), &QAbstractItemModel::modelReset, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel.get(), &QAbstractItemModel::layoutChanged, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel.get(), &TaskManager::TasksModel::launcherListChanged, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel.get(), &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        if (roles.isEmpty() || roles.contains(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon)
            || roles.contains(TaskManager::AbstractTasksModel::LauncherUrl) || roles.contains(TaskManager::AbstractTasksModel::HasLauncher)
            || roles.contains(TaskManager::AbstractTasksModel::IsGroupParent)) {
            scheduleTaskPartition();
        }
    });
    scheduleTaskPartition();

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

bool DockModel::separateLaunchers() const
{
    return m_separateLaunchers;
}

void DockModel::setSeparateLaunchers(bool separate)
{
    if (m_separateLaunchers == separate) {
        return;
    }
    m_separateLaunchers = separate;
    scheduleTaskPartition();
}

int DockModel::pinnedTaskCount() const
{
    return m_pinnedTaskCount;
}

void DockModel::scheduleTaskPartition()
{
    if (!m_separateLaunchers && m_pinnedTaskCount == 0) {
        return;
    }
    if (m_partitionPending || m_reconcilingPartition) {
        return;
    }
    m_partitionPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_partitionPending = false;
            reconcileTaskPartition();
        },
        Qt::QueuedConnection);
}

void DockModel::reconcileTaskPartition()
{
    if (!m_separateLaunchers) {
        if (m_pinnedTaskCount != 0) {
            m_pinnedTaskCount = 0;
            Q_EMIT pinnedTaskCountChanged();
        }
        return;
    }

    m_reconcilingPartition = true;
    int pinnedCount = 0;
    bool moved = false;
    bool moveFailed = false;
    const int count = m_tasksModel->rowCount();
    // Insertion into the next pinned slot preserves both zones' relative
    // order. Classify each row once; already partitioned rows need no move.
    for (int row = 0; row < count; ++row) {
        if (!isPinnedTask(*m_tasksModel, m_tasksModel->index(row, 0))) {
            continue;
        }
        if (row != pinnedCount && !moveFailed) {
            if (m_tasksModel->move(row, pinnedCount)) {
                moved = true;
            } else {
                qCWarning(lcModel) << "Could not move pinned task" << row << "to" << pinnedCount;
                moveFailed = true;
            }
        }
        ++pinnedCount;
    }
    if (moved) {
        m_tasksModel->syncLaunchers();
    }
    m_reconcilingPartition = false;
    if (m_pinnedTaskCount != pinnedCount) {
        m_pinnedTaskCount = pinnedCount;
        Q_EMIT pinnedTaskCountChanged();
    }
}

QString DockModel::iconName(int index) const
{
    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }

    const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
    QString name = icon.name();

    // A nameless, non-null DecorationRole is authoritative artwork. Give it
    // an opaque task/content key so it outranks an unrelated AppId fallback,
    // remains stable when rows move, and changes when the artwork changes.
    if (name.isEmpty() && !icon.isNull()) {
        name = rawIconKey(idx, appId(index), icon);
        TaskIconProvider::registerRawIcon(name, icon);
        return name;
    }

    // KWin/LibTaskManager can provide these named placeholders when a window
    // has no resolved decoration. Prefer a resolvable TaskManager/KService
    // AppId, but preserve the usable placeholder when no fallback exists.
    static const QSet<QString> genericPlaceholderIconNames{QStringLiteral("wayland"), QStringLiteral("unknown")};
    if (genericPlaceholderIconNames.contains(name)) {
        const QString app = appId(index);
        if (!app.isEmpty() && !QIcon::fromTheme(app).isNull()) {
            return app;
        }
        return name;
    }

    // AppId is the TaskManager/KService desktop-entry or executable identity,
    // not a promise that the raw Wayland app_id is a theme icon name.
    if (name.isEmpty() || QIcon::fromTheme(name).isNull()) {
        const QString app = appId(index);
        if (!app.isEmpty() && !QIcon::fromTheme(app).isNull()) {
            return app;
        }
    }

    return name;
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
    return isPinnedTask(*m_tasksModel, m_tasksModel->index(index, 0));
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

void DockModel::publishDelegateGeometry(int index, QObject *delegate)
{
    if (!delegate) {
        return;
    }

    const QModelIndex idx = m_tasksModel->index(index, 0);
    if (!idx.isValid()
        || (!idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool()
            && !idx.data(TaskManager::AbstractTasksModel::IsGroupParent).toBool())) {
        return;
    }

    m_tasksModel->requestPublishDelegateGeometry(idx, QRect{}, delegate);
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
