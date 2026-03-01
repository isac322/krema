# Notification Badges for All Apps

Research for Krema M8+ feature: showing notification badges/counts on dock icons for apps that
do NOT support the Unity Launcher API.

**Headers**: `/usr/include/notificationmanager/`
**Package**: `plasma-workspace` (`PW::LibNotificationManager`)
**QML Module**: `org.kde.notificationmanager`

---

## Problem Statement

Krema currently uses two badge sources:
1. `model.IsDemandingAttention` — EWMH `_NET_WM_STATE_DEMANDS_ATTENTION` (boolean, all apps)
2. `SmartLauncherItem` — Unity Launcher API `com.canonical.Unity.LauncherEntry` (count, few apps)

Most Electron apps (Slack, Discord, etc.) do NOT send Unity Launcher API signals on Linux.
This document covers additional sources.

---

## Approach 1: WatchedNotificationsModel (Recommended)

### Architecture

Plasma's notification system uses a layered architecture:

```
App → Notify() → org.freedesktop.Notifications (plasmashell owns it)
                        ↓ broadcasts to watchers
              RegisterWatcher() → WatchedNotificationsModel (passive)
                                        ↓
                          Notifications (QSortFilterProxy)
                                        ↓
                              QML views (badge counts)
```

`WatchedNotificationsModel` is a **passive watcher** — it does NOT need to own the D-Bus service.
It calls `org.kde.NotificationManager.RegisterWatcher()` on startup and receives all notifications
as broadcast calls from the server.

### WatchedNotificationsModel: C++ vs QML Availability

**IMPORTANT**: `WatchedNotificationsModel` has **NO installed C++ public header** in
`/usr/include/notificationmanager/`. It is exposed only via the QML module
`org.kde.notificationmanager`. The C++ class `NotificationManager::AbstractNotificationsModel`
(its base) also has no public header.

- **QML use**: Fully supported — use `NotificationManager.WatchedNotificationsModel` directly.
- **C++ use**: See critical bug note below — `Notifications` proxy does NOT use `WatchedNotificationsModel`.

### CRITICAL BUG: Notifications Proxy Does NOT Use WatchedNotificationsModel (Verified via Binary Analysis)

**SYMPTOM**: `NotificationManager::Notifications` created from C++ stays at 0 rows even after
`notify-send` fires. The `rowsInserted` signal is never emitted.

**ROOT CAUSE** (confirmed via disassembly of `/usr/lib/libnotificationmanager.so`):

```
Notifications::componentComplete()
  → Private::update()
    → NotificationsModel::createNotificationsModel()   // singleton factory
      → new NotificationsModel()
        → connect(Server::self().notificationAdded, this, onNotificationAdded)
        → connect(Server::self().notificationReplaced, ...)
        → connect(Server::self().notificationRemoved, ...)
```

`NotificationsModel` connects to **`Server::self()`** — the in-process notification server
singleton. In an external app like Krema, `Server::self()` exists but `Server::init()` is never
called, so `Server::self()` is never the real D-Bus server. All `notify-send` / application
notifications go to plasmashell's `Server`, which is in a **different process**.

The `WatchedNotificationsModel` exists as a separate class (symbol:
`NotificationManager::WatchedNotificationsModel::WatchedNotificationsModel()`) but is **not** used
by `Notifications::componentComplete()` in the current implementation.

**WHAT DOES NOT WORK:**
```cpp
// This stays at 0 rows forever — connects to in-process Server::self(), not plasmashell's server
auto *model = new NotificationManager::Notifications(this);
auto *ps = static_cast<QQmlParserStatus*>(model);
ps->classBegin();
model->setShowNotifications(true);
ps->componentComplete();
// model->rowCount() == 0 always, rowsInserted never fires
```

**CORRECT APPROACH: Custom D-Bus Watcher (see below)**
**IMPORTANT: Read `notification-watcher-protocol.md` for the correct path/interface — the code below has been verified.**

### WatchedNotificationsModel QML API (from qmltypes)

