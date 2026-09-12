// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockmodel.h"
#include "baseisland.h"
#include "hyprlandtasksmodel.h"
#include "kdetasksproxymodel.h"
#include "utils/identitymanager.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/activityinfo.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/virtualdesktopinfo.h>

#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QProcessEnvironment>
#include <QScreen>

#include <algorithm>

#include "taskiconprovider.h"
#include "utils/debugmanager.h"

namespace krema
{
namespace
{

QString stripDesktopSuffix(const QString &id)
{
    static const QLatin1String suffix(".desktop");
    if (id.endsWith(suffix)) {
        return id.left(id.size() - suffix.size());
    }
    return id;
}

QString lastSegment(const QString &id)
{
    const int dot = id.lastIndexOf(QLatin1Char('.'));
    return (dot >= 0) ? id.mid(dot + 1) : id;
}

QString desktopNameFromUrl(const QUrl &url)
{
    if (!url.isValid()) {
        return {};
    }

    if (url.scheme() == QLatin1String("applications")) {
        return stripDesktopSuffix(url.path());
    }

    if (url.isLocalFile()) {
        const QString local = url.toLocalFile();
        if (local.endsWith(QLatin1String(".desktop"))) {
            return stripDesktopSuffix(QFileInfo(local).baseName());
        }
    }

    return {};
}

QStringList iconCandidates(const QModelIndex &idx)
{
    QStringList candidates;

    // THE IDENTITY RESOLVER:
    // Standardizes disparate app identifiers (appId, launcherUrl, display name)
    // to map raw metadata to high-res system theme icons and prevent 'Ghost Icons'.
    const QString appId = idx.data(HyprlandTasksModel::AppId).toString(); // Roles are same value
    const QString stripped = stripDesktopSuffix(appId);
    const QString segment = lastSegment(stripped);
    const QString display = idx.data(Qt::DisplayRole).toString().trimmed();
    const QString launcherName = desktopNameFromUrl(idx.data(HyprlandTasksModel::LauncherUrlWithoutIcon).toUrl());

    auto addCandidate = [&candidates](const QString &value) {
        if (value.isEmpty()) {
            return;
        }
        if (!candidates.contains(value)) {
            candidates.push_back(value);
        }
        const QString lowered = value.toLower();
        if (!lowered.isEmpty() && !candidates.contains(lowered)) {
            candidates.push_back(lowered);
        }
    };

    // 1. Primary identification
    addCandidate(appId);
    addCandidate(stripped);
    addCandidate(segment);
    addCandidate(launcherName);
    addCandidate(display);

    // THE IDENTITY BRIDGE: Functional Constraints
    if (stripped == QLatin1String("org.kde.dolphin") || stripped == QLatin1String("dolphin")) {
        addCandidate(QStringLiteral("org.kde.dolphin"));
        addCandidate(QStringLiteral("dolphin"));
    } else if (stripped == QLatin1String("org.kde.systemsettings") || stripped == QLatin1String("systemsettings")) {
        addCandidate(QStringLiteral("org.kde.systemsettings"));
        addCandidate(QStringLiteral("systemsettings"));
    } else if (stripped == QLatin1String("org.kde.konsole") || stripped == QLatin1String("konsole")) {
        addCandidate(QStringLiteral("org.kde.konsole"));
        addCandidate(QStringLiteral("konsole"));
    }

    // 3. Steam-specific mapping
    if (stripped.startsWith(QLatin1String("steam_app_"))) {
        QString steamThemeName = stripped;
        steamThemeName.replace(QLatin1String("steam_app_"), QLatin1String("steam_icon_"));
        addCandidate(steamThemeName);
        addCandidate(QStringLiteral("steam"));
    }

    return candidates;
}

} // namespace

DockModel::DockModel(QObject *parent)
    : QObject(parent)
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP")).toLower();
    m_isHyprland = desktop.contains(QStringLiteral("hyprland")) || env.contains(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"));

    if (m_isHyprland) {
        qCInfo(lcModel) << "Instantiating HyprlandTasksModel";
        m_hyprTasksModel = std::make_unique<HyprlandTasksModel>(this);
    } else {
        qCInfo(lcModel) << "Instantiating TaskManager::TasksModel (KDE)";
        m_kdeTasksModel = std::make_unique<TaskManager::TasksModel>(this);
        m_virtualDesktopInfo = std::make_shared<TaskManager::VirtualDesktopInfo>(this);
        m_activityInfo = std::make_shared<TaskManager::ActivityInfo>(this);

        m_kdeTasksModel->classBegin();
        m_kdeTasksModel->setGroupMode(TaskManager::TasksModel::GroupApplications);
        m_kdeTasksModel->setSortMode(TaskManager::TasksModel::SortManual);
        m_kdeTasksModel->setHideActivatedLaunchers(true);
        m_kdeTasksModel->setSeparateLaunchers(true);
        m_kdeTasksModel->setLaunchInPlace(true);
        m_kdeTasksModel->setGroupInline(false);
        m_kdeTasksModel->setTaskReorderingEnabled(true);

        m_kdeTasksModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
        m_kdeTasksModel->setActivity(m_activityInfo->currentActivity());

        if (auto *screen = QGuiApplication::primaryScreen()) {
            m_kdeTasksModel->setScreenGeometry(screen->geometry());
        }

        m_kdeTasksModel->setFilterByVirtualDesktop(false);
        m_kdeTasksModel->setFilterByScreen(false);
        m_kdeTasksModel->setFilterByActivity(false);
        m_kdeTasksModel->setFilterHidden(false);

        m_kdeTasksModel->componentComplete();

        m_kdeTasksProxyModel = std::make_unique<KdeTasksProxyModel>(this);
        m_kdeTasksProxyModel->setSourceModel(m_kdeTasksModel.get());

        connect(m_virtualDesktopInfo.get(), &TaskManager::VirtualDesktopInfo::currentDesktopChanged, this, [this]() {
            m_kdeTasksModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
            Q_EMIT currentDesktopChanged();
        });
        connect(m_activityInfo.get(), &TaskManager::ActivityInfo::currentActivityChanged, this, [this]() {
            m_kdeTasksModel->setActivity(m_activityInfo->currentActivity());
        });
    }

    auto *model = tasksModel();
    connect(model, &QAbstractItemModel::rowsInserted, this, [model]() {
        qCDebug(lcModel) << "Model rows after insert:" << model->rowCount();
    });
    connect(model, &QAbstractItemModel::rowsRemoved, this, [model]() {
        qCDebug(lcModel) << "Model rows after remove:" << model->rowCount();
    });

    m_appIsland = std::make_unique<BaseIsland>(QStringLiteral("app-island"), model, this);
}

DockModel::~DockModel() = default;

QAbstractItemModel *DockModel::tasksModel() const
{
    return m_isHyprland ? static_cast<QAbstractItemModel *>(m_hyprTasksModel.get()) : static_cast<QAbstractItemModel *>(m_kdeTasksProxyModel.get());
}

QVariantList DockModel::islandsVariant() const
{
    QVariantList list;
    if (m_appIsland) {
        list.append(QVariant::fromValue(static_cast<QObject *>(m_appIsland.get())));
    }
    return list;
}

TaskManager::TasksModel *DockModel::kdeTasksModel() const
{
    return m_kdeTasksModel.get();
}

HyprlandTasksModel *DockModel::hyprTasksModel() const
{
    return m_hyprTasksModel.get();
}

bool DockModel::isHyprland() const
{
    return m_isHyprland;
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
    return m_isHyprland ? m_hyprTasksModel->launcherList() : m_kdeTasksModel->launcherList();
}

void DockModel::setPinnedLaunchers(const QStringList &launchers)
{
    if (m_isHyprland) {
        m_hyprTasksModel->setLauncherList(launchers);
    } else {
        m_kdeTasksModel->setLauncherList(launchers);
    }
    Q_EMIT pinnedLaunchersChanged();
}

QVariant DockModel::iconData(int index) const
{
    auto *model = tasksModel();
    const QModelIndex idx = model->index(index, 0);
    if (!idx.isValid())
        return {};

    QString name = iconName(index);
    if (QIcon::hasThemeIcon(name)) {
        return QIcon::fromTheme(name);
    }

    QString id = idx.data(HyprlandTasksModel::AppId).toString();
    if (id.startsWith(QLatin1String("steam_app_"))) {
        QString steamIconId = QStringLiteral("steam_icon_") + id.mid(10);
        if (QIcon::hasThemeIcon(steamIconId))
            return QIcon::fromTheme(steamIconId);

        return QIcon::fromTheme(QStringLiteral("steam"));
    }

    const QVariant decoration = idx.data(Qt::DecorationRole);
    if (decoration.isValid() && !decoration.value<QIcon>().isNull()) {
        return decoration;
    }

    return QIcon::fromTheme(QStringLiteral("application-x-executable"));
}

QString DockModel::iconName(int index) const
{
    auto *model = tasksModel();
    const QModelIndex idx = model->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }

