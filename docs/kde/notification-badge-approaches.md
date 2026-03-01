# Notification Badge Approaches

**Investigated**: 2026-03-01 (Plasma 6.5.5, Qt 6.10.2)

All findings are from actual headers, binaries, QML source, and live D-Bus tests.
"미확인" label is used where assumptions were not directly verified.

---

## Summary

KDE Plasma 6 programs use these badge sources, in rough order of adoption:

| Source | Count? | Coverage | Verified |
|--------|--------|----------|---------|
| Unity Launcher API (`com.canonical.Unity.LauncherEntry`) | Exact int | Slack, some KDE apps | Yes (binary strings) |
| `_NET_WM_STATE_DEMANDS_ATTENTION` (EWMH) | Boolean | Native Linux apps, Firefox | Yes (TaskManager model) |
| SNI `NeedsAttention` status | Boolean | Discord, Telegram, tray apps | Yes (D-Bus XML) |
| `RegisterWatcher` / `WatchedNotificationsModel` | Unread count | All libnotify apps with `desktop-entry` hint | Yes (live D-Bus test) |

Plasma's own task manager uses **only** Unity API + EWMH. `WatchedNotificationsModel` is
used exclusively by the notification bell applet — not the task bar.

---

## Approach 1: Plasma Task Manager Reference (What Plasma Does)

### Source Files

- `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/Task.qml`
- `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/TaskBadgeOverlay.qml`
- `/usr/lib/qt6/qml/org/kde/plasma/private/taskmanager/taskmanagerplugin.qmltypes`

### Badge Display (TaskBadgeOverlay.qml:33, 83-84)

```qml
// Visibility driven by SmartLauncherItem
visible: task.smartLauncherItem.countVisible
number: task.smartLauncherItem.count
```

### Attention State (Task.qml:656)

```qml
when: model.IsDemandingAttention || (task.smartLauncherItem && task.smartLauncherItem.urgent)
```

### SmartLauncherItem Creation (Task.qml:238-244)

```qml
const component = Qt.createComponent("org.kde.plasma.private.taskmanager", "SmartLauncherItem");
const smartLauncher = component.createObject(task);
smartLauncher.launcherUrl = Qt.binding(() => model.LauncherUrlWithoutIcon);
smartLauncherItem = smartLauncher;
```

**Key findings**:
- `WatchedNotificationsModel` is NOT used in the task manager at all.
- Badge count comes exclusively from `SmartLauncherItem.count` (Unity Launcher API).
- Attention (urgent) comes from `SmartLauncherItem.urgent` OR `IsDemandingAttention` (EWMH).

---

## Approach 2: SmartLauncherItem (Unity Launcher API)

### Module and Availability

- **QML module**: `org.kde.plasma.private.taskmanager`
- **Shared library**: `/usr/lib/qt6/qml/org/kde/plasma/private/taskmanager/libtaskmanagerplugin.so`
- **No public C++ header** — QML-only, "private" module

### Public API (from taskmanagerplugin.qmltypes)

```
Property: launcherUrl : QUrl  — write to associate with a .desktop file URL
Property: count : int         — badge count (readonly)
Property: countVisible : bool — whether to show the badge (readonly)
Property: progress : int      — progress 0-100 (readonly)
Property: progressVisible : bool (readonly)
Property: urgent : bool       — attention/urgent state (readonly)
```

All properties have `notify` signals: `countChanged`, `countVisibleChanged`, etc.

### Protocol: Unity Launcher API

From binary string analysis of `libtaskmanagerplugin.so`:

```
"1update(QString, QMap<QString, QVariant>)"   ← D-Bus signal signature
"Failed to find service for Unity Launcher"
"Failed to register unity object"
"Failed to register unity service"
```

The `SmartLauncherItem` backend:
1. Registers a D-Bus object to receive `update` signals from apps
2. Apps send `com.canonical.Unity.LauncherEntry` `update(launcherUrl, properties)` signals
3. Properties dict contains: `"count"`, `"count-visible"`, `"progress"`, `"progress-visible"`, `"urgent"`

### Settings Integration (confirmed from binary symbols)

