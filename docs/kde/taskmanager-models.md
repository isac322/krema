# KDE TaskManager Models

> Source: `/usr/include/taskmanager/` headers (plasma-workspace 6.5.5)

## Model Hierarchy

| Class | Base | Purpose |
|-------|------|---------|
| `AbstractTasksModel` | `QAbstractListModel` | Abstract base. Defines Role enum |
| `AbstractWindowTasksModel` | `AbstractTasksModel` | Window-specific abstract base |
| `WaylandTasksModel` | `AbstractWindowTasksModel` | Wayland window tasks |
| `XWindowTasksModel` | `AbstractWindowTasksModel` | X11 window tasks |
| `WindowTasksModel` | `QIdentityProxyModel` | Platform abstraction (wraps Wayland/X11) |
| `LauncherTasksModel` | `AbstractTasksModel` | .desktop launcher tasks |
| `StartupTasksModel` | `QIdentityProxyModel` | Startup notification tasks |
| `ConcatenateTasksProxyModel` | `QConcatenateTablesProxyModel` | Merges source models |
| `TaskFilterProxyModel` | `QSortFilterProxyModel` | Desktop/screen/activity/state filter |
| `TaskGroupingProxyModel` | `QAbstractProxyModel` | Groups by app (tree structure) |
| `FlattenTaskGroupsProxyModel` | `KDescendantsProxyModel` | Flattens group tree to list |
| `TasksModel` | `QSortFilterProxyModel` | Final unified model |

## Proxy Chain (inside TasksModel)

```
WindowTasksModel  ---+
StartupTasksModel ---+--> ConcatenateTasksProxyModel
LauncherTasksModel---+            |
                                  v
                        TaskFilterProxyModel
                          (desktop/screen/activity/state)
                                  |
                                  v
                       TaskGroupingProxyModel
                          (app-based grouping -> tree)
                                  |
                                  v
                   +--- groupInline == true ---+
                   |                           |
                   v                           v
    FlattenTaskGroupsProxyModel         (direct connect)
      (tree -> flat list)                      |
                   |                           |
                   +----------+----------------+
                              v
                         TasksModel
                   (QSortFilterProxyModel)
                   (final sort + filterAcceptsRow)
```

Source model switches dynamically in `updateGroupInline()`:
- `groupInline == true` + grouping active: `FlattenTaskGroupsProxyModel`
- Otherwise: `TaskGroupingProxyModel` directly

---

## WindowTasksModel vs TasksModel

### TasksModel (dock taskbar display)
- Launchers + windows + startup combined
- Built-in filtering/grouping/sorting
- `filterByVirtualDesktop`, `filterByActivity`, etc.
- Group parents have `IsGroupParent` role

### WindowTasksModel (visibility checks)
- Pure window list only (no launchers/startup)
- No grouping/filtering — all individual windows 1:1
- Suitable for geometry-based visibility checks
- Krema creates separate instance: `new TaskManager::WindowTasksModel(this)`

---

## Roles — AbstractTasksModel::AdditionalRoles

### Identity

| Role | Type | Description |
|------|------|-------------|
| `AppId` | QString | .desktop file name (without extension) |
| `AppName` | QString | Application name |
| `GenericName` | QString | Generic application name |
| `LauncherUrl` | QUrl | URL to launch application |
| `LauncherUrlWithoutIcon` | QUrl | LauncherUrl without icon encoding |
| `WinIdList` | QVariantList | Window IDs (local on Wayland, global on X11) |
| `AppPid` | int | Process ID (may be kwin_wayland PID on Wayland) |

### Task Type

| Role | Type | Description |
|------|------|-------------|
| `IsWindow` | bool | Is a window task |
| `IsStartup` | bool | Is a startup task |
| `IsLauncher` | bool | Is a launcher task |
| `HasLauncher` | bool | Launcher exists for this task |
| `IsGroupParent` | bool | Parent of grouped tasks |
| `ChildCount` | int | Number of tasks in group |
| `IsGroupable` | bool | Task ignored by grouping |
| `CanLaunchNewInstance` | bool | Can launch new instance |

### Window State

| Role | Type | Description |
|------|------|-------------|
| `IsActive` | bool | Currently active window |
| `IsClosable` | bool | Can be closed |
| `IsMovable` | bool | Can be moved |
| `IsResizable` | bool | Can be resized |
| `IsMaximizable` | bool | Can be maximized |
| `IsMaximized` | bool | Is maximized |
| `IsMinimizable` | bool | Can be minimized |
| `IsMinimized` | bool | Is minimized |
| `IsKeepAbove` | bool | Keep above other windows |
| `IsKeepBelow` | bool | Keep below other windows |
| `IsFullScreenable` | bool | Can enter fullscreen |
| `IsFullScreen` | bool | Is fullscreen |
| `IsShadeable` | bool | Can be shaded |
| `IsShaded` | bool | Is shaded |
| `IsHidden` | bool | Is hidden (different from minimized) |
| `IsDemandingAttention` | bool | Requesting user attention |
| `SkipTaskbar` | bool | Should skip taskbar |
| `SkipPager` | bool | Should skip pager |
| `HasNoBorder` | bool | Has no window border (KDE 6.4+) |
| `CanSetNoBorder` | bool | Can toggle border (KDE 6.4+) |