    const QStringList candidates = iconCandidates(idx);
    for (const QString &candidate : candidates) {
        if (QIcon::hasThemeIcon(candidate)) {
            return candidate;
        }
    }

    return candidates.value(0, QStringLiteral("application-x-executable"));
}

QUrl DockModel::launcherUrl(int index) const
{
    auto *model = tasksModel();
    const QModelIndex idx = model->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(HyprlandTasksModel::LauncherUrlWithoutIcon).toUrl();
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
    auto *model = tasksModel();
    const QModelIndex idx = model->index(index, 0);
    if (!idx.isValid()) {
        return false;
    }

    const QUrl url = idx.data(HyprlandTasksModel::LauncherUrlWithoutIcon).toUrl();
    if (!url.isValid()) {
        return false;
    }

    QString canonical = IdentityManager::canonicalLauncherUrl(url).toString();
    for (const QString &pinned : pinnedLaunchers()) {
        if (IdentityManager::canonicalLauncherUrl(QUrl(pinned)).toString() == canonical) {
            return true;
        }
    }
    return false;
}

int DockModel::pinnedBoundaryIndex() const
{
    auto *model = tasksModel();
    int count = model->rowCount();
    int lp = -1;
    for (int i = 0; i < count; ++i) {
        if (isPinned(i)) {
            lp = i;
        }
    }
    return lp;
}

