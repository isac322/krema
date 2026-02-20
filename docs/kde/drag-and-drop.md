# Drag & Drop (TasksModel DnD)

> Source: `/usr/include/taskmanager/` headers

## Overview

TasksModel supports drag-and-drop for reordering tasks and launchers. DnD data is provided via roles (`MimeType`, `MimeData`) and reordering is done via `move()` + `syncLaunchers()`.

---

## Roles for DnD

| Role | Type | Description |
|------|------|-------------|
| `MimeType` | QString | MIME type string for the task |
| `MimeData` | QByteArray | Serialized data for the MIME type |

Common MIME types:
- `text/uri-list` — URL-based launcher tasks
- `application/x-kde-launcher-desktop` — .desktop file launchers

---

## Reordering

### move()

```cpp
// Move task at row to newPos within parent
bool TasksModel::move(int row, int newPos, const QModelIndex &parent = QModelIndex());
```

- Works in `SortManual` mode
- `row`: Current position in the model
- `newPos`: Target position
- `parent`: For grouped tasks, the group parent index (default = top level)
- Returns `true` if move succeeded

### syncLaunchers()

```cpp
// Persist current launcher order to config
void TasksModel::syncLaunchers();
```

- Must be called after `move()` to save the new order
- Updates the internal launcher list to match current visual order
- Without this call, launcher order reverts on next load

### Typical DnD Reorder Flow

```
1. User starts drag on task item
2. Track source index (row)
3. On drop, calculate target position
4. Call move(sourceRow, targetRow)
5. Call syncLaunchers() to persist
```

---

## Launcher Management

### Adding Launchers

```cpp
bool requestAddLauncher(const QUrl &url);
bool requestAddLauncherToActivity(const QUrl &url, const QString &activity);
```

- `url`: Can be `file:///path/to.desktop`, `applications:org.kde.dolphin.desktop`, or `preferred://browser`
- Returns `true` if launcher was added (not already present)

### Removing Launchers

```cpp
bool requestRemoveLauncher(const QUrl &url);
bool requestRemoveLauncherFromActivity(const QUrl &url, const QString &activity);
```

### Querying

```cpp
int launcherPosition(const QUrl &url) const;     // Position in model (-1 if not found)
QStringList launcherActivities(const QUrl &url);  // Activities for this launcher
```

### Launcher List

```cpp
QStringList launcherList() const;                 // All launcher URLs as strings
void setLauncherList(const QStringList &launchers); // Replace entire list
```

---

## URL Comparison

TaskManager uses `launcherUrlsMatch()` for comparing launcher URLs:

```cpp
enum UrlComparisonMode {
    Strict = 0,          // Exact match
    IgnoreQueryItems,    // Ignore query string (metadata)
};

bool launcherUrlsMatch(const QUrl &a, const QUrl &b, UrlComparisonMode mode = Strict);
```

Important: Query items often contain metadata (icon overrides, etc.) — use `IgnoreQueryItems` when comparing user-initiated URLs.

### URL Formats

| Format | Example | Description |
|--------|---------|-------------|
| File path | `file:///usr/share/applications/org.kde.dolphin.desktop` | Absolute .desktop file |
| Applications | `applications:org.kde.dolphin.desktop` | KService storage ID |
| Preferred | `preferred://browser` | Default app for category |

Supported preferred categories: `browser`, `mailer`, `terminal`, `filemanager`

Resolving preferred URLs: `TaskManager::defaultApplication(url)` returns the actual app ID.

---

## Activity-Scoped Launchers

Launchers can be scoped to specific activities:

- **All activities**: launcher activity list contains a null UUID (`{00000000-0000-0000-0000-000000000000}`)
- **Specific activities**: launcher activity list contains specific activity UUIDs
- **No activity**: launcher is visible nowhere (effectively hidden)

```cpp
// Add launcher for current activity only
model->requestAddLauncherToActivity(url, activityId);

// Add launcher for all activities
model->requestAddLauncher(url);  // defaults to all

// Check which activities
QStringList activities = model->launcherActivities(url);
```

---

## Detecting Task Type for DnD

```cpp
QModelIndex index = model->index(row, 0);

bool isLauncher = index.data(TaskManager::AbstractTasksModel::IsLauncher).toBool();
bool isWindow = index.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
bool isStartup = index.data(TaskManager::AbstractTasksModel::IsStartup).toBool();
bool hasLauncher = index.data(TaskManager::AbstractTasksModel::HasLauncher).toBool();
```

### DnD Behavior by Task Type

| Source | Drop Target | Action |
|--------|-------------|--------|
| Launcher | Launcher area | Reorder (`move` + `syncLaunchers`) |
| Window | Launcher area | Pin to dock (`requestAddLauncher`) |
| External URL | Dock | Add launcher (`requestAddLauncher`) |
| Launcher | Outside dock | Remove launcher (`requestRemoveLauncher`) |

---

## AppData for External Drops

When receiving external URL drops:

```cpp
#include <taskmanager/tasktools.h>

TaskManager::AppData appData = TaskManager::appDataFromUrl(droppedUrl);

if (!appData.id.isEmpty()) {
    // Valid app found — use appData.url for requestAddLauncher
    model->requestAddLauncher(appData.url);
}
```

---

## Model Index Helpers

```cpp
// Create model index for row (needed for request* methods)
QModelIndex idx = model->makeModelIndex(row);

// For grouped tasks: child within group
QModelIndex childIdx = model->makeModelIndex(groupRow, childRow);

// Persistent index (survives model changes)
QPersistentModelIndex persistent = model->makePersistentModelIndex(row);
```