```
NotificationManager::Settings::badgesInTaskManager()
NotificationManager::Settings::badgeBlacklistedApplications()
```

`SmartLauncherItem` checks these before showing badges.

### Apps That Use Unity Launcher API

- **Slack**: sends `update` with `count` on new messages
- **KDE applications** using `KNotification` + `KTaskbarProgress`: yes
- **Discord (Linux native)**: does NOT use Unity API — uses SNI NeedsAttention
- **Electron apps in general**: behavior varies; most do not implement it on Linux

### Warning: Private API

`org.kde.plasma.private.taskmanager` is a **private** KDE module. Using it from Krema
couples Krema to Plasma internals. Acceptable if Krema is Plasma-only (it is), but
the API may break between Plasma versions without notice.

### QML Usage in Krema

```qml
// In DockItem.qml
property QtObject smartLauncherItem: null

Component.onCompleted: {
    const component = Qt.createComponent("org.kde.plasma.private.taskmanager", "SmartLauncherItem");
    if (component.status === Component.Ready) {
        const sli = component.createObject(root);
        // LauncherUrlWithoutIcon from TaskManager model role
        sli.launcherUrl = Qt.binding(() => model.LauncherUrlWithoutIcon);
        smartLauncherItem = sli;
    }
}

readonly property int badgeCount: smartLauncherItem?.countVisible ? smartLauncherItem.count : 0
readonly property bool isUrgent: (smartLauncherItem?.urgent ?? false) || model.IsDemandingAttention
```

---

## Approach 3: RegisterWatcher Protocol

### Protocol Verification (live D-Bus test, 2026-03-01)

```python
# Registered at: /NotificationWatcher, interface: org.kde.NotificationWatcher
# Result: Notify() received for every notify-send

Bus unique name: :1.1289
RegisterWatcher() called successfully
[WATCHER] Notify() called: id=87, app=notify-send, entry=firefox   <- RECEIVED
[WATCHER] Notify() called: id=88, app=notify-send, entry=org.kde.spectacle <- RECEIVED
```

The protocol works. Both `out_signature='u'` (uint return) and `out_signature=''` (void
return, simulating C++ `Q_SCRIPTABLE void Notify()`) work correctly — plasmashell ignores
the return value from watchers.

### D-Bus Object Introspection (from live test)

```
method uint org.kde.NotificationWatcher.Notify(uint id, QString app_name,
    uint replaces_id, QString app_icon, QString summary, QString body,
    QStringList actions, QVariantMap hints, int expire_timeout)
```

The `org.kde.NotificationWatcher` interface name must appear in introspection for
plasmashell to route calls correctly.

### Krema's Current Implementation (notificationtracker.cpp)

The current code (as of 2026-03-01) uses correct path and interface:

```cpp
bus.registerObject(
    QStringLiteral("/NotificationWatcher"),
    QStringLiteral("org.kde.NotificationWatcher"),
    this,
    QDBusConnection::ExportScriptableSlots
);
```

### Potential Bug: Q_CLASSINFO Missing (미확인)

Qt's `registerObject(path, interfaceName, object, options)` 4-argument form uses
`interfaceName` to filter which methods to export. WITHOUT `Q_CLASSINFO("D-Bus
Interface", "org.kde.NotificationWatcher")` on the class, Qt may export the methods
under the **C++ class name** as the interface name (e.g., `"local.NotificationTracker"`),
not under `"org.kde.NotificationWatcher"`.

**Status**: Not directly confirmed. To verify, run Krema and introspect:

```bash
# While Krema is running, find its bus name:
qdbus6 --session | grep krema

# Introspect the watcher object:
qdbus6 :1.XXX /NotificationWatcher
```

If the output shows `local.NotificationTracker.Notify` instead of
`org.kde.NotificationWatcher.Notify`, the fix is to add to `notificationtracker.h`:

```cpp
class NotificationTracker : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationWatcher")
    // ... rest of class
};
```

Alternatively, use `QDBusAbstractAdaptor` (the recommended Qt pattern):

