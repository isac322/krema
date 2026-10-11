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

bool sameLauncher(const TaskManager::TasksModel &model, const QUrl &a, const QUrl &b)
{
    if (!a.isValid() || !b.isValid()) {
        return false;
    }
    const int position = model.launcherPosition(a);
    return position != -1 && position == model.launcherPosition(b);
}

QStringList windowIdStrings(const QModelIndex &index)
{
    const QVariantList windowIds = index.data(TaskManager::AbstractTasksModel::WinIdList).toList();
    QStringList ids;
    ids.reserve(windowIds.size());
    for (const QVariant &windowId : windowIds) {
        ids.append(windowId.toString());
    }
    return ids;
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

    // Launch in place: a task started for a visible pinned launcher replaces
    // that launcher at its position. libtaskmanager stopped doing this for
    // manual sorting when it moved launcher hiding into TaskFilterProxyModel
    // (plasma-workspace 4a513cce1, shipped in Plasma 6.8 as c2c859523), so
    // the task is appended at the end instead. Remember where each launcher
    // left and which pinned tasks appeared; the deferred pass below moves
    // them back into place.
    // Connected before the scheduling slots so they see the recorded state.
    connect(m_tasksModel.get(), &QAbstractItemModel::rowsAboutToBeRemoved, this, [this](const QModelIndex &parent, int first, int last) {
        if (!parent.isValid()) {
            recordVacatedLaunchers(first, last);
        }
    });
    connect(m_tasksModel.get(), &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &parent, int first, int last) {
        if (!parent.isValid()) {
            recordStartedTasks(first, last);
        }
    });
    connect(m_tasksModel.get(), &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        m_vacatedLaunchers.clear();
        m_startedTaskLaunchers.clear();
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
    if (!m_separateLaunchers && m_pinnedTaskCount == 0 && m_vacatedLaunchers.isEmpty() && m_startedTaskLaunchers.isEmpty()) {
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
    m_reconcilingPartition = true;
    bool moved = placeStartedTasksInLauncherSlots();

    int pinnedCount = 0;
    if (m_separateLaunchers) {
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

void DockModel::recordVacatedLaunchers(int first, int last)
{
    if (m_reconcilingPartition) {
        return;
    }
    for (int row = first; row <= last; ++row) {
        const QModelIndex index = m_tasksModel->index(row, 0);
        if (!index.data(TaskManager::AbstractTasksModel::IsLauncher).toBool()) {
            continue;
        }
        VacatedLauncher vacated;
        vacated.launcherUrl = index.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
        if (!vacated.launcherUrl.isValid()) {
            continue;
        }
        // The preceding row may itself be a vacating launcher; matching by
        // launcher then resolves to the task that replaces it.
        if (row > 0) {
            const QModelIndex previous = m_tasksModel->index(row - 1, 0);
            vacated.hasAnchor = true;
            if (isPinnedTask(*m_tasksModel, previous)) {
                vacated.anchorLauncherUrl = previous.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
            } else {
                vacated.anchorWindowIds = windowIdStrings(previous);
                vacated.anchorAppId = previous.data(TaskManager::AbstractTasksModel::AppId).toString();
            }
        }
        m_vacatedLaunchers.append(vacated);
    }
}

void DockModel::recordStartedTasks(int first, int last)
{
    if (m_reconcilingPartition) {
        return;
    }
    for (int row = first; row <= last; ++row) {
        const QModelIndex index = m_tasksModel->index(row, 0);
        if (!index.data(TaskManager::AbstractTasksModel::IsLauncher).toBool() && isPinnedTask(*m_tasksModel, index)) {
            m_startedTaskLaunchers.append(index.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl());
        }
    }
}

bool DockModel::placeStartedTasksInLauncherSlots()
{
    QList<VacatedLauncher> vacated;
    vacated.swap(m_vacatedLaunchers);
    QList<QUrl> started;
    started.swap(m_startedTaskLaunchers);

    // A launcher can also leave because it was unpinned or filtered out; only
    // a launcher whose task has just started is replaced in place.
    vacated.removeIf([&](const VacatedLauncher &entry) {
        return std::none_of(started.cbegin(), started.cend(), [&](const QUrl &url) {
            return sameLauncher(*m_tasksModel, entry.launcherUrl, url);
        });
    });

    bool moved = false;
    // An anchor may be another replaced launcher; repeat until chains settle.
    for (qsizetype round = 0; round < vacated.size(); ++round) {
        bool movedThisRound = false;
        for (const VacatedLauncher &entry : std::as_const(vacated)) {
            int taskRow = -1;
            const int count = m_tasksModel->rowCount();
            for (int row = 0; row < count; ++row) {
                const QModelIndex index = m_tasksModel->index(row, 0);
                if (!index.data(TaskManager::AbstractTasksModel::IsLauncher).toBool()
                    && sameLauncher(*m_tasksModel, entry.launcherUrl, index.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl())) {
                    taskRow = row;
                    break;
                }
            }
            if (taskRow < 0) {
                continue;
            }

            int target = 0;
            if (entry.hasAnchor) {
                const int anchorRow = findAnchorRow(entry);
                if (anchorRow < 0 || anchorRow == taskRow) {
                    continue;
                }
                target = anchorRow < taskRow ? anchorRow + 1 : anchorRow;
            }
            if (target == taskRow) {
                continue;
            }
            if (!m_tasksModel->move(taskRow, target)) {
                qCWarning(lcModel) << "Could not move started task" << taskRow << "into its launcher slot" << target;
                continue;
            }
            moved = true;
            movedThisRound = true;
        }
        if (!movedThisRound) {
            break;
        }
    }
    return moved;
}

int DockModel::findAnchorRow(const VacatedLauncher &vacated) const
{
    const int count = m_tasksModel->rowCount();
    for (int row = 0; row < count; ++row) {
        const QModelIndex index = m_tasksModel->index(row, 0);
        if (vacated.anchorLauncherUrl.isValid()) {
            if (sameLauncher(*m_tasksModel, vacated.anchorLauncherUrl, index.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl())) {
                return row;
            }
        } else if (!vacated.anchorWindowIds.isEmpty()) {
            const QStringList windowIds = windowIdStrings(index);
            if (std::any_of(windowIds.cbegin(), windowIds.cend(), [&](const QString &id) {
                    return vacated.anchorWindowIds.contains(id);
                })) {
                return row;
            }
        } else if (!vacated.anchorAppId.isEmpty() && !index.data(TaskManager::AbstractTasksModel::IsLauncher).toBool()
                   && index.data(TaskManager::AbstractTasksModel::AppId).toString() == vacated.anchorAppId) {
            return row;
        }
    }
    return -1;
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