### Position / Desktop

| Role | Type | Description |
|------|------|-------------|
| `Geometry` | QRect | Window screen coordinates |
| `ScreenGeometry` | QRect | Screen geometry where window is |
| `VirtualDesktops` | QVariantList | Virtual desktop ID list |
| `IsOnAllVirtualDesktops` | bool | On all desktops |
| `IsVirtualDesktopsChangeable` | bool | Can change desktops |
| `Activities` | QStringList | Activity ID list |
| `StackingOrder` | int | Index in window stacking order |
| `LastActivated` | QDateTime | Last activation timestamp |

### D-Bus Menu

| Role | Type | Description |
|------|------|-------------|
| `ApplicationMenuServiceName` | QString | D-Bus service name for app menu |
| `ApplicationMenuObjectPath` | QString | D-Bus object path for app menu |

### Drag & Drop

| Role | Type | Description |
|------|------|-------------|
| `MimeType` | QString | MIME type for DnD |
| `MimeData` | QByteArray | Data for MIME type |

---

## TasksModel Enums

### SortMode

```cpp
enum SortMode {
    SortDisabled = 0,                // No sorting
    SortManual,                      // Manual (move/syncLaunchers)
    SortAlpha,                       // Alphabetical by AppName
    SortVirtualDesktop,              // By virtual desktop
    SortActivity,                    // By activity count
    SortLastActivated,               // By last activation time
    SortWindowPositionHorizontal,    // By desktop then window coordinates
};
```

### GroupMode

```cpp
enum GroupMode {
    GroupDisabled = 0,    // No grouping
    GroupApplications,    // Group by application
};
```

### RegionFilterMode::Mode

```cpp
enum Mode {
    Disabled = 0,  // No region filtering
    Inside,        // Window inside region
    Intersect,     // Window intersects region
    Outside,       // Window outside and doesn't intersect
};
```

---

## TasksModel Q_PROPERTIES

```cpp
Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
Q_PROPERTY(int launcherCount READ launcherCount NOTIFY launcherCountChanged)
Q_PROPERTY(QStringList launcherList READ launcherList WRITE setLauncherList NOTIFY launcherListChanged)
Q_PROPERTY(bool anyTaskDemandsAttention READ anyTaskDemandsAttention NOTIFY anyTaskDemandsAttentionChanged)

// Filtering criteria
Q_PROPERTY(QVariant virtualDesktop READ virtualDesktop WRITE setVirtualDesktop NOTIFY virtualDesktopChanged)
Q_PROPERTY(QRect screenGeometry READ screenGeometry WRITE setScreenGeometry NOTIFY screenGeometryChanged)
Q_PROPERTY(QRect regionGeometry READ regionGeometry WRITE setRegionGeometry NOTIFY regionGeometryChanged)
Q_PROPERTY(QString activity READ activity WRITE setActivity NOTIFY activityChanged)

// Filter toggles
Q_PROPERTY(bool filterByVirtualDesktop READ filterByVirtualDesktop WRITE setFilterByVirtualDesktop NOTIFY filterByVirtualDesktopChanged)
Q_PROPERTY(bool filterByScreen READ filterByScreen WRITE setFilterByScreen NOTIFY filterByScreenChanged)
Q_PROPERTY(bool filterByActivity READ filterByActivity WRITE setFilterByActivity NOTIFY filterByActivityChanged)
Q_PROPERTY(RegionFilterMode::Mode filterByRegion READ filterByRegion WRITE setFilterByRegion NOTIFY filterByRegionChanged)
Q_PROPERTY(bool filterMinimized READ filterMinimized WRITE setFilterMinimized NOTIFY filterMinimizedChanged)
Q_PROPERTY(bool filterNotMinimized READ filterNotMinimized WRITE setFilterNotMinimized NOTIFY filterNotMinimizedChanged)
Q_PROPERTY(bool filterNotMaximized READ filterNotMaximized WRITE setFilterNotMaximized NOTIFY filterNotMaximizedChanged)
Q_PROPERTY(bool filterHidden READ filterHidden WRITE setFilterHidden NOTIFY filterHiddenChanged)

// Sorting & grouping
Q_PROPERTY(SortMode sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
Q_PROPERTY(bool separateLaunchers READ separateLaunchers WRITE setSeparateLaunchers NOTIFY separateLaunchersChanged)
Q_PROPERTY(bool launchInPlace READ launchInPlace WRITE setLaunchInPlace NOTIFY launchInPlaceChanged)
Q_PROPERTY(bool hideActivatedLaunchers READ hideActivatedLaunchers WRITE setHideActivatedLaunchers NOTIFY hideActivatedLaunchersChanged)
Q_PROPERTY(GroupMode groupMode READ groupMode WRITE setGroupMode NOTIFY groupModeChanged)
Q_PROPERTY(bool groupInline READ groupInline WRITE setGroupInline NOTIFY groupInlineChanged)
Q_PROPERTY(int groupingWindowTasksThreshold READ groupingWindowTasksThreshold WRITE setGroupingWindowTasksThreshold NOTIFY groupingWindowTasksThresholdChanged)
Q_PROPERTY(QStringList groupingAppIdBlacklist READ groupingAppIdBlacklist WRITE setGroupingAppIdBlacklist NOTIFY groupingAppIdBlacklistChanged)
Q_PROPERTY(QStringList groupingLauncherUrlBlacklist READ groupingLauncherUrlBlacklist WRITE setGroupingLauncherUrlBlacklist NOTIFY groupingLauncherUrlBlacklistChanged)
Q_PROPERTY(bool taskReorderingEnabled READ taskReorderingEnabled WRITE setTaskReorderingEnabled NOTIFY taskReorderingEnabledChanged)

// Active task
Q_PROPERTY(QModelIndex activeTask READ activeTask NOTIFY activeTaskChanged)
```

