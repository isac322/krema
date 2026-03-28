# KDE Virtual Desktops

> Source: `/usr/include/taskmanager/virtualdesktopinfo.h`, `/usr/include/taskmanager/taskfilterproxymodel.h`, `/usr/include/taskmanager/abstracttasksmodel.h`

## VirtualDesktopInfo Class

**Header**: `<taskmanager/virtualdesktopinfo.h>`
**Package**: `libplasma` (taskmanager)
**QML module**: `org.kde.taskmanager`

| Property/Method | Type | Description |
|----------------|------|-------------|
| `currentDesktop` | QVariant | Currently active desktop ID |
| `numberOfDesktops` | int | Total desktop count |
| `desktopIds` | QVariantList | All desktop ID list |
| `desktopNames` | QStringList | All desktop name list |
| `desktopLayoutRows` | int | Desktop layout row count |
| `navigationWrappingAround` | bool | Whether wrapping navigation is enabled |
| `position(desktop)` | quint32 | Position of desktop (-1 = invalid) |
| `requestActivate(desktop)` | void | Switch to specified desktop |
| `requestCreateDesktop(position)` | void | Create new desktop at position |
| `requestRemoveDesktop(position)` | void | Remove desktop at position |

### Signals
- `currentDesktopChanged` — desktop switched
- `numberOfDesktopsChanged` — desktop count changed
- `desktopIdsChanged` — desktop ID list changed
- `desktopNamesChanged` — name changed
- `desktopPositionsChanged` — position changed
- `desktopLayoutRowsChanged` — layout rows changed
- `navigationWrappingAroundChanged` — wrapping changed

## Desktop ID Format

**Platform-dependent** (documented in header):
- **Wayland**: `QString` (opaque UUID, managed by KWin)
- **X11**: `uint > 0`

Wrapped as `QVariant` so platform-independent comparison works:
```cpp
QVariant currentDesktop = virtualDesktopInfo->currentDesktop();
if (desktops.contains(currentDesktop)) { ... }
```

---

## TasksModel Virtual Desktop Filtering

**Header**: `<taskmanager/tasksmodel.h>`

### Properties for Virtual Desktop Filtering

```cpp
Q_PROPERTY(QVariant virtualDesktop READ virtualDesktop WRITE setVirtualDesktop NOTIFY virtualDesktopChanged)
Q_PROPERTY(bool filterByVirtualDesktop READ filterByVirtualDesktop WRITE setFilterByVirtualDesktop NOTIFY filterByVirtualDesktopChanged)
```

### Enabling Current-Desktop-Only Mode

```cpp
// Initialization order MUST follow classBegin → properties → componentComplete
m_tasksModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
m_tasksModel->setFilterByVirtualDesktop(true);

// Keep up to date when desktop switches
connect(m_virtualDesktopInfo.get(), &TaskManager::VirtualDesktopInfo::currentDesktopChanged,
    this, [this]() {
        m_tasksModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
    });
```

### Plasma Reference

`/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/main.qml`:
```qml
TaskManager.TasksModel {
    virtualDesktop: virtualDesktopInfo.currentDesktop
    filterByVirtualDesktop: Plasmoid.configuration.showOnlyCurrentDesktop  // default: true
    filterByScreen: Plasmoid.configuration.showOnlyCurrentScreen           // default: false
    filterByActivity: Plasmoid.configuration.showOnlyCurrentActivity       // default: true
}

TaskManager.VirtualDesktopInfo {
    id: virtualDesktopInfo
}
```

---

## AbstractTasksModel Roles for Virtual Desktop

**Header**: `<taskmanager/abstracttasksmodel.h>`

| Role | Type | Description |
|------|------|-------------|
| `VirtualDesktops` | QVariantList | List of desktop IDs this task is on |
| `IsOnAllVirtualDesktops` | bool | True if task is on all desktops (sticky) |
| `IsActive` | bool | True if this is the currently active/focused task |
| `ScreenGeometry` | QRect | Screen geometry of the task's screen |
| `IsDemandingAttention` | bool | True if task is demanding attention |

### Window Desktop Membership Pattern

```cpp
const QVariant currentDesktop = virtualDesktopInfo->currentDesktop();

auto idx = tasksModel->index(row, 0);

// 1. Always include "sticky" windows (on all desktops)
bool onAllDesktops = idx.data(AbstractTasksModel::IsOnAllVirtualDesktops).toBool();

// 2. Check if window is on current desktop
auto desktops = idx.data(AbstractTasksModel::VirtualDesktops).toList();
bool onCurrentDesktop = desktops.contains(currentDesktop);

// For "all desktops visible with reduced opacity" mode:
bool isCurrentDesktopTask = onAllDesktops || onCurrentDesktop;
// Apply full opacity to isCurrentDesktopTask, reduced opacity to others
```

