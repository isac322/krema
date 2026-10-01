# Notification Badges for All Apps

Reference for Krema's notification badges: showing badges/counts on dock icons, including apps
that do NOT support the Unity Launcher API.

**Headers**: `/usr/include/notificationmanager/`
**Package**: `plasma-workspace` (`PW::LibNotificationManager`)
**QML Module**: `org.kde.notificationmanager`

---

## Sources in Krema

Krema combines four badge sources:
1. `LauncherEntryTracker` (C++) — Unity Launcher API `com.canonical.Unity.LauncherEntry` (exact count, progress, urgent)
2. `NotificationTracker` (C++) — unread notifications via `RegisterWatcher` (per `desktop-entry`)
3. `NotificationTracker` (C++) — SNI `NeedsAttention` (boolean)
4. `model.IsDemandingAttention` — EWMH `_NET_WM_STATE_DEMANDS_ATTENTION` (boolean, all apps)

Most Electron apps (Slack, Discord, etc.) do NOT send Unity Launcher API signals on Linux, so
sources 2–4 cover the rest.

---

## Approach 1: RegisterWatcher (Notification Watcher)

### Architecture

```
App → Notify() → org.freedesktop.Notifications (plasmashell owns it)
                        ↓ forwards to every registered watcher
              RegisterWatcher() → Krema NotificationTracker (passive)
                                        ↓
                          per-desktop-entry unread counts → QML (badge counts)
```

A watcher is **passive**: it does NOT own the notification service. It calls
`org.kde.NotificationManager.RegisterWatcher()` and plasmashell then calls `Notify()` on it for
every new notification. `WatchedNotificationsModel` (QML only, no installed C++ header) uses the
same protocol.

### `Notifications` proxy does NOT work in external apps

`NotificationManager::Notifications` created from C++ stays at 0 rows. Its source model connects
to the in-process `Server::self()`, which is never the real D-Bus server outside plasmashell.
Use `RegisterWatcher` (C++) or `WatchedNotificationsModel` (QML) instead.

### Protocol (implemented in `src/models/notificationtracker.cpp`)

1. Register an object at **`/NotificationWatcher`** with interface **`org.kde.NotificationWatcher`**
   (`registerObject(..., ExportScriptableSlots)`, plus `Q_CLASSINFO("D-Bus Interface", ...)`).
   The server hardcodes this path/interface; `/org/freedesktop/Notifications` never receives calls.
2. Call `RegisterWatcher()` on `org.freedesktop.Notifications` `/org/freedesktop/Notifications`,
   interface `org.kde.NotificationManager`. The unique bus name (`:1.xxx`) is enough.