---

## TasksModel Methods

### Launcher Management

```cpp
bool requestAddLauncher(const QUrl &url);
bool requestRemoveLauncher(const QUrl &url);
bool requestAddLauncherToActivity(const QUrl &url, const QString &activity);
bool requestRemoveLauncherFromActivity(const QUrl &url, const QString &activity);
QStringList launcherActivities(const QUrl &url);
int launcherPosition(const QUrl &url) const;
```

### Reordering

```cpp
bool move(int row, int newPos, const QModelIndex &parent = QModelIndex());
void syncLaunchers();  // Persist current launcher order
```

### Index Helpers

```cpp
Q_INVOKABLE QModelIndex makeModelIndex(int row, int childRow = -1) const;
Q_INVOKABLE QPersistentModelIndex makePersistentModelIndex(int row, int childRow = -1) const;
```

### Task Interaction (via AbstractTasksModelIface)

```cpp
void requestActivate(const QModelIndex &index);
void requestNewInstance(const QModelIndex &index);
void requestOpenUrls(const QModelIndex &index, const QList<QUrl> &urls);
void requestClose(const QModelIndex &index);
void requestMove(const QModelIndex &index);
void requestResize(const QModelIndex &index);
void requestToggleMinimized(const QModelIndex &index);
void requestToggleMaximized(const QModelIndex &index);
void requestToggleKeepAbove(const QModelIndex &index);
void requestToggleKeepBelow(const QModelIndex &index);
void requestToggleFullScreen(const QModelIndex &index);
void requestToggleShaded(const QModelIndex &index);
void requestToggleNoBorder(const QModelIndex &index);  // KDE 6.4+
void requestVirtualDesktops(const QModelIndex &index, const QVariantList &desktops);
void requestNewVirtualDesktop(const QModelIndex &index);
void requestActivities(const QModelIndex &index, const QStringList &activities);
void requestPublishDelegateGeometry(const QModelIndex &index, const QRect &geometry, QObject *delegate = nullptr);
void requestToggleGrouping(const QModelIndex &index);
```

---

## TaskFilterProxyModel Q_PROPERTIES

```cpp
Q_PROPERTY(QVariant virtualDesktop ...)
Q_PROPERTY(QRect screenGeometry ...)
Q_PROPERTY(QRect regionGeometry ...)
Q_PROPERTY(QString activity ...)
Q_PROPERTY(bool filterByVirtualDesktop ...)
Q_PROPERTY(bool filterByScreen ...)
Q_PROPERTY(bool filterByActivity ...)
Q_PROPERTY(RegionFilterMode::Mode filterByRegion ...)
Q_PROPERTY(bool filterMinimized ...)
Q_PROPERTY(bool filterNotMinimized ...)
Q_PROPERTY(bool filterNotMaximized ...)
Q_PROPERTY(bool filterHidden ...)
Q_PROPERTY(bool filterSkipTaskbar ...)    // not in TasksModel
Q_PROPERTY(bool filterSkipPager ...)      // not in TasksModel
Q_PROPERTY(bool demandingAttentionSkipsFilters ...)  // not in TasksModel
```