---

## TaskFilterProxyModel Virtual Desktop Filtering

**Header**: `<taskmanager/taskfilterproxymodel.h>`

For a separate filter proxy (e.g., DockVisibilityController already uses this):

```cpp
Q_PROPERTY(QVariant virtualDesktop READ virtualDesktop WRITE setVirtualDesktop NOTIFY virtualDesktopChanged)
Q_PROPERTY(bool filterByVirtualDesktop READ filterByVirtualDesktop WRITE setFilterByVirtualDesktop NOTIFY filterByVirtualDesktopChanged)
Q_PROPERTY(QRect screenGeometry READ screenGeometry WRITE setScreenGeometry NOTIFY screenGeometryChanged)
Q_PROPERTY(bool filterByScreen READ filterByScreen WRITE setFilterByScreen NOTIFY filterByScreenChanged)
Q_PROPERTY(bool demandingAttentionSkipsFilters READ demandingAttentionSkipsFilters WRITE setDemandingAttentionSkipsFilters)
```

### demandingAttentionSkipsFilters

Default value: `true`

When `true`, tasks that are demanding attention bypass virtual desktop and activity filters and always appear. **This is the correct behavior for a dock** — keep the default.

```cpp
// Already the default, but explicit for clarity:
filterModel->setDemandingAttentionSkipsFilters(true);
```

### Combined Screen + Virtual Desktop Filtering (for Multi-Monitor Mode)

When each dock on a different screen should show only windows from its screen AND current desktop:

```cpp
// Per-dock filter model
m_filterModel->setFilterByVirtualDesktop(true);
m_filterModel->setVirtualDesktop(virtualDesktopInfo->currentDesktop());
m_filterModel->setFilterByScreen(true);
m_filterModel->setScreenGeometry(m_screen->geometry());  // This dock's screen

// Keep virtual desktop current:
connect(virtualDesktopInfo, &TaskManager::VirtualDesktopInfo::currentDesktopChanged,
    this, [this, virtualDesktopInfo]() {
        m_filterModel->setVirtualDesktop(virtualDesktopInfo->currentDesktop());
    });

// Keep screen geometry current:
connect(m_screen, &QScreen::geometryChanged, this, [this]() {
    m_filterModel->setScreenGeometry(m_screen->geometry());
});
```

---

## M8 Virtual Desktop Mode Implementation

### Mode 1: Current Desktop Only

```
filterByVirtualDesktop = true
virtualDesktop = virtualDesktopInfo.currentDesktop
```

Windows on other desktops are hidden from the model. Windows on all desktops (`IsOnAllVirtualDesktops`) always appear.

### Mode 2: All Desktops (with opacity differentiation)

```
filterByVirtualDesktop = false  // Show all windows
```

Use a custom filter/decorator in QML to reduce opacity of "other desktop" windows:

```qml
// In DockItem.qml
property bool isOnCurrentDesktop: {
    var desktops = model.VirtualDesktops
    var current = virtualDesktopInfo.currentDesktop
    return model.IsOnAllVirtualDesktops || desktops.indexOf(current) >= 0
}

opacity: isOnCurrentDesktop ? 1.0 : 0.4
```

**Note**: This approach uses QML-level opacity without changing the model, which avoids list rebuild when switching desktops.

---

## ActivityInfo Class

**Header**: `<taskmanager/activityinfo.h>`

| Property/Method | Type | Description |
|----------------|------|-------------|
| `currentActivity` | QString | Currently active activity ID |
| `numberOfRunningActivities` | int | Number of running activities |
| `runningActivities()` | QStringList | Running activity ID list |
| `activityName(id)` | QString | Activity name |
| `activityIcon(id)` | QString | Activity icon name/path |

### Signals
- `currentActivityChanged`
- `numberOfRunningActivitiesChanged`
- `namesOfRunningActivitiesChanged`

---

## Notes

- `IsOnAllVirtualDesktops == true` must always be included in current-desktop-only displays
- `VirtualDesktops` role returns `QVariantList` — a window can be on multiple desktops simultaneously (Wayland)
- X11: a window is on one desktop OR all desktops (no multi-desktop membership)
- Desktop switching signals may arrive before `dataChanged` for affected windows — use both
- `VirtualDesktopInfo` uses a static private (`static Private *d`) — all instances share the same backend connection; creating multiple instances is safe and cheap
- Krema already tracks `currentDesktopChanged` in `DockModel` (connected to `m_tasksModel->setVirtualDesktop()`) — M8 only needs to add `setFilterByVirtualDesktop(true)` to enable filtering
