// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockmodel.h"

#include "shell/outputordermonitor.h"
#include "taskiconprovider.h"

#include <QAbstractItemModel>
#include <algorithm>

#include <QCryptographicHash>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QRect>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QVariant>

#include <utility>

Q_LOGGING_CATEGORY(lcModel, "krema.model")

namespace krema
{

namespace
{
// launcherList() serializes activity-scoped launchers as "[ids]\n<url>", so a
// string lookup misses them; launcherPosition() matches the URL itself.
bool isPinnedTask(const tasks::TasksModel &model, const QModelIndex &index)
{
    if (!index.isValid()) {
        return false;
    }
    const QUrl url = index.data(tasks::roles().LauncherUrlWithoutIcon).toUrl();
    return url.isValid() && model.launcherPosition(url) != -1;
}

QStringList windowIdStrings(const QModelIndex &index)
{
    const QVariantList windowIds = index.data(tasks::roles().WinIdList).toList();
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
    // task/KService identity fallback and is never exposed in the key.
    QStringList identityParts;
    const QVariantList windowIds = index.data(tasks::roles().WinIdList).toList();
    identityParts.reserve(windowIds.size());
    for (const QVariant &windowId : windowIds) {
        identityParts.append(QString::fromLatin1(windowId.typeName()) + QLatin1Char('=') + windowId.toString());
    }
    std::sort(identityParts.begin(), identityParts.end());

    QByteArray identity;
    if (!identityParts.isEmpty()) {
        identity = identityParts.join(QLatin1Char('\x1f')).toUtf8();
    } else {
        const QUrl launcherUrl = index.data(tasks::roles().LauncherUrlWithoutIcon).toUrl();
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
    Telegram,
    Element,
};

ChatTitleAdapter chatTitleAdapterForAppId(const QString &rawAppId)
{
    QString appId = rawAppId.trimmed().toLower();
    if (appId.endsWith(QLatin1String(".desktop"))) {
        appId.chop(8);
    }
    static const QRegularExpression telegramInstanceSuffix(QStringLiteral("^(org\\.telegram\\.desktop)\\._[0-9a-f]{32}$"));
    if (const auto match = telegramInstanceSuffix.match(appId); match.hasMatch()) {
        appId = match.captured(1);
    }
    if (appId == QLatin1String("discord") || appId == QLatin1String("com.discordapp.discord") || appId == QLatin1String("com.discord.app")
        || appId == QLatin1String("vesktop") || appId == QLatin1String("dev.vencord.vesktop") || appId == QLatin1String("dev.vencord")) {
        return ChatTitleAdapter::Discord;
    }
    if (appId == QLatin1String("webcord") || appId == QLatin1String("io.github.spacingbat3.webcord")) {
        return ChatTitleAdapter::WebCord;
    }
    if (appId == QLatin1String("org.telegram") || appId == QLatin1String("org.telegram.desktop") || appId == QLatin1String("telegram")
        || appId == QLatin1String("telegram-desktop") || appId == QLatin1String("telegramdesktop")) {
        return ChatTitleAdapter::Telegram;
    }
    if (appId == QLatin1String("io.element.element") || appId == QLatin1String("im.riot.riot") || appId == QLatin1String("element")
        || appId == QLatin1String("element-desktop")) {
        return ChatTitleAdapter::Element;
    }
    return ChatTitleAdapter::None;
}

int telegramTitleCount(const QStringList &titles)
{
    static const QRegularExpression telegramPrefix(QStringLiteral("^Telegram\\s*\\(([1-9][0-9]*)\\)\\s*$"));
    static const QRegularExpression telegramSuffix(QStringLiteral("[–-]\\s*\\(([1-9][0-9]*)\\)\\s*$"));

    int count = 0;
    for (const QString &title : titles) {
        const QRegularExpressionMatch prefixMatch = telegramPrefix.match(title);
        const QRegularExpressionMatch suffixMatch = telegramSuffix.match(title);
        const QRegularExpressionMatch match = prefixMatch.hasMatch() ? prefixMatch : suffixMatch;
        if (!match.hasMatch()) {
            continue;
        }
        bool ok = false;
        const int candidate = match.captured(1).toInt(&ok);
        if (ok) {
            count = std::max(count, candidate);
        }
    }
    return count;
}

ChatTitleState chatTitleState(const QStringList &titles, ChatTitleAdapter adapter)
{
    if (adapter == ChatTitleAdapter::None || adapter == ChatTitleAdapter::Telegram) {
        return {};
    }

    static const QRegularExpression discordMention(
        QStringLiteral("^\\s*(?:•\\s*)?\\(([1-9][0-9]*)\\)\\s+(?:(?:Discord|Vesktop)(?:\\b|\\s*\\||.*-\\s*(?:Discord|Vesktop)\\s*$)|\\S.*?\\s+[|-]\\s*(?:"
                       "Discord|Vesktop)\\s*$)"));
    static const QRegularExpression discordUnread(
        QStringLiteral("^\\s*•\\s+(?:(?:Discord|Vesktop)(?:\\b|\\s*\\||.*-\\s*(?:Discord|Vesktop)\\s*$)|\\S.*?\\s+[|-]\\s*(?:Discord|Vesktop)\\s*$)"));
    static const QRegularExpression webCordMention(QStringLiteral("^\\s*\\[([1-9][0-9]*)\\]\\s+WebCord\\b"));
    static const QRegularExpression webCordUnread(QStringLiteral("^\\s*\\*+\\s*\\[?WebCord\\]?\\b"));
    static const QRegularExpression elementMention(QStringLiteral("^\\s*Element(?:\\s*\\[[^\\]]*?\\])*?\\s*\\[([1-9][0-9]*)\\]"));
    static const QRegularExpression elementUnread(QStringLiteral("^\\s*Element(?:\\s*\\[[^\\]]*?\\])*?\\s*[\\*•]"));
    if (adapter == ChatTitleAdapter::Element) {
        ChatTitleState result;
        bool hasMention = false;
        bool hasUnread = false;
        for (const QString &title : titles) {
            const QRegularExpressionMatch mentionMatch = elementMention.match(title);
            if (mentionMatch.hasMatch()) {
                hasMention = true;
                bool ok = false;
                const int count = mentionMatch.captured(1).toInt(&ok);
                if (ok) {
                    result.mentionCount = std::max(result.mentionCount, count);
                }
                continue;
            }
            if (elementUnread.match(title).hasMatch()) {
                hasUnread = true;
            }
        }
        result.unreadState = hasMention ? 2 : (hasUnread ? 1 : 0);
        return result;
    }

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
{
    auto &backend = tasks::Backend::instance();
    if (!backend.ensureLoaded()) {
        qCWarning(lcModel) << "Could not load the Plasma task backend:" << backend.errorString();
        return;
    }

    m_virtualDesktopInfo = backend.createVirtualDesktopInfo(this);
    m_activityInfo = backend.createActivityInfo(this);
    if (!m_virtualDesktopInfo || !m_activityInfo) {
        qCWarning(lcModel) << "Could not create Plasma virtual desktop/activity information";
        return;
    }

    QVariantMap initialProperties{
        {QStringLiteral("hideActivatedLaunchers"), true},
        {QStringLiteral("separateLaunchers"), false},
        {QStringLiteral("launchInPlace"), true},
        {QStringLiteral("groupInline"), false},
        {QStringLiteral("taskReorderingEnabled"), true},
        {QStringLiteral("virtualDesktop"), m_virtualDesktopInfo->property("currentDesktop")},
        {QStringLiteral("activity"), m_activityInfo->property("currentActivity")},
        {QStringLiteral("filterByVirtualDesktop"), false},
        {QStringLiteral("filterByScreen"), false},
        {QStringLiteral("filterByActivity"), false},
        {QStringLiteral("filterHidden"), false},
    };
    if (auto *screen = krema::OutputOrderMonitor::instance()->primaryScreen()) {
        initialProperties.insert(QStringLiteral("screenGeometry"), screen->geometry());
    }

    m_tasksModel = backend.createTasksModel(
        "groupMode: TasksModel.GroupApplications\n"
        "sortMode: TasksModel.SortManual\n",
        initialProperties,
        this);
    if (!m_tasksModel) {
        qCWarning(lcModel) << "Could not create the Plasma task model";
        return;
    }

    // Plasma's primary output can change independently of Qt's primary screen.
    connect(krema::OutputOrderMonitor::instance(), &krema::OutputOrderMonitor::primaryOutputChanged, this, [this] {
        if (auto *screen = krema::OutputOrderMonitor::instance()->primaryScreen()) {
            tasks().setProperty("screenGeometry", screen->geometry());
        }
    });

    // Track desktop/activity changes so the model stays up to date.
    tasks::connectNotify(m_virtualDesktopInfo, "currentDesktop", this, [this]() {
        tasks().setProperty("virtualDesktop", m_virtualDesktopInfo->property("currentDesktop"));
        Q_EMIT currentDesktopChanged();
    });
    tasks::connectNotify(m_activityInfo, "currentActivity", this, [this]() {
        tasks().setProperty("activity", m_activityInfo->property("currentActivity"));
    });

    const auto bumpChatStateRevision = [this]() {
        ++m_chatStateRevision;
        Q_EMIT chatStateRevisionChanged();
    };
    connect(m_tasksModel, &QAbstractItemModel::dataChanged, this, bumpChatStateRevision);
    connect(m_tasksModel, &QAbstractItemModel::rowsInserted, this, bumpChatStateRevision);
    connect(m_tasksModel, &QAbstractItemModel::rowsRemoved, this, bumpChatStateRevision);
    connect(m_tasksModel, &QAbstractItemModel::modelReset, this, bumpChatStateRevision);

    // Launch in place: a task started for a visible pinned launcher replaces
    // that launcher at its position. libtaskmanager stopped doing this for
    // manual sorting when it moved launcher hiding into TaskFilterProxyModel
    // (plasma-workspace 4a513cce1, shipped in Plasma 6.8 as c2c859523), so
    // the task is appended at the end instead. The deferred pass below
    // compares the model with the order it last settled and moves such a
    // task back into its launcher's slot.
    connect(m_tasksModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        m_settledRows.clear();
    });

    // Defer sorting until the source model has finished its current change.
    // Our own moves emit layout signals synchronously; ignore those while
    // reconciling so they cannot recursively queue another sorting pass.
    connect(m_tasksModel, &QAbstractItemModel::rowsInserted, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel, &QAbstractItemModel::rowsRemoved, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel, &QAbstractItemModel::rowsMoved, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel, &QAbstractItemModel::modelReset, this, &DockModel::scheduleTaskPartition);
    connect(m_tasksModel, &QAbstractItemModel::layoutChanged, this, &DockModel::scheduleTaskPartition);
    tasks::connectSignal(m_tasksModel, "launcherListChanged", this, [this]() {
        scheduleTaskPartition();
    });
    connect(m_tasksModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &changedRoles) {
        if (changedRoles.isEmpty() || changedRoles.contains(tasks::roles().LauncherUrlWithoutIcon) || changedRoles.contains(tasks::roles().LauncherUrl)
            || changedRoles.contains(tasks::roles().HasLauncher) || changedRoles.contains(tasks::roles().IsGroupParent)) {
            scheduleTaskPartition();
        }
    });
    scheduleTaskPartition();

    // Debug logging for model row changes
    connect(m_tasksModel, &QAbstractItemModel::rowsInserted, this, [this]() {
        qCDebug(lcModel) << "Model rows after insert:" << tasks().rowCount();
    });
    connect(m_tasksModel, &QAbstractItemModel::rowsRemoved, this, [this]() {
        qCDebug(lcModel) << "Model rows after remove:" << tasks().rowCount();
    });
}

DockModel::~DockModel() = default;

QAbstractItemModel *DockModel::tasksModel() const
{
    return m_tasksModel;
}

tasks::TasksModel DockModel::tasks() const
{
    return tasks::TasksModel(m_tasksModel);
}

QObject *DockModel::virtualDesktopInfo() const
{
    return m_virtualDesktopInfo;
}

QObject *DockModel::activityInfo() const
{
    return m_activityInfo;
}

QStringList DockModel::pinnedLaunchers() const
{
    return tasks().launcherList();
}

void DockModel::setPinnedLaunchers(const QStringList &launchers)
{
    const auto taskModel = tasks();
    const QStringList previous = taskModel.launcherList();
    taskModel.setLauncherList(launchers);
    if (taskModel.launcherList() != previous) {
        // A new list replaces pinned-order intent, not unpinned task anchors.
        snapshotSettledRows();
        QList<int> positions;
        positions.reserve(m_settledRows.size());
        for (const SettledRow &settled : std::as_const(m_settledRows)) {
            positions.append(settled.launcherUrl.isValid() ? taskModel.launcherPosition(settled.launcherUrl) : -1);
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
    const auto taskModel = tasks();
    if (!taskModel.move(fromIndex, toIndex)) {
        return false;
    }
    taskModel.syncLaunchers();
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
    const auto taskModel = tasks();

    int pinnedCount = 0;
    if (m_separateLaunchers) {
        bool moveFailed = false;
        const int count = taskModel.rowCount();
        // Insertion into the next pinned slot preserves both zones' relative
        // order. Classify each row once; already partitioned rows need no move.
        for (int row = 0; row < count; ++row) {
            if (!isPinnedTask(taskModel, taskModel.index(row, 0))) {
                continue;
            }
            if (row != pinnedCount && !moveFailed) {
                if (taskModel.move(row, pinnedCount)) {
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
        taskModel.syncLaunchers();
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
    const auto taskModel = tasks();
    // Resolve every launcher URL against the launcher list once per pass.
    QList<int> settledPositions;
    settledPositions.reserve(m_settledRows.size());
    for (const SettledRow &settled : std::as_const(m_settledRows)) {
        const int position = settled.launcherUrl.isValid() ? taskModel.launcherPosition(settled.launcherUrl) : -1;
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

    const int count = taskModel.rowCount();
    QList<CurrentRow> rows;
    rows.reserve(count);
    for (int row = 0; row < count; ++row) {
        const QModelIndex index = taskModel.index(row, 0);
        const QUrl url = index.data(tasks::roles().LauncherUrlWithoutIcon).toUrl();
        CurrentRow current;
        current.launcherPosition = url.isValid() ? taskModel.launcherPosition(url) : -1;
        current.isLauncher = index.data(tasks::roles().IsLauncher).toBool();
        current.windowIds = windowIdStrings(index);
        current.appId = index.data(tasks::roles().AppId).toString();
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
        if (!taskModel.move(taskRow, target)) {
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
    const auto taskModel = tasks();
    const int count = taskModel.rowCount();
    m_settledRows.clear();
    m_settledRows.reserve(count);
    for (int row = 0; row < count; ++row) {
        const QModelIndex index = taskModel.index(row, 0);
        SettledRow settled;
        if (isPinnedTask(taskModel, index)) {
            settled.launcherUrl = index.data(tasks::roles().LauncherUrlWithoutIcon).toUrl();
        } else {
            settled.windowIds = windowIdStrings(index);
            settled.appId = index.data(tasks::roles().AppId).toString();
        }
        m_settledRows.append(settled);
    }
}

QString DockModel::iconName(int index) const
{
    const auto taskModel = tasks();
    const QModelIndex idx = taskModel.index(index, 0);
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

    // KWin can provide these named placeholders when a window has no resolved
    // decoration. Prefer a resolvable service identity, but preserve the
    // usable placeholder when no fallback exists.
    static const QSet<QString> genericPlaceholderIconNames{QStringLiteral("wayland"), QStringLiteral("unknown")};
    if (genericPlaceholderIconNames.contains(name)) {
        const QString app = appId(index);
        if (!app.isEmpty() && !QIcon::fromTheme(app).isNull()) {
            return app;
        }
        return name;
    }

    // AppId is the service desktop-entry or executable identity, not a
    // promise that the raw Wayland app_id is a theme icon name.
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
    const QModelIndex idx = tasks().index(index, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(tasks::roles().LauncherUrlWithoutIcon).toUrl();
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
    const auto taskModel = tasks();
    return isPinnedTask(taskModel, taskModel.index(index, 0));
}

QVariantList DockModel::windowIds(int index) const
{
    const QModelIndex idx = tasks().index(index, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(tasks::roles().WinIdList).toList();
}

int DockModel::childCount(int index) const
{
    const auto taskModel = tasks();
    const QModelIndex idx = taskModel.index(index, 0);
    if (!idx.isValid()) {
        return 0;
    }
    return taskModel.rowCount(idx);
}

QModelIndex DockModel::taskModelIndex(int index) const
{
    return tasks().index(index, 0);
}

void DockModel::publishDelegateGeometry(int index, QObject *delegate)
{
    if (!delegate) {
        return;
    }

    const auto taskModel = tasks();
    const QModelIndex idx = taskModel.index(index, 0);
    if (!idx.isValid() || (!idx.data(tasks::roles().IsWindow).toBool() && !idx.data(tasks::roles().IsGroupParent).toBool())) {
        return;
    }

    taskModel.requestPublishDelegateGeometry(idx, QRect{}, delegate);
}

QString DockModel::appId(int index) const
{
    const QModelIndex idx = tasks().index(index, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(tasks::roles().AppId).toString();
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
    const ChatTitleState state = chatTitleState(titles, adapter);
    if (adapter == ChatTitleAdapter::Telegram && state.unreadState == 0 && telegramTitleCount(titles) > 0) {
        return 1;
    }
    return state.unreadState;
}

int DockModel::chatMentionCountForTitles(const QString &appId, const QStringList &titles)
{
    const ChatTitleAdapter adapter = chatTitleAdapterForAppId(appId);
    if (adapter == ChatTitleAdapter::None) {
        return 0;
    }
    const ChatTitleState state = chatTitleState(titles, adapter);
    if (adapter == ChatTitleAdapter::Telegram) {
        return std::max(state.mentionCount, telegramTitleCount(titles));
    }
    return state.mentionCount;
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
    const auto taskModel = tasks();
    const QModelIndex index = taskModel.index(row, 0);
    if (!index.isValid()) {
        return {};
    }

    const int childCount = taskModel.rowCount(index);
    const bool isWindow = index.data(tasks::roles().IsWindow).toBool();
    QStringList titles;
    titles.reserve(childCount + (isWindow ? 1 : 0));
    for (int child = 0; child < childCount; ++child) {
        titles.append(taskModel.index(child, 0, index).data(Qt::DisplayRole).toString());
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
    tasks().setProperty("filterByVirtualDesktop", mode == 2);
    Q_EMIT virtualDesktopModeChanged();
}

QVariant DockModel::currentDesktop() const
{
    return m_virtualDesktopInfo ? m_virtualDesktopInfo->property("currentDesktop") : QVariant();
}

bool DockModel::isOnCurrentDesktop(int index) const
{
    const auto taskModel = tasks();
    const QModelIndex idx = taskModel.index(index, 0);
    if (!idx.isValid()) {
        return true;
    }

    // Launchers (no window) are always considered "on current desktop"
    if (!idx.data(tasks::roles().IsWindow).toBool()) {
        return true;
    }

    // Windows on all desktops are always visible
    if (idx.data(tasks::roles().IsOnAllVirtualDesktops).toBool()) {
        return true;
    }

    // Check if any of the task's desktops match the current desktop
    const QVariant currentDesktop = m_virtualDesktopInfo ? m_virtualDesktopInfo->property("currentDesktop") : QVariant();
    const QVariantList desktops = idx.data(tasks::roles().VirtualDesktops).toList();
    return desktops.contains(currentDesktop);
}

} // namespace krema
