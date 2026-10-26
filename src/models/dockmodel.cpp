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
#include <QRegularExpression>
#include <QRect>
#include <QVariant>
#include <QScreen>
#include <QSet>

#include <utility>

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
struct ChatTitleState {
    int unreadState = 0;
    int mentionCount = 0;
};

enum class ChatTitleAdapter {
    None,
    Discord,
    WebCord,
};

ChatTitleAdapter chatTitleAdapterForAppId(const QString &rawAppId)
{
    QString appId = rawAppId.trimmed().toLower();
    if (appId.endsWith(QLatin1String(".desktop"))) {
        appId.chop(8);
    }
    if (appId == QLatin1String("discord") || appId == QLatin1String("com.discordapp.discord")
        || appId == QLatin1String("vesktop") || appId == QLatin1String("dev.vencord.vesktop")) {
        return ChatTitleAdapter::Discord;
    }
    if (appId == QLatin1String("webcord") || appId == QLatin1String("io.github.spacingbat3.webcord")) {
        return ChatTitleAdapter::WebCord;
    }
    return ChatTitleAdapter::None;
}

ChatTitleState chatTitleState(const QStringList &titles, ChatTitleAdapter adapter)
{
    if (adapter == ChatTitleAdapter::None) {
        return {};
    }

    static const QRegularExpression discordMention(
        QStringLiteral("^\\s*\\(([1-9][0-9]*)\\)\\s+Discord(?:\\b|\\s*\\||.*-\\s*Discord\\s*$)"));
    static const QRegularExpression discordUnread(
        QStringLiteral("^\\s*•\\s+Discord(?:\\b|\\s*\\||.*-\\s*Discord\\s*$)"));
    static const QRegularExpression webCordMention(QStringLiteral("^\\s*\\[([1-9][0-9]*)\\]\\s+WebCord\\b"));
    static const QRegularExpression webCordUnread(QStringLiteral("^\\s*\\*+\\s*\\[?WebCord\\]?\\b"));

    const QRegularExpression &mentionPattern = adapter == ChatTitleAdapter::WebCord ? webCordMention : discordMention;
    const QRegularExpression &unreadPattern = adapter == ChatTitleAdapter::WebCord ? webCordUnread : discordUnread;
    ChatTitleState result;
    bool hasMention = false;
    bool hasUnread = false;
    for (const QString &title : titles) {
        const QRegularExpressionMatch mentionMatch = mentionPattern.match(title);
        if (mentionMatch.hasMatch()) {
            hasMention = true;
            bool ok = false;
            const int count = mentionMatch.captured(1).toInt(&ok);
            if (ok) {
                result.mentionCount = std::max(result.mentionCount, count);
            }
            continue;
        }
        if (unreadPattern.match(title).hasMatch()) {
            hasUnread = true;
        }
    }
    result.unreadState = hasMention ? 2 : (hasUnread ? 1 : 0);
    return result;
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

    const auto bumpChatStateRevision = [this]() {
        ++m_chatStateRevision;
        Q_EMIT chatStateRevisionChanged();
    };
    connect(m_tasksModel.get(), &QAbstractItemModel::dataChanged, this, bumpChatStateRevision);
    connect(m_tasksModel.get(), &QAbstractItemModel::rowsInserted, this, bumpChatStateRevision);
    connect(m_tasksModel.get(), &QAbstractItemModel::rowsRemoved, this, bumpChatStateRevision);
    connect(m_tasksModel.get(), &QAbstractItemModel::modelReset, this, bumpChatStateRevision);

    // Launch in place: a task started for a visible pinned launcher replaces
    // that launcher at its position. libtaskmanager stopped doing this for
    // manual sorting when it moved launcher hiding into TaskFilterProxyModel
    // (plasma-workspace 4a513cce1, shipped in Plasma 6.8 as c2c859523), so
    // the task is appended at the end instead. The deferred pass below
    // compares the model with the order it last settled and moves such a
    // task back into its launcher's slot.
    connect(m_tasksModel.get(), &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        m_settledRows.clear();
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
    const QStringList previous = m_tasksModel->launcherList();
    m_tasksModel->setLauncherList(launchers);
    if (m_tasksModel->launcherList() != previous) {
        // A new list replaces pinned-order intent, not unpinned task anchors.
        snapshotSettledRows();
        QList<int> positions;
        positions.reserve(m_settledRows.size());
        for (const SettledRow &settled : std::as_const(m_settledRows)) {
            positions.append(settled.launcherUrl.isValid() ? m_tasksModel->launcherPosition(settled.launcherUrl) : -1);
        }
        // Stable insertion across pinned slots leaves all other rows in place.
        for (qsizetype row = 1; row < m_settledRows.size(); ++row) {
            if (positions.at(row) == -1) {
                continue;
            }
            qsizetype current = row;
            for (qsizetype earlier = row - 1; earlier >= 0; --earlier) {
                if (positions.at(earlier) == -1) {
                    continue;
                }
                if (positions.at(earlier) <= positions.at(current)) {
                    break;
                }
                std::swap(m_settledRows[current], m_settledRows[earlier]);
                std::swap(positions[current], positions[earlier]);
                current = earlier;
            }
        }
        scheduleTaskPartition();
    }
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

bool DockModel::moveTask(int fromIndex, int toIndex)
{
    if (!m_tasksModel->move(fromIndex, toIndex)) {
        return false;
    }
    m_tasksModel->syncLaunchers();
    // The user's order is the new settled order: a launcher-slot repair still
    // pending from before the move must not undo it.
    snapshotSettledRows();
    return true;
}

void DockModel::scheduleTaskPartition()
{
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
    snapshotSettledRows();
    m_reconcilingPartition = false;
    if (m_pinnedTaskCount != pinnedCount) {
        m_pinnedTaskCount = pinnedCount;
        Q_EMIT pinnedTaskCountChanged();
    }
}

bool DockModel::placeStartedTasksInLauncherSlots()
{
    // Resolve every launcher URL against the launcher list once per pass.
    QList<int> settledPositions;
    settledPositions.reserve(m_settledRows.size());
    for (const SettledRow &settled : std::as_const(m_settledRows)) {
        const int position = settled.launcherUrl.isValid() ? m_tasksModel->launcherPosition(settled.launcherUrl) : -1;
        settledPositions.append(position != -1 && settledPositions.contains(position) ? -1 : position);
    }

    // Each pinned URL keeps its logical slot through startup, window, and
    // group replacement. Restore the earliest representative first so later
    // slots can anchor to it.
    QList<qsizetype> vacated;
    for (qsizetype settledIndex = 0; settledIndex < m_settledRows.size(); ++settledIndex) {
        if (settledPositions.at(settledIndex) != -1) {
            vacated.append(settledIndex);
        }
    }
    if (vacated.isEmpty()) {
        return false;
    }

    const int count = m_tasksModel->rowCount();
    QList<CurrentRow> rows;
    rows.reserve(count);
    for (int row = 0; row < count; ++row) {
        const QModelIndex index = m_tasksModel->index(row, 0);
        const QUrl url = index.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
        CurrentRow current;
        current.launcherPosition = url.isValid() ? m_tasksModel->launcherPosition(url) : -1;
        current.isLauncher = index.data(TaskManager::AbstractTasksModel::IsLauncher).toBool();
        current.windowIds = windowIdStrings(index);
        current.appId = index.data(TaskManager::AbstractTasksModel::AppId).toString();
        rows.append(current);
    }

    bool moved = false;
    for (const qsizetype settledIndex : std::as_const(vacated)) {
        const int position = settledPositions.at(settledIndex);
        int taskRow = -1;
        bool launcherVisible = false;
        for (int row = 0; row < count; ++row) {
            if (rows.at(row).launcherPosition != position) {
                continue;
            }
            if (rows.at(row).isLauncher) {
                launcherVisible = true;
            } else if (taskRow < 0) {
                taskRow = row;
            }
        }
        if (taskRow < 0 || launcherVisible) {
            continue;
        }

        // Right after the nearest settled predecessor still in the dock.
        int target = 0;
        for (qsizetype previous = settledIndex - 1; previous >= 0; --previous) {
            const int anchorRow = findRow(rows, m_settledRows.at(previous), settledPositions.at(previous), taskRow);
            if (anchorRow >= 0) {
                target = anchorRow < taskRow ? anchorRow + 1 : anchorRow;
                break;
            }
        }
        if (target == taskRow) {
            continue;
        }
        if (!m_tasksModel->move(taskRow, target)) {
            qCWarning(lcModel) << "Could not move started task" << taskRow << "into its launcher slot" << target;
            continue;
        }
        // Keep the cached rows in model order.
        if (target < taskRow) {
            std::rotate(rows.begin() + target, rows.begin() + taskRow, rows.begin() + taskRow + 1);
        } else {
            std::rotate(rows.begin() + taskRow, rows.begin() + taskRow + 1, rows.begin() + target + 1);
        }
        moved = true;
    }
    return moved;
}

int DockModel::findRow(const QList<CurrentRow> &rows, const SettledRow &settled, int settledLauncherPosition, int excludedRow)
{
    for (int row = 0; row < rows.size(); ++row) {
        if (row == excludedRow) {
            continue;
        }
        const CurrentRow &current = rows.at(row);
        if (settled.launcherUrl.isValid()) {
            if (settledLauncherPosition != -1 && current.launcherPosition == settledLauncherPosition) {
                return row;
            }
        } else if (!settled.windowIds.isEmpty()) {
            if (std::any_of(current.windowIds.cbegin(), current.windowIds.cend(), [&](const QString &id) {
                    return settled.windowIds.contains(id);
                })) {
                return row;
            }
        } else if (!settled.appId.isEmpty() && !current.isLauncher && current.appId == settled.appId) {
            return row;
        }
    }
    return -1;
}

void DockModel::snapshotSettledRows()
{
    const int count = m_tasksModel->rowCount();
    m_settledRows.clear();
    m_settledRows.reserve(count);
    for (int row = 0; row < count; ++row) {
        const QModelIndex index = m_tasksModel->index(row, 0);
        SettledRow settled;
        if (isPinnedTask(*m_tasksModel, index)) {
            settled.launcherUrl = index.data(TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
        } else {
            settled.windowIds = windowIdStrings(index);
            settled.appId = index.data(TaskManager::AbstractTasksModel::AppId).toString();
        }
        m_settledRows.append(settled);
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

int DockModel::chatStateRevision() const
{
    return m_chatStateRevision;
}

int DockModel::chatUnreadStateForTitles(const QString &appId, const QStringList &titles)
{
    const ChatTitleAdapter adapter = chatTitleAdapterForAppId(appId);
    if (adapter == ChatTitleAdapter::None) {
        return 0;
    }
    return chatTitleState(titles, adapter).unreadState;
}

int DockModel::chatMentionCountForTitles(const QString &appId, const QStringList &titles)
{
    const ChatTitleAdapter adapter = chatTitleAdapterForAppId(appId);
    if (adapter == ChatTitleAdapter::None) {
        return 0;
    }
    return chatTitleState(titles, adapter).mentionCount;
}

int DockModel::taskChatUnreadState(int row) const
{
    return chatUnreadStateForTitles(appId(row), windowTitlesForRow(row));
}

int DockModel::taskChatMentionCount(int row) const
{
    return chatMentionCountForTitles(appId(row), windowTitlesForRow(row));
}

QStringList DockModel::windowTitlesForRow(int row) const
{
    const QModelIndex index = m_tasksModel->index(row, 0);
    if (!index.isValid()) {
        return {};
    }

    const int childCount = m_tasksModel->rowCount(index);
    const bool isWindow = index.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
    QStringList titles;
    titles.reserve(childCount + (isWindow ? 1 : 0));
    for (int child = 0; child < childCount; ++child) {
        titles.append(m_tasksModel->index(child, 0, index).data(Qt::DisplayRole).toString());
    }
    if (isWindow) {
        titles.append(index.data(Qt::DisplayRole).toString());
    }
    return titles;
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