Extra method: `bool acceptsRow(int sourceRow) const;`

---

## TaskGroupingProxyModel Q_PROPERTIES

```cpp
Q_PROPERTY(TasksModel::GroupMode groupMode ...)
Q_PROPERTY(bool groupDemandingAttention ...)
Q_PROPERTY(int windowTasksThreshold ...)
Q_PROPERTY(QStringList blacklistedAppIds ...)
Q_PROPERTY(QStringList blacklistedLauncherUrls ...)
```

---

## LauncherTasksModel

### Q_PROPERTIES

```cpp
Q_PROPERTY(QStringList launcherList READ launcherList WRITE setLauncherList NOTIFY launcherListChanged)
```

### Methods

```cpp
bool requestAddLauncher(const QUrl &url);
bool requestRemoveLauncher(const QUrl &url);
bool requestAddLauncherToActivity(const QUrl &url, const QString &activity);
bool requestRemoveLauncherFromActivity(const QUrl &url, const QString &activity);
QStringList launcherActivities(const QUrl &url) const;
int launcherPosition(const QUrl &url) const;
int rowCountForActivity(const QString &activity) const;
```

Activity-scoped launchers: a launcher on **all** activities uses null UUID in the activity list.

---

## VirtualDesktopInfo

```cpp
Q_PROPERTY(QVariant currentDesktop READ currentDesktop NOTIFY currentDesktopChanged)
Q_PROPERTY(int numberOfDesktops READ numberOfDesktops NOTIFY numberOfDesktopsChanged)
Q_PROPERTY(QVariantList desktopIds READ desktopIds NOTIFY desktopIdsChanged)
Q_PROPERTY(QStringList desktopNames READ desktopNames NOTIFY desktopNamesChanged)
Q_PROPERTY(int desktopLayoutRows READ desktopLayoutRows NOTIFY desktopLayoutRowsChanged)
Q_PROPERTY(int navigationWrappingAround READ navigationWrappingAround NOTIFY navigationWrappingAroundChanged)
```

Methods:
```cpp
quint32 position(const QVariant &desktop) const;
void requestActivate(const QVariant &desktop);
void requestCreateDesktop(quint32 position);
void requestRemoveDesktop(quint32 position);
```

---

## ActivityInfo

```cpp
Q_PROPERTY(QString currentActivity READ currentActivity NOTIFY currentActivityChanged)
Q_PROPERTY(int numberOfRunningActivities READ numberOfRunningActivities NOTIFY numberOfRunningActivitiesChanged)

QStringList runningActivities() const;
QString activityName(const QString &id);
QString activityIcon(const QString &id);
```

---

## TaskTools Utility Functions

```cpp
namespace TaskManager {

struct AppData {
    QString id;
    QString name;
    QString genericName;
    QIcon icon;
    QUrl url;
    bool skipTaskbar;
};

enum UrlComparisonMode { Strict = 0, IgnoreQueryItems };

AppData appDataFromUrl(const QUrl &url, const QIcon &fallbackIcon = QIcon());
QUrl windowUrlFromMetadata(const QString &appId, quint32 pid = 0,
                           const KSharedConfig::Ptr &config = {},
                           const QString &xWindowsWMClassName = {});
KService::List servicesFromPid(quint32 pid, const KSharedConfig::Ptr &rulesConfig = {});
KService::List servicesFromCmdLine(const QString &cmdLine, const QString &processName,
                                   const KSharedConfig::Ptr &rulesConfig = {});
QString defaultApplication(const QUrl &url);  // resolves preferred:// URLs
bool launcherUrlsMatch(const QUrl &a, const QUrl &b, UrlComparisonMode mode = Strict);
bool appsMatch(const QModelIndex &a, const QModelIndex &b);
QRect screenGeometry(const QPoint &pos);
void runApp(const AppData &appData, const QList<QUrl> &urls = {});
bool canLaunchNewInstance(const AppData &appData);
}
```

---

## Key Design Patterns

1. **Task types**: Window / Startup / Launcher — combined in TasksModel
2. **Proxy chain**: Concatenate -> Filter -> Group -> Flatten -> Sort
3. **Grouping**: `GroupApplications` groups by app; `groupInline` flattens groups; blacklists exclude apps
4. **Launcher management**: Activity-scoped; null UUID = all activities; URL comparison ignores query strings
5. **Geometry publishing**: `requestPublishDelegateGeometry` sends screen coordinates back to the compositor
6. **Border control**: `HasNoBorder` / `CanSetNoBorder` / `requestToggleNoBorder` added in KDE 6.4