QVariantList DockModel::windowIds(int index) const
{
    auto *model = tasksModel();
    const QModelIndex idx = model->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(HyprlandTasksModel::WinIdList).toList();
}

int DockModel::childCount(int index) const
{
    auto *model = tasksModel();
    const QModelIndex idx = model->index(index, 0);
    if (!idx.isValid()) {
        return 0;
    }
    return model->rowCount(idx);
}

QModelIndex DockModel::taskModelIndex(int index) const
{
    return tasksModel()->index(index, 0);
}

QString DockModel::appId(int index) const
{
    auto *model = tasksModel();
    const QModelIndex idx = model->index(index, 0);
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(HyprlandTasksModel::AppId).toString();
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
    if (!m_isHyprland) {
        m_kdeTasksModel->setFilterByVirtualDesktop(mode == 2);
    }
    Q_EMIT virtualDesktopModeChanged();
}

QVariant DockModel::currentDesktop() const
{
    if (m_isHyprland)
        return 0; // TODO: Hyprland workspaces
    return m_virtualDesktopInfo ? m_virtualDesktopInfo->currentDesktop() : QVariant();
}

bool DockModel::isOnCurrentDesktop(int index) const
{
    auto *model = tasksModel();
    const QModelIndex idx = model->index(index, 0);
    if (!idx.isValid()) {
        return true;
    }

    if (!idx.data(HyprlandTasksModel::IsWindow).toBool()) {
        return true;
    }

    if (idx.data(HyprlandTasksModel::IsOnAllVirtualDesktops).toBool()) {
        return true;
    }

    if (m_isHyprland)
        return true; // TODO

    const QVariant currentDesktop = m_virtualDesktopInfo->currentDesktop();
    const QVariantList desktops = idx.data(HyprlandTasksModel::VirtualDesktops).toList();
    return desktops.contains(currentDesktop);
}

} // namespace krema