3. The server calls `Notify` with signature `ususssasa{sv}i`: the server-assigned `id` comes
   FIRST (unlike the spec's `Notify`, where `replaces_id` is first and returns the id).
4. `NotificationClosed(uint id, uint reason)` arrives as a **broadcast signal** on
   `org.freedesktop.Notifications`, not as a directed call — subscribe with `QDBusConnection::connect()`.
5. Call `UnRegisterWatcher()` on shutdown.

Only notifications sent after registration are received. `RegisterWatcher` needs plasmashell
(owner of `org.freedesktop.Notifications`). Quick check: `qdbus6 <krema-bus-name> /NotificationWatcher`
must list `org.kde.NotificationWatcher.Notify`.

### Key Roles Available

| Role | Type | Description |
|------|------|-------------|
| `DesktopEntryRole` | `QString` | Desktop file name without .desktop (e.g. `"slack"`, `"org.kde.spectacle"`) |
| `ApplicationNameRole` | `QString` | Human-readable app name (e.g. `"Slack"`) |
| `ApplicationIconNameRole` | `QString` | Icon name |
| `ExpiredRole` | `bool` | Notification timed out / was closed |
| `ReadRole` | `bool` | User has seen it |
| `UrgencyRole` | `Notifications::Urgency` | `LowUrgency`, `NormalUrgency`, `CriticalUrgency` |
| `SummaryRole` | `QString` | Notification title |
| `TypeRole` | `Notifications::Type` | `NotificationType` or `JobType` |
| `CreatedRole` | `QDateTime` | When first sent |
| `HintsRole` | `QVariantMap` | Raw hints dict from Notify() call |

### What `DesktopEntryRole` Contains

The `desktop-entry` hint from the `Notify()` D-Bus call is used to populate `DesktopEntryRole`.
This is how apps identify themselves to the notification server.

- **KDE apps using KNotification**: Always send proper `desktop-entry` hint (from .notifyrc)
- **GTK apps**: Often send `desktop-entry` hint
- **Slack**: Sends `desktop-entry: "slack"` via Electron notification API
- **Discord**: May or may not send `desktop-entry` (varies by version/platform)
- **Electron apps**: Typically send `app_name` but may omit `desktop-entry` hint

**Badge count derivation**: `NotificationTracker` counts notification IDs per desktop entry from
`Notify()` until `NotificationClosed` (or `clearUnreadNotifications()` when the app is activated).

This is NOT the same as the app-reported badge count from Unity API, but it's the best
approximation available for all apps. When the `desktop-entry` hint is missing,
`NotificationTracker` falls back to `app_name` (normalized through KService).

### Feasibility Assessment

| Factor | Assessment |
|--------|-----------|
| Implementation effort | Medium — D-Bus watcher in C++ |
| Gives badge COUNT | Yes (unread notification count, not app-reported) |
| Works for all apps | Only apps that use `org.freedesktop.Notifications` |
| Privacy concerns | Low — reading already-delivered notifications |
| Permission requirements | None — passive watcher |
| Requires plasmashell | Yes — no `RegisterWatcher` without Plasma's notification server |

### Settings Integration

The `NotificationManager::Settings` class tracks whether badges should show in task managers:

```cpp
#include <notificationmanager/settings.h>

NotificationManager::Settings settings;
settings.load();
if (settings.badgesInTaskManager()) {
    // Show notification-based badges
}
// Check if specific app is blacklisted from badges:
QStringList blacklisted = settings.badgeBlacklistedApplications();
```

This respects user's notification settings from System Settings → Notifications.

---

## Approach 2: StatusNotifierItem (SNI) Monitoring

### What It Provides

SNI `Status` is a **boolean enum** — it cannot provide badge counts.

```
enum ItemStatus {
    Passive = 1,        // Hidden / not important
    Active = 2,         // Normal running state
    NeedsAttention = 3, // Urgent — equivalent to "has notification"
};
```

### Apps That Use SNI NeedsAttention

- **Discord** (Linux native app): Sets `NeedsAttention` on mentions/DMs
- **Telegram**: Sets `NeedsAttention` on new messages
- **KDE applications** (most): Use SNI with attention for urgent events
- **Spotify**: Uses SNI but typically stays at `Active`
- **Slack**: Usually uses Unity Launcher API instead

### SNI Monitoring Implementation

```cpp
// Step 1: Watch StatusNotifierWatcher for new SNI registrations
QDBusConnection::sessionBus().connect(
    "org.kde.StatusNotifierWatcher",
    "/StatusNotifierWatcher",
    "org.kde.StatusNotifierWatcher",
    "StatusNotifierItemRegistered",
    this, SLOT(onSniRegistered(QString))
);

// Step 2: Query current registered items
QDBusInterface watcher("org.kde.StatusNotifierWatcher",
                       "/StatusNotifierWatcher",
                       "org.kde.StatusNotifierWatcher",
                       QDBusConnection::sessionBus());
QStringList items = watcher.property("RegisteredStatusNotifierItems").toStringList();
// Format: "bus_name/object_path" e.g. ":1.164/org/ayatana/NotificationItem/spotify_client"

// Step 3: For each item, subscribe to NewStatus signal
void onSniRegistered(const QString &serviceAndPath) {
    auto parts = serviceAndPath.split('/');
    QString service = parts[0];
    QString path = "/" + QStringList(parts.mid(1)).join('/');

    QDBusConnection::sessionBus().connect(
        service, path,
        "org.kde.StatusNotifierItem",
        "NewStatus",
        this, SLOT(onSniStatusChanged(QString))
    );

    // Read current Status
    QDBusInterface sni(service, path, "org.kde.StatusNotifierItem",
                       QDBusConnection::sessionBus());
    QString id = sni.property("Id").toString();    // e.g. "discord"
    QString status = sni.property("Status").toString(); // "Active", "NeedsAttention"
    QString title = sni.property("Title").toString();
}
```

### SNI → Dock Item Correlation

The SNI `Id` field typically matches the `.desktop` file stem:
- SNI `Id: "discord"` → desktop file `discord.desktop` → TaskManager `AppId: "discord"`
- SNI `Id: "spotify"` → `spotify.desktop`
- SNI `Id: "kime"` (input method) → no corresponding dock item

**Match algorithm**:
```cpp
// Compare SNI Id with TaskManager AppId (lowercase desktop file stem)
bool matchSniToDockItem(const QString &sniId, const QString &appId) {
    return sniId.toLower() == appId.toLower() ||
           appId.contains(sniId, Qt::CaseInsensitive);
}
```

### Feasibility Assessment

| Factor | Assessment |
|--------|-----------|
| Implementation effort | Medium (D-Bus boilerplate) |
| Gives badge COUNT | No — boolean only |
| Works for all apps | No — only apps with system tray icons |
| Privacy concerns | None |
| Permission requirements | None |

---

## Approach 3: Direct `org.freedesktop.Notifications` D-Bus Signal Subscription

### Why This Does NOT Work for Monitoring

The `Notify()` call is a **method**, not a signal — there is no signal emitted when `Notify()` is called.
The `org.freedesktop.Notifications` interface only exposes these signals:
- `NotificationClosed(uint id, uint reason)` — emitted AFTER closing
- `ActionInvoked(uint id, QString action_key)` — after user action
- `NotificationReplied(uint id, QString text)` — after reply

You cannot subscribe to intercept `Notify()` calls without being the server or using `RegisterWatcher()`.

**The correct approach is `RegisterWatcher` (Approach 1).**

---

## Approach 4: `_NET_WM_STATE_DEMANDS_ATTENTION` (Already Implemented)

Already available as `model.IsDemandingAttention` in TaskManager model.

**Signal source**: KWin monitors `_NET_WM_STATE_DEMANDS_ATTENTION` EWMH property.
When a window's urgency hint changes, KWin propagates it through TaskManager.

**Electron apps**: Most do NOT set this hint — they rely on Taskbar/Unity API instead.
**Native Linux apps**: Usually DO set it (Firefox, Thunderbird, etc.)

---

## Plasma's Approach (What the Task Manager Does)

Plasma Task Manager uses **only** two sources — confirmed by reading Task.qml:

```qml
// Task.qml:655-661
State {
    name: "attention"
    when: model.IsDemandingAttention ||
          (task.smartLauncherItem && task.smartLauncherItem.urgent)
}

// Badge:
active: task.smartLauncherItem && task.smartLauncherItem.countVisible
```

**Plasma does NOT use `WatchedNotificationsModel` in the task manager.**
That is used only by the notification bell applet and notification history.

`SmartLauncherItem` checks `NotificationManager::Settings::badgesInTaskManager()` and
`badgeBlacklistedApplications()` at runtime. Since Plasma 6.6 it is compiled into the task
manager applet plugin and cannot be imported by other processes; Krema's `LauncherEntryTracker`
ports its backend semantics and applies the same settings.

---

## Krema's Architecture

### Badge Source Priority

```
Priority  Source                                     Count    Coverage
--------  ------                                     -----    --------
1 (best)  LauncherEntryTracker.count                 Exact    KDE apps, Slack (Unity API)
2         NotificationTracker.unreadCount            Unread#  Apps using org.freedesktop.Notifications
3         NotificationTracker.sniNeedsAttention      Boolean  Discord, Telegram, tray apps
4         IsDemandingAttention                       Boolean  X11 urgency hint apps
```

### Combined Badge Logic (`src/qml/DockItem.qml`)

```qml
readonly property int _badgeCount: {
    let _rev = NotificationTracker.revision  // reactive dependency
    if (_launcherCountVisible)               // 1st: Unity API exact count
        return _launcherCount
    if (_appId.length > 0) {                 // 2nd: unread notification count
        let n = NotificationTracker.unreadCount(_appId)
        if (n > 0) return n
    }
    return 0
}

readonly property bool _isDemandingAttention: {
    let _rev = NotificationTracker.revision
    return (model.IsDemandingAttention ?? false)
        || _launcherUrgent
        || (_appId.length > 0 && NotificationTracker.sniNeedsAttention(_appId))
}
```

Both trackers are C++ singletons with a `revision` property for reactive QML bindings and
`Q_INVOKABLE` lookups, so QML never iterates notifications per dock item.

---

## CMake Integration

```cmake
# Find LibNotificationManager (from plasma-workspace)
find_package(LibNotificationManager REQUIRED)

# Link target
target_link_libraries(krema_lib PRIVATE PW::LibNotificationManager)

# Headers are at: /usr/include/notificationmanager/
```

**CMake target**: `PW::LibNotificationManager`
**CMake find_package name**: `LibNotificationManager`
**Config file**: `/usr/lib/cmake/LibNotificationManager/LibNotificationManagerConfig.cmake`
**Shared library**: `libnotificationmanager.so` (soname: `libnotificationmanager.so.1`)
**Qt minimum version required by config**: `Qt6 6.9.0`

**INTERFACE_LINK_LIBRARIES** (pulled in transitively via `find_dependency`):
`Qt6::Core`, `Qt6::Gui`, `Qt6::Quick`, `KF6::ItemModels`

**IMPORTED_LINK_DEPENDENT_LIBRARIES** (runtime deps, not transitive for consumers):
`Qt6::DBus`, `KF6::ConfigGui`, `KF6::I18n`, `KF6::WindowSystem`, `KF6::ItemModels`,
`KF6::Notifications`, `KF6::KIOFileWidgets`, `Plasma::Plasma`, `PW::LibTaskManager`,
`KF6::Screen`, `KF6::Service`, `Qt6::Qml`

> Note: The IMPORTED_LINK_DEPENDENT list (runtime deps) is NOT transitively propagated.
> If Krema code directly uses Qt6::DBus etc., link them explicitly.

---

## Privacy and Permission Concerns

- **Notification watcher**: Receives notification content (summary, body, app name) — the same
  data the notification bell applet reads. No special permissions needed.
  `NotificationTracker` stores only notification IDs per desktop entry, not bodies.

- **SNI monitoring**: Only reads app name, status, and icon. No sensitive data.

- **Respect user settings**: Always check `NotificationManager::Settings::badgesInTaskManager()`
  and `badgeBlacklistedApplications()` before showing any notification-derived badge.

---

## Known Limitations

0. **`Notifications` proxy is broken for external apps** — `Notifications::componentComplete()`
   creates `NotificationsModel` which connects to in-process `Server::self()`, not plasmashell's
   server. Use the D-Bus watcher (see above) or `WatchedNotificationsModel` via QML instead.

1. **`desktop-entry` hint is optional** — apps that don't send it cannot be correlated exactly.
   `app_name` is always present but may differ from the desktop file stem; `NotificationTracker`
   falls back to it.

2. **Requires plasmashell** — without Plasma's notification server there is no `RegisterWatcher`.

3. **"Unread" is dock-managed** — Krema clears an app's count when it is focused, from the
   context menu, or when its SNI returns to `Active`; it does not share read state with the
   notification bell applet.

4. **Electron apps** (Discord, VS Code, etc.) — Notification behavior varies by Electron version.
   Older versions may send `app_name` without `desktop-entry`.

5. **SNI ID matching** — Not standardized. Heuristic matching may produce false positives for
   similarly-named apps.

---

## Verified API Signatures (from headers)

### `NotificationManager::Notifications` — `/usr/include/notificationmanager/notifications.h`

```cpp
namespace NotificationManager {

class Notifications : public QSortFilterProxyModel, public QQmlParserStatus {
    Q_OBJECT
    QML_ELEMENT

public:
    // Constructor
    explicit Notifications(QObject *parent = nullptr);

    // Key configuration setters (all have matching Q_PROPERTY + notify signal)
    void setShowNotifications(bool showNotifications);  // default: true
    void setShowJobs(bool showJobs);                    // default: false
    void setShowExpired(bool show);                     // default: false
    void setShowDismissed(bool show);                   // default: false
    void setLimit(int limit);                           // default: 0 (no limit)
    void setBlacklistedDesktopEntries(const QStringList &blacklist);
    void setWhitelistedDesktopEntries(const QStringList &whitelist);
    void setUrgencies(Urgencies urgencies);             // default: all
    void setSortMode(SortMode sortMode);                // default: SortByDate
    void setGroupMode(GroupMode groupMode);             // default: GroupDisabled

    // Aggregate counts (Q_PROPERTY)
    int count() const;
    int activeNotificationsCount() const;              // non-expired
    int expiredNotificationsCount() const;
    int unreadNotificationsCount() const;              // added since lastRead
    int activeJobsCount() const;
    int jobsPercentage() const;

    // QAbstractItemModel interface
    QVariant data(const QModelIndex &index, int role) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Invokable actions
    Q_INVOKABLE void expire(const QModelIndex &idx);
    Q_INVOKABLE void close(const QModelIndex &idx);
    Q_INVOKABLE void clear(ClearFlags flags);  // ClearFlag::ClearExpired

    // lastRead tracking for "unread" count
    QDateTime lastRead() const;
    void setLastRead(const QDateTime &lastRead);
    void resetLastRead();

    enum Roles {
        IdRole = Qt::UserRole + 1,
        SummaryRole = Qt::DisplayRole,     // notification title
        ImageRole = Qt::DecorationRole,
        IsGroupRole = Qt::UserRole + 2,
        TypeRole,          // NotificationType or JobType
        CreatedRole,       // QDateTime
        UpdatedRole,       // QDateTime
        BodyRole,          // QString
        IconNameRole,      // QString
        DesktopEntryRole,  // QString — "slack", "org.kde.spectacle" (no .desktop)
        NotifyRcNameRole,  // QString — "spectaclerc"
        ApplicationNameRole,      // QString — "Spectacle"
        ApplicationIconNameRole,  // QString
        OriginNameRole,    // QString — device/account source
        UrgencyRole,       // Urgency enum: LowUrgency|NormalUrgency|CriticalUrgency
        TimeoutRole,       // int — ms, 0=no timeout, -1=default
        ExpiredRole,       // bool — timed out / closed
        DismissedRole,     // bool — temporarily hidden by user
        ReadRole,          // bool — user has seen it
        HintsRole,         // QVariantMap — raw Notify() hints dict (since 6.4)
        // ... more job/action roles
    };

    enum Type { NoType, NotificationType, JobType };
    enum Urgency { LowUrgency = 1, NormalUrgency = 2, CriticalUrgency = 4 };
    enum SortMode { SortByDate = 0, SortByTypeAndUrgency };
    enum GroupMode { GroupDisabled = 0, GroupApplicationsFlat };
    enum ClearFlag { ClearExpired = 1 << 1 };
    enum JobState { JobStateStopped, JobStateRunning, JobStateSuspended };

Q_SIGNALS:
    void countChanged();
    void activeNotificationsCountChanged();
    void unreadNotificationsCountChanged();
    void showNotificationsChanged();
    void showJobsChanged();
    void showExpiredChanged();
    // ... (one notify signal per Q_PROPERTY)
};

} // namespace NotificationManager
```

### `NotificationManager::Settings` — `/usr/include/notificationmanager/settings.h`

```cpp
namespace NotificationManager {

class Settings : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit Settings(QObject *parent = nullptr);

    // Must call load() to populate from disk (constructor does NOT auto-load)
    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void defaults();

    // Badge-relevant methods
    bool badgesInTaskManager() const;
    void setBadgesInTaskManager(bool enable);

    QStringList badgeBlacklistedApplications() const;
    // Note: no setter — managed via setApplicationBehavior()

    // Per-app behavior (use ShowBadges flag)
    enum NotificationBehavior {
        ShowPopups = 1 << 1,
        ShowPopupsInDoNotDisturbMode = 1 << 2,
        ShowInHistory = 1 << 3,
        ShowBadges = 1 << 4,
    };
    Q_INVOKABLE NotificationBehaviors applicationBehavior(const QString &desktopEntry) const;
    Q_INVOKABLE void setApplicationBehavior(const QString &desktopEntry, NotificationBehaviors behaviors);

    // Live reload (default: true — automatically reloads when config changes on disk)
    bool live() const;
    void setLive(bool live);

    // Other useful methods
    bool criticalPopupsInDoNotDisturbMode() const;
    bool jobsInNotifications() const;
    QStringList knownApplications() const;  // apps that have ever sent a notification

Q_SIGNALS:
    void settingsChanged();  // emitted for ALL property changes
    void knownApplicationsChanged();
};

} // namespace NotificationManager
```

### StatusNotifierWatcher D-Bus Interface (`/usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierWatcher.xml`)

```
Service:   org.kde.StatusNotifierWatcher
Object:    /StatusNotifierWatcher
Interface: org.kde.StatusNotifierWatcher

Properties (read):
  RegisteredStatusNotifierItems : as (QStringList)
    Format: "bus_name/object_path"  e.g. ":1.164/org/ayatana/NotificationItem/discord"
  IsStatusNotifierHostRegistered : b
  ProtocolVersion : i

Signals:
  StatusNotifierItemRegistered(s)    — new SNI appeared
  StatusNotifierItemUnregistered(s)  — SNI disappeared
  StatusNotifierHostRegistered()
  StatusNotifierHostUnregistered()

Methods:
  RegisterStatusNotifierItem(service: s)
  RegisterStatusNotifierHost(service: s)
```

### StatusNotifierItem D-Bus Interface (`/usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierItem.xml`)

```
Interface: org.kde.StatusNotifierItem

Properties (read):
  Category      : s  — "ApplicationStatus", "Communications", "SystemServices", "Hardware"
  Id            : s  — unique identifier, typically matches desktop file stem (e.g. "discord")
  Title         : s  — user-visible name
  Status        : s  — "Passive", "Active", "NeedsAttention"
  IconName      : s
  OverlayIconName : s
  AttentionIconName : s
  Menu          : o  — D-Bus object path for context menu
  WindowId      : i
  IconThemePath : s

Signals:
  NewTitle()
  NewIcon()
  NewStatus(status: s)    — emitted when status changes; subscribe to this for attention tracking
  NewAttentionIcon()
  NewOverlayIcon()
  NewToolTip()

Methods:
  Activate(x: i, y: i)
  SecondaryActivate(x: i, y: i)
  ContextMenu(x: i, y: i)
  Scroll(delta: i, orientation: s)
  ProvideXdgActivationToken(token: s)
```

## Reference Files

| File | Content |
|------|---------|
| `/usr/include/notificationmanager/notifications.h` | `Notifications` proxy model, all roles |
| `/usr/include/notificationmanager/notification.h` | `Notification` value type, per-item fields |
| `/usr/include/notificationmanager/settings.h` | `Settings` class — `badgesInTaskManager()`, `badgeBlacklistedApplications()` |
| `/usr/include/notificationmanager/server.h` | `Server` — NOT needed for passive watching |
| `/usr/lib/qt6/qml/org/kde/notificationmanager/notificationmanager.qmltypes` | QML types — `WatchedNotificationsModel`, `Notifications`, `Settings` |
| `/usr/lib/cmake/LibNotificationManager/LibNotificationManagerConfig.cmake` | CMake config |
| `/usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierItem.xml` | SNI D-Bus interface |
| `/usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierWatcher.xml` | StatusNotifierWatcher interface |
| `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/Task.qml` | Plasma reference (lines 655-661 attention, 369-370 badge) |