```
namespace: NotificationManager
QML type: WatchedNotificationsModel
Prototype: NotificationManager::AbstractNotificationsModel → QAbstractListModel

Properties:
  bool valid [readonly, notify: validChanged]
    - true when connected to the D-Bus notification server
    - false if plasmashell is not running or registration failed

Methods:
  void expire(uint notificationId)
  void close(uint notificationId)
  void invokeDefaultAction(uint notificationId)

Signals:
  validChanged(bool valid)
```

### QML Usage (Simplest)

```qml
import org.kde.notificationmanager as NotificationManager

// Passive watcher model — does not conflict with plasmashell's notification server
NotificationManager.WatchedNotificationsModel {
    id: watchedModel
    // valid: true when successfully connected to the notification server
}

// Count unread notifications per app
function unreadCountForApp(desktopEntry) {
    let count = 0;
    for (let i = 0; i < watchedModel.rowCount(); i++) {
        const idx = watchedModel.index(i, 0);
        const entry = watchedModel.data(idx, NotificationManager.Notifications.DesktopEntryRole);
        const expired = watchedModel.data(idx, NotificationManager.Notifications.ExpiredRole);
        const read = watchedModel.data(idx, NotificationManager.Notifications.ReadRole);
        if (entry === desktopEntry && !expired && !read) {
            count++;
        }
    }
    return count;
}
```

### Using the Notifications Proxy Model (Preferred)

```qml
import org.kde.notificationmanager as NotificationManager

// Proxy model with filtering/sorting — sits on top of WatchedNotificationsModel
NotificationManager.Notifications {
    id: notificationsModel
    showNotifications: true
    showJobs: false
    showExpired: false

    // Filter to a specific app
    // Note: no built-in per-app filter property — use QSortFilterProxyModel in C++
    // or iterate in QML
}
```

### C++ Usage (BROKEN — DO NOT USE)

```cpp
// WARNING: This does NOT work for receiving external notifications.
// NotificationsModel connects to in-process Server::self(), not plasmashell.
// Use the custom D-Bus watcher approach instead (see below).

#include <notificationmanager/notifications.h>

auto *model = new NotificationManager::Notifications(this);
auto *ps = static_cast<QQmlParserStatus*>(model);
ps->classBegin();
model->setShowNotifications(true);
model->setShowExpired(true);
ps->componentComplete();
// model->rowCount() will always be 0 — never receives external notify-send
```

### Correct C++ Implementation: Custom D-Bus Watcher

The `WatchedNotificationsModel` works by:
1. Registering a D-Bus object at `/org/freedesktop/Notifications` on this app's session bus connection
2. Calling `org.kde.NotificationManager.RegisterWatcher()` on plasmashell's service
3. Plasmashell then calls `Notify()` on all registered watchers whenever a notification arrives
4. Calling `org.kde.NotificationManager.UnRegisterWatcher()` on shutdown

This can be implemented directly in C++ without any private headers:

```cpp
// notificationwatcher.h
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>
#include <QVariantMap>

class NotificationWatcher : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    explicit NotificationWatcher(QObject *parent = nullptr);
    ~NotificationWatcher() override;

    // D-Bus slot: called by plasmashell for each new notification
    // Signature MUST match exactly — plasmashell is the caller
    Q_SCRIPTABLE uint Notify(const QString &appName,
                             uint replacesId,
                             const QString &appIcon,
                             const QString &summary,
                             const QString &body,
                             const QStringList &actions,
                             const QVariantMap &hints,
                             int expireTimeout);

    // D-Bus slot: called by plasmashell when a notification is closed
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);

Q_SIGNALS:
    void notificationReceived(uint id, const QString &appName, const QString &desktopEntry,
                              const QString &summary, const QVariantMap &hints);
    void notificationClosed(uint id, uint reason);

private:
    bool m_registered = false;
};

// notificationwatcher.cpp
NotificationWatcher::NotificationWatcher(QObject *parent)
    : QObject(parent)
{
    // Step 1: Register D-Bus object at /NotificationWatcher with org.kde.NotificationWatcher.
    // CRITICAL: The server calls /NotificationWatcher with org.kde.NotificationWatcher interface,
    // NOT /org/freedesktop/Notifications with org.freedesktop.Notifications (verified by D-Bus trace).
    const bool ok = QDBusConnection::sessionBus().registerObject(
        QStringLiteral("/NotificationWatcher"),
        QStringLiteral("org.kde.NotificationWatcher"),
        this,
        QDBusConnection::ExportScriptableSlots
    );
    if (!ok) {
        qWarning() << "NotificationWatcher: Failed to register D-Bus object";
        return;
    }

    // Step 2: Call RegisterWatcher() to subscribe to notifications from plasmashell
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.kde.NotificationManager"),
        QStringLiteral("RegisterWatcher")
    );
    QDBusConnection::sessionBus().asyncCall(msg);
    m_registered = true;
}

NotificationWatcher::~NotificationWatcher()
{
    if (m_registered) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.Notifications"),
            QStringLiteral("/org/freedesktop/Notifications"),
            QStringLiteral("org.kde.NotificationManager"),
            QStringLiteral("UnRegisterWatcher")
        );
        QDBusConnection::sessionBus().asyncCall(msg);
        QDBusConnection::sessionBus().unregisterObject(
            QStringLiteral("/org/freedesktop/Notifications")
        );
    }
}

uint NotificationWatcher::Notify(const QString &appName, uint replacesId,
                                 const QString &appIcon, const QString &summary,
                                 const QString &body, const QStringList &actions,
                                 const QVariantMap &hints, int expireTimeout)
{
    // Extract desktop-entry hint — this is the key for matching to dock items
    const QString desktopEntry = hints.value(QStringLiteral("desktop-entry")).toString();
    Q_EMIT notificationReceived(replacesId > 0 ? replacesId : 0, appName, desktopEntry, summary, hints);
    return 0; // Watchers return 0 (server assigns the real ID)
}

void NotificationWatcher::NotificationClosed(uint id, uint reason)
{
    Q_EMIT notificationClosed(id, reason);
}
```

**Key details from binary analysis + live D-Bus trace (verified 2026-02-28):**

- Plasmashell calls `Notify(uint id, QString appName, uint replacesId, ...)` on the watcher — note
  the `id` is the FIRST argument (different from the D-Bus spec where `replaces_id` is second and
  apps pass 0 for new notifications). This is the server-assigned notification ID.
- `NotificationClosed(uint id, uint reason)` is sent as a **broadcast signal** on
  `org.freedesktop.Notifications`, NOT as a directed call to watchers. Must subscribe with
  `QDBusConnection::connect()`.
- `UnRegisterWatcher()` must be called on shutdown to avoid stale watcher entries.
- Only NEW notifications (after registration) are received — no historical notifications.
- The watcher does NOT need to own a well-known service name — unique bus name (`:1.xxx`) suffices.

See `docs/kde/notification-watcher-protocol.md` for complete protocol documentation and test evidence.

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

**Badge count derivation**: `unread = rows where desktopEntry matches AND !expired AND !read`

This is NOT the same as the app-reported badge count from Unity API, but it's the best
approximation available for all apps.

### Feasibility Assessment

| Factor | Assessment |
|--------|-----------|
| Implementation effort | Medium — QML-first, simple |
| Gives badge COUNT | Yes (unread notification count, not app-reported) |
| Works for all apps | Only apps that use `org.freedesktop.Notifications` AND send `desktop-entry` hint |
| Privacy concerns | Low — reading already-delivered notifications |
| Permission requirements | None — passive watcher |
| Requires plasmashell | Yes — `valid` property is false without a notification server |

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

**The correct approach is `WatchedNotificationsModel` (Approach 1).**

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

The `SmartLauncherItem` does check `NotificationManager::Settings::badgesInTaskManager()` and
`badgeBlacklistedApplications()` at runtime — confirming settings integration exists.

---

## Recommended Architecture for Krema

### Badge Source Priority