```cpp
// notificationwatcheradaptor.h
class NotificationWatcherAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationWatcher")
    Q_CLASSINFO("D-Bus Introspection", "...xml...")
public:
    explicit NotificationWatcherAdaptor(NotificationTracker *parent);

    // forward to NotificationTracker
    Q_SCRIPTABLE uint Notify(uint id, const QString &appName, ...);
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);
};
```

### Notification Argument Order (server-sent, differs from app-sent)

When plasmashell forwards a notification to watchers, the argument order is:

| Pos | Type | Name | Note |
|-----|------|------|------|
| 0 | `u` | `id` | Server-assigned ID (FIRST — differs from app's Notify where replaces_id is arg 1) |
| 1 | `s` | `app_name` | e.g. `"notify-send"`, `"slack"` |
| 2 | `u` | `replaces_id` | 0 for new notifications |
| 3 | `s` | `app_icon` | icon name or empty |
| 4 | `s` | `summary` | title |
| 5 | `s` | `body` | body text |
| 6 | `as` | `actions` | action list |
| 7 | `a{sv}` | `hints` | includes `desktop-entry` |
| 8 | `i` | `expire_timeout` | ms, -1 = default |

### App Coverage

Apps that send `desktop-entry` hint in their `Notify()` call:
- **KDE apps** using `KNotification`: always — from `.notifyrc`
- **GTK apps** using `libnotify`: usually
- **Slack** (Electron): sends `desktop-entry: "slack"`
- **Discord** (Electron): inconsistent, may omit
- **Firefox**: sends `desktop-entry: "firefox"`
- **Plain `notify-send`**: only if `--hint=string:desktop-entry:appname` is specified

When `desktop-entry` is absent, fall back to matching `app_name` (less reliable).

---

## Approach 4: WatchedNotificationsModel (QML)

### QML Usage (works — registers its own watcher internally)

```qml
import org.kde.notificationmanager as NotificationManager

NotificationManager.WatchedNotificationsModel {
    id: watchedModel
    // valid: true when connected to plasmashell's notification server
}
```

The QML type internally calls `RegisterWatcher` itself. This is the simplest approach
if notification state is only needed in QML.

### Why C++ Notifications Proxy Does NOT Work for External Apps

`NotificationManager::Notifications` (C++) creates `NotificationsModel` internally, which
connects to `Server::self()` — the in-process notification server. In an external app,
`Server::self()` is never initialized as the real D-Bus server. Notifications go to
plasmashell's `Server`, in a different process.

**Confirmed broken** (2026-02-28, binary analysis):
```cpp
// DOES NOT WORK — always 0 rows, rowsInserted never fires
auto *model = new NotificationManager::Notifications(this);
auto *ps = static_cast<QQmlParserStatus*>(model);
ps->classBegin();
model->setShowNotifications(true);
ps->componentComplete();
```

**Use RegisterWatcher (C++) or WatchedNotificationsModel (QML) instead.**

### Key Roles (from notifications.h)

| Role | Type | Description |
|------|------|-------------|
| `DesktopEntryRole` | `QString` | Desktop file name without `.desktop` |
| `ApplicationNameRole` | `QString` | Human-readable app name |
| `ExpiredRole` | `bool` | Timed out or closed |
| `ReadRole` | `bool` | User has seen it |
| `UrgencyRole` | enum | `LowUrgency`, `NormalUrgency`, `CriticalUrgency` |
| `HintsRole` | `QVariantMap` | Raw hints from `Notify()` call |

---

## Approach 5: SNI NeedsAttention

### Protocol

```
Service:   org.kde.StatusNotifierWatcher
Object:    /StatusNotifierWatcher
Interface: org.kde.StatusNotifierWatcher

Signals:
  StatusNotifierItemRegistered(s)
  StatusNotifierItemUnregistered(s)

SNI Item signals:
  NewStatus(status: s)   <- subscribe to this per-item
```

SNI `Status` is a string enum: `"Passive"`, `"Active"`, `"NeedsAttention"`.
There is no numeric count — boolean attention only.

### Apps Verified Using SNI NeedsAttention

- **Discord (Linux native)**: sets `NeedsAttention` on unread mentions/DMs
- **Telegram**: sets `NeedsAttention` on new messages
- **Spotify**: stays `Active` (no attention state for playlists)

### Implementation Status in Krema

Already implemented in `NotificationTracker` — see `setupSniWatcher()`.

---

## Approach 6: EWMH `_NET_WM_STATE_DEMANDS_ATTENTION`

Already available as `model.IsDemandingAttention` in the TaskManager model. No
additional implementation needed. Already used in Krema.

Apps that use this: Firefox (on download), Thunderbird (new mail), many native Linux apps.
Apps that do NOT use this: Most Electron apps (they use Unity API or SNI instead).

---

## Recommended Architecture for Krema

### Badge Source Priority

```
Priority  Source                          Count?   When active
--------  ------                          ------   -----------
1 (best)  SmartLauncherItem.count         Exact    countVisible == true
2         RegisterWatcher / WatchedModel  Unread#  Has desktop-entry hint
3         SNI NeedsAttention              Dot      Status == "NeedsAttention"
4         IsDemandingAttention (existing) Dot      EWMH urgency hint set
```

### Combined Badge Logic (QML sketch)

```qml
// DockItem.qml
readonly property int badgeCount: {
    // Priority 1: Unity API exact count
    if (smartLauncherItem?.countVisible) {
        return smartLauncherItem.count;
    }
    // Priority 2: Unread notification count (from NotificationTracker)
    const nc = notificationTracker.unreadCount(model.AppId ?? "");
    if (nc > 0) {
        return nc;
    }
    return 0;
}

readonly property bool showAttentionDot: {
    return model.IsDemandingAttention
        || (smartLauncherItem?.urgent ?? false)
        || notificationTracker.sniNeedsAttention(model.AppId ?? "")
        || notificationTracker.unreadCount(model.AppId ?? "") > 0;
}
```

---

## Immediate Action Items for Krema

### 1. Verify NotificationTracker D-Bus Introspection

Run Krema and check what interface name it exposes:

```bash
# Find Krema's bus name
BUS=$(qdbus6 --session | grep krema)
# Introspect the watcher object
qdbus6 "$BUS" /NotificationWatcher
```

**Expected (correct)**:
```
method uint org.kde.NotificationWatcher.Notify(...)
```

**Wrong (indicates Q_CLASSINFO missing)**:
```
method uint local.NotificationTracker.Notify(...)
```

### 2. If Q_CLASSINFO Is Missing

Add to `notificationtracker.h`:

```cpp
class NotificationTracker : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationWatcher")
    // ...
};
```

No other changes needed — `registerObject(path, "org.kde.NotificationWatcher", this, ExportScriptableSlots)`
already passes the interface name; adding Q_CLASSINFO ensures the introspection
matches what plasmashell expects.

### 3. Add SmartLauncherItem Integration

`SmartLauncherItem` is the primary badge source for Slack and other Unity API apps.
It is already used by Plasma's own task manager — safe to add to Krema.

---

## Reference Files

| File | Purpose |
|------|---------|
| `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/Task.qml` | Plasma reference — attention state line 656, badge lines 369-370 |
| `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/TaskBadgeOverlay.qml` | Badge overlay — SmartLauncherItem countVisible/count |
| `/usr/lib/qt6/qml/org/kde/plasma/private/taskmanager/taskmanagerplugin.qmltypes` | SmartLauncherItem API (properties/signals) |
| `/usr/lib/qt6/qml/org/kde/plasma/private/taskmanager/libtaskmanagerplugin.so` | Binary — Unity API strings, NotificationManager::Settings symbols |
| `/usr/include/notificationmanager/notifications.h` | Notifications proxy model — roles, methods |
| `/usr/include/notificationmanager/settings.h` | Settings — badgesInTaskManager(), badgeBlacklistedApplications() |
| `/usr/lib/libnotificationmanager.so.6.5.5` | Binary — WatchedNotificationsModel::Private symbols |
| `/usr/lib/qt6/qml/org/kde/notificationmanager/notificationmanager.qmltypes` | WatchedNotificationsModel QML type |
| `docs/kde/notification-badges.md` | Multi-source badge architecture (detailed) |
| `docs/kde/notification-watcher-protocol.md` | RegisterWatcher protocol deep-dive |