```
Priority  Source                          Count    Coverage
--------  -----                           -----    --------
1 (best)  SmartLauncherItem.count         Exact    KDE apps, Slack (Unity API)
2         WatchedNotificationsModel        Unread#  All apps using libnotify
3         SNI NeedsAttention              Boolean  Discord, Telegram, tray apps
4         IsDemandingAttention (existing) Boolean  X11 urgency hint apps
```

### Combined Badge Logic (QML sketch)

```qml
// In DockItem.qml
readonly property int badgeCount: {
    // Priority 1: Unity API exact count
    if (smartLauncherItem && smartLauncherItem.countVisible) {
        return smartLauncherItem.count;
    }
    // Priority 2: Unread notification count
    if (notifBadgeCount > 0) {
        return notifBadgeCount;
    }
    // Priority 3+4: Boolean dot — handled by attentionState
    return 0;
}

readonly property bool attentionState: {
    return model.IsDemandingAttention ||
           (smartLauncherItem && smartLauncherItem.urgent) ||
           sniNeedsAttention ||
           notifBadgeCount > 0;
}
```

### Per-App Notification Count Model (C++)

For efficiency, create a C++ model that:
1. Uses `NotificationManager::Notifications` as source
2. Groups by `DesktopEntryRole`
3. Counts non-expired, non-read rows per app
4. Exposes `Q_INVOKABLE int unreadCountForApp(const QString &desktopEntry)`
5. Emits `countChanged(desktopEntry, newCount)` signal

This avoids QML-side iteration over all notifications per dock item.

---

## CMake Integration

```cmake
# Find LibNotificationManager (from plasma-workspace)
find_package(LibNotificationManager REQUIRED)

# Link target
target_link_libraries(krema PRIVATE PW::LibNotificationManager)

# Headers are at: /usr/include/notificationmanager/
```

**CMake target**: `PW::LibNotificationManager`
**CMake find_package name**: `LibNotificationManager`
**Config file**: `/usr/lib/cmake/LibNotificationManager/LibNotificationManagerConfig.cmake`
**Shared library**: `/usr/lib/libnotificationmanager.so.6.5.5` (soname: `libnotificationmanager.so.1`)
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

- **WatchedNotificationsModel**: Reads notification content (summary, body, app name).
  This is the same data the notification bell applet reads. No special permissions needed.
  Consider NOT storing notification bodies — only counts per desktop entry.

- **SNI monitoring**: Only reads app name, status, and icon. No sensitive data.

- **Respect user settings**: Always check `NotificationManager::Settings::badgesInTaskManager()`
  and `badgeBlacklistedApplications()` before showing any notification-derived badge.

---

## Known Limitations

0. **`Notifications` proxy is broken for external apps** — `Notifications::componentComplete()`
   creates `NotificationsModel` which connects to in-process `Server::self()`, not plasmashell's
   server. Use the custom D-Bus watcher (see above) or `WatchedNotificationsModel` via QML instead.
   Verified via binary analysis of `/usr/lib/libnotificationmanager.so` (Plasma 6.5.5).

1. **`desktop-entry` hint is optional** — apps that don't send it cannot be correlated.
   `app_name` (the first argument to `Notify()`) is always present but may differ from desktop file stem.
   The `WatchedNotificationsModel` uses `DesktopEntryRole` from the `desktop-entry` hint.

2. **No server ownership** — `WatchedNotificationsModel.valid` can be false if no notification
   server is running (edge case — plasmashell always runs on KDE Plasma).

3. **"Unread" definition** — `NotificationManager::Notifications` marks a notification as read
   when the user interacts with it or when `setLastRead()` is called. The dock needs to
   cooperate with the notification bell applet for consistent read state.

4. **Electron apps** (Discord, VS Code, etc.) — Notification behavior varies by Electron version.
   Older versions may send `app_name` without `desktop-entry`. In that case, fallback to
   matching `app_name` against known desktop entries via `KService`.

5. **SNI ID matching** — Not standardized. Heuristic matching (case-insensitive substring)
   may produce false positives for similarly-named apps.

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
