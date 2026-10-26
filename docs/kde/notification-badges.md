# Notification Badges for All Apps

Reference for Krema's notification badges: showing badges/counts on dock icons, including apps
that do NOT support the Unity Launcher API.

**Headers**: `/usr/include/notificationmanager/`
**Package**: `plasma-workspace` (`PW::LibNotificationManager`)
**QML Module**: `org.kde.notificationmanager`

---

## Sources in Krema

Krema combines five badge sources:
1. `LauncherEntryTracker` (C++) — Unity Launcher API `com.canonical.Unity.LauncherEntry` (exact count, progress, urgent)
2. `NotificationTracker` (C++) — unread notifications via `RegisterWatcher` (canonical desktop identity)
3. `NotificationTracker` (C++) — SNI `NeedsAttention` (boolean)
4. `model.IsDemandingAttention` — EWMH `_NET_WM_STATE_DEMANDS_ATTENTION` (boolean, all apps)
5. Chat adapters — application-specific SNI icons and task-window titles (unread / mention state, and reported counts)

Most Electron apps do not send Unity Launcher API signals on Linux. Their chat
state can instead be recovered from the app's own tray or window metadata.

### Chat states (unreleased)

Supported chat adapters use `0 = None`, `1 = Unread`, and `2 = Mention`. Mentions
use the Plasma negative-text color, while ordinary unread messages use the link
color. Number mode shows a reported count when available; Dot mode omits numbers.
Off hides ordinary unread dots and numeric badges but keeps a mention dot when
there is no numeric count, alongside the existing live-attention marker.
Plasma's global and per-application badge preferences gate chat state without
discarding the stored state. DND suppresses attention animations, not badges.

Visible Unity counts have priority, followed by a chat-reported count, then
Freedesktop unread notification counts. The largest count across matching task
windows is used rather than summing windows for the same account.

| App | State source | Count source |
| --- | --- | --- |
| Slack Desktop | Lower-right pink mention / cyan unread tray badge | Positive notification count in tray tooltip |
| Discord | `• Discord` / `(N) Discord` window titles and red SNI badge | Positive title mention count; tray is state-only |
| Vesktop | `• Discord` / `(N) Discord` window titles and red SNI badge | Positive title mention count; tray is state-only |
| WebCord | `*WebCord` / `[N] WebCord` window titles and blue/red SNI badges | Positive title mention count; red tray mention is state-only |
| Mattermost Desktop | Lower-right quadrant of the SNI `IconPixmap`; tooltip title fallback | Mention / Unread state |
| Telegram Desktop | Theme icon names or bottom-right composited SNI `IconPixmap` badge | Positive window-title unread count as a fallback |

Electron `_status_icon_N` tray identity suffixes are stripped before
desktop-service lookup. Generic `chrome_status_icon_` items can also match an
exact canonical tooltip title or tray title. Tray pixmaps are decoded from
network-order ARGB only after dimensions and payload length are validated; images
are limited to 256 × 256. An app's `IconThemePath` PNG fallback is limited to
256 KiB. `NewIcon`, `NewIconThemePath`, and metadata property changes refresh
state through a coalesced in-flight/pending query, with no polling timer.

#### Telegram Desktop (unreleased)

The Telegram adapter recognizes `org.telegram.desktop`, `telegram`,
`telegram-desktop`, and `telegramdesktop`. It uses the same badge display modes,
Plasma badge preferences, count precedence, and asynchronous SNI refresh path
as the other chat adapters.

Theme `IconName` values are matched case-insensitively: a name containing
`attention` produces Mention; otherwise, a name containing `mute` produces
Unread. `attention` wins if both appear. Either match takes precedence over
pixel detection; names with neither token fall through to the tray image.

For composited `IconPixmap` badges, the adapter scans the bottom-right quadrant
(`x >= width * 5 / 10`, `y >= height * 5 / 10`) and ignores pixels with alpha below
200. It counts active red pixels (`r >= 200`, `g <= 110`, `b <= 120`) and muted gray
pixels (`abs(r - g) <= 10`, `abs(g - b) <= 10`, `70 <= r <= 190`); blue pixels are
ignored so Telegram's colored application logo does not count as a badge. At least
12 active pixels, outnumbering gray pixels, produce Mention; otherwise, at least 12
gray pixels produce Unread. An image without either badge leaves the tray state at
None. Tray pixels provide a state, not a numeric count; Krema renders that state with
its semantic mention or unread color rather than copying the tray badge color.

Window titles supply a positive unread count when no chat count is reported.
The adapter accepts `^Telegram\s*\(([1-9][0-9]*)\)\s*$` or a title suffix matching
`[–-]\s*\(([1-9][0-9]*)\)\s*$`, for example `Telegram (4)`, `Alice – (4)`, or
`Project - (4)`. Zero counts do not match. Grouped windows use the highest
matching count, not their sum. A positive title count changes None to Unread
without downgrading an existing Mention state. Number mode shows the count;
the resulting chat state determines whether the badge uses the mention or unread
color. Visible Unity counts retain priority.


## Input boundaries and service lifecycle

Krema consumes the KDE watcher, Unity LauncherEntry, SNI `NeedsAttention`,
task-manager attention, and `NotificationManager::Settings`. The notification
tracker re-registers with the Freedesktop notification service after a late
startup, restart, or direct owner-to-owner replacement. Both that service and
the StatusNotifierWatcher use `QDBusServiceWatcher::WatchForOwnerChange` and
one `serviceOwnerChanged(service, oldOwner, newOwner)` handler each. Registration
and unregistration signals alone miss a replacement with no unowned interval.

Every notification-server owner change invalidates pending registration
replies and the registered flag before registering with a nonempty new owner.
It leaves active notification IDs and unread counts intact. Every SNI-watcher
owner change invalidates pending snapshot replies and clears the old items
before querying a nonempty new owner's snapshot. Per-item generations reject
stale asynchronous `Id` and `Status` replies. Do not also connect registration
and unregistration handlers: one owner change must trigger one lifecycle reset.

StatusNotifierItem services follow the standard export-before-claim ordering:
the `/StatusNotifierItem` object is exported before the well-known service name
is claimed. A same-name SNI owner replacement erases the old `service|*`
entries, emits `revisionChanged()`, and queries the replacement owner under
that ordering contract.

If a non-standard peer claims its name before exporting the object, failed
initial `Get` calls set `initialQueryFailed`. Recovery is signal-driven only:
`StatusNotifierItemRegistered`, `NewStatus`, `NewTitle`, `NewAttentionIcon`,
`NewOverlayIcon`, `NewToolTip`, or `PropertiesChanged` re-requests the
properties. Krema does not use timer polling for this state. Duplicate
`StatusNotifierItemRegistered` signals preserve populated entries instead of
resetting them to blank `Passive` state.

### DND deadlines and QTimer bounds

`NotificationManager::Settings` supplies application inhibition and the
absolute `notificationsInhibitedUntil()` deadline. The existing single-shot
expiry timer clamps `now.msecsTo(until)` to `[1, INT_MAX]` milliseconds before
passing a chrono duration to `QTimer::start()`. Each timeout calls
`updateDndState()`, which reads the current settings and wall clock again.
A capped timeout therefore rearms while the deadline remains in the future;
it does not end DND early or substitute an elapsed-time counter for the deadline.
Settings changes stop and recompute the arm; application inhibition remains
independent of the Until deadline.

The tracker regression reproduced an invalid negative timer interval on
Qt 6.8.2 with an Until deadline 30 days ahead. The same pre-fix test on
Qt 6.10.3 stayed active at `INT_MAX`; newer Qt's saturation does not establish
compatibility with older supported versions. Tests also emit an early timeout
without advancing the clock, rewrite Until to a short real deadline, and
cancel Until. Emitting a timeout alone is not evidence that the deadline passed.

The test peers use `QDBusConnectionInterface::registerService()` with
`AllowReplacement` and, for the replacement owner, `ReplaceExistingService`.
They check the typed reply for `ServiceRegistered` and use separate bus
connections to prove a nonempty old owner changes directly to a different,
nonempty new owner. The enums and reply were verified in the installed
Qt 6.10.3 `QtDBus/qdbusconnectioninterface.h`; the owner watcher API is also
declared in Qt 6.8's `qdbusservicewatcher.h`.

### Transport boundaries

The desktop portal is a transport, not a second badge source. When the KDE
portal forwards a notification to Plasma's Freedesktop server, Krema receives
the resulting watcher `Notify` call. If the portal does not forward it, Krema
does not receive the notification directly.

`org.kde.kuiserver` and `JobViewV2` report file-operation progress. They are not
Krema notification inputs and do not create inbox rows or unread badges.


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
   Krema extracts the canonical desktop identity and does not retain the payload.
4. `NotificationClosed(uint id, uint reason)` arrives as a **broadcast signal** on
   `org.freedesktop.Notifications`, not as a directed call — subscribe with `QDBusConnection::connect()`.
5. The directed watcher `CloseNotification(uint id)` has no reason argument. Both directed and
   broadcast close callbacks remove the ID from the active count.
6. Call `UnRegisterWatcher()` on shutdown.

### Canonical desktop identity and lifecycle

Krema canonicalizes every desktop identity to lower case and strips one trailing `.desktop`
suffix. When possible, KService resolves the canonical identity to the full desktop entry.
Active ID maps, SNI correlation, and blacklist checks use that same canonical form.
The tracker uses exact canonical equality; it does not compare only the last dot-separated
segment or use substring matching, so unrelated reverse-domain IDs cannot be merged.

The `desktop-entry` hint is the primary identity. If it is missing or cannot be resolved by
KService, the normalized hint or normalized `app_name` is retained as an explicit fallback
identity so the notification is not lost. That fallback is not treated as proof that the app
matches a different desktop entry.

Each active server notification ID contributes one count under its canonical identity. A
replacement removes the replaced ID before storing the new ID, so it keeps one count. Any
directed or broadcast close removes the ID from the active count. Clearing an app removes
its IDs from both active maps and emits `revisionChanged()` when state changes.

---

Only notifications sent after registration are received. `RegisterWatcher` needs plasmashell
(owner of `org.freedesktop.Notifications`). Quick check: `qdbus6 <krema-bus-name> /NotificationWatcher`
must list `org.kde.NotificationWatcher.Notify`.

### Badge identity and settings

The `desktop-entry` hint from the `Notify()` D-Bus call is used as the badge identity.
KDE apps, GTK apps, and Electron apps may provide this hint; when it is absent,
Krema falls back to the normalized `app_name`.

`NotificationTracker` counts active notification IDs per canonical desktop identity from
`Notify()` until a directed or broadcast close. `clearUnreadNotifications()` removes those
IDs from the active count. This is distinct from the app-reported Unity badge count, but
provides a consistent dock badge for applications that use Freedesktop notifications.

The tracker honors `NotificationManager::Settings::badgesInTaskManager()` and
`badgeBlacklistedApplications()` before exposing a notification-derived badge.

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

- **Discord, Vesktop, and WebCord**: use the app-specific tray badge states documented below
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

The SNI `Id` field typically matches the canonical `.desktop` file stem:
- SNI `Id: "discord"` → desktop file `discord.desktop` → TaskManager `AppId: "discord"`
- SNI `Id: "spotify"` → `spotify.desktop`
- SNI `Id: "kime"` (input method) → no corresponding dock item

**Match algorithm**:
```cpp
// Compare canonical desktop identities (lowercase, one trailing .desktop removed).
// Do not use last-segment or substring matching.
bool matchSniToDockItem(const QString &sniId, const QString &appId) {
    return canonicalDesktopIdentity(sniId) == canonicalDesktopIdentity(appId);
}
```

### Discord, Vesktop, and WebCord support

Krema has explicit chat-state adapters for these three Electron applications. Window-title
matching and SNI matching use separate, exact alias sets; an unrelated Chrome or PWA app ID
does not receive these states.

| App | Window/task aliases | SNI aliases | Window-title states | Tray-icon states |
|-----|--------------------|-------------|---------------------|------------------|
| Discord | `discord`, `com.discordapp.discord` | `discord`, `com.discordapp.discord`, `com.discord.app` | `• Discord...` = unread; `(N) Discord...` = mention count `N` | Red top-right badge = unread |
| Vesktop | `vesktop`, `dev.vencord.vesktop` | `vesktop`, `dev.vencord.vesktop` | `• Discord...` = unread; `(N) Discord...` = mention count `N` | Red top-right badge = unread |
| WebCord | `webcord`, `io.github.spacingbat3.webcord` | `webcord`, `io.github.spacingbat3.webcord` | `*WebCord` = unread; `[N] WebCord` = mention count `N` | Blue top-right badge = unread; red top-right badge = mention |

`N` is a positive decimal integer. The effective title patterns are:

- Discord/Vesktop unread: `^\s*•\s+(?:(?:Discord|Vesktop)(?:\b|\s*\||.*-\s*(?:Discord|Vesktop)\s*$)|\S.*?\s+[|-]\s*(?:Discord|Vesktop)\s*$)`
- Discord/Vesktop mention: `^\s*(?:•\s*)?\(([1-9][0-9]*)\)\s+(?:(?:Discord|Vesktop)(?:\b|\s*\||.*-\s*(?:Discord|Vesktop)\s*$)|\S.*?\s+[|-]\s*(?:Discord|Vesktop)\s*$)`
- WebCord unread: `^\s*\*+\s*\[?WebCord\]?\b`
- WebCord mention: `^\s*\[([1-9][0-9]*)\]\s+WebCord\b`

Examples include `• Discord - #general`, `• #general | Server - Discord`, `(3) #general | Server - Discord`,
`*WebCord`, and `[3] WebCord`. Other title forms produce no title-derived chat state.

Tray detection reads the decoded SNI icon image. For Discord and Vesktop, an unread badge
requires at least 12 opaque-enough red pixels (alpha ≥ 180, red ≥ 200, green and blue ≤ 100)
in the top-right half, with no matching red pixels in the bottom-right half; the exclusion
prevents voice-state indicators from becoming unread badges. Because Discord and Vesktop use
the same tray badge for ordinary unreads and mentions, a positive numeric count (from Unity
`LauncherEntry`, window title `(N)`, or active `org.freedesktop.Notifications` mention/DM toasts
retained across `Expired` popup timeouts while the SNI unread badge remains active) renders in
the negative/red badge color, and returning the tray icon to rest clears retained toast counts.
For WebCord, an unread badge requires at least 25 pixels with alpha ≥ 180, red ≤ 170, green ≥ 150,
blue ≥ 210, and blue − red ≥ 50 in the top-right half; a mention state requires at least 25 pixels
with alpha ≥ 180, red ≥ 200, green ≤ 90, and blue ≤ 90 in that same region, or a `WebCord - N mention(s)`
SNI tooltip count.
For grouped tasks, Krema takes the maximum state and mention count across the group; it
does not add child counts. A positive Unity Launcher count remains the primary numeric
badge, followed by a positive chat mention count and then the Freedesktop unread count.
Global and per-application Plasma badge settings gate all of these chat states.

### Mattermost Desktop support

Krema uses these exact Mattermost Desktop aliases:

- `mattermost`
- `mattermost-desktop`
- `com.mattermost.desktop`

The adapter decodes the SNI `IconPixmap` tray icon and scans only its lower-right quadrant (`x >= width / 2`, `y >= height / 2`):

- **Mention** (`2`): at least 10 pixels with alpha `>= 180`, red `>= 210`, green `<= 90`, and blue `<= 80`
- **Unread** (`1`): at least 10 pixels with alpha `>= 180`, red `<= 120`, green `>= 130`, blue `>= 220`, and `blue - red >= 90`

If pixel detection finds no state, a case-insensitive tooltip-title match for `mentioned` produces Mention, and `unread channel` produces Unread. Mention takes precedence when both states are present. Global and per-application badge settings gate both states, and a positive Unity Launcher count remains authoritative.

Mattermost states use the shared chat semantics: Mention uses the red attention treatment, Unread uses the blue unread treatment, Dot mode keeps the state color without a number, and accessibility text names the active state.


### Feasibility Assessment

| Factor | Assessment |
|--------|-----------|
| Implementation effort | Medium (D-Bus boilerplate) |
| Gives badge COUNT | No — boolean only |
| Works for all apps | No — only apps with system tray icons |
| Privacy concerns | None |
| Permission requirements | None |

---

## SNI metadata in dock indicators

Krema retains the SNI `Title`, `AttentionIconName`, `OverlayIconName`, and
`ToolTip` fields alongside `Status`. These values are queried when an item is
registered and refreshed through the corresponding `NewTitle`,
`NewAttentionIcon`, `NewOverlayIcon`, and `NewToolTip` signals. Queries are
generation-checked so replies from an unregistered or replaced item cannot
restore stale metadata.

Dock lookups use the same canonical desktop identity as notification counts.
When multiple SNI items identify the same application, an item in
`NeedsAttention` status has precedence. The attention icon is shown only while
that status is active; otherwise an available overlay icon is shown. SNI
tooltip title/subtitle text is included in the dock item's accessible
description, but SNI metadata does not create a numeric notification count.

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
2         Chat adapters                              State/#  Slack, Discord, Vesktop, WebCord, Mattermost
3         NotificationTracker.unreadCount            Unread#  Apps using org.freedesktop.Notifications
4         NotificationTracker.sniNeedsAttention      Boolean  Apps with SNI attention state
5         IsDemandingAttention                       Boolean  X11 urgency hint apps
```

### Combined Badge Logic (`src/qml/DockItem.qml`)

```qml
readonly property int _badgeCount: {
    let _rev = NotificationTracker.revision  // reactive dependency
    if (_launcherCountVisible)               // 1st: Unity API exact count
        return _launcherCount
    if (_chatMentionCount > 0)               // 2nd: chat adapter count
        return _chatMentionCount
    if (_appId.length > 0) {                 // 3rd: unread notification count
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

The C++ trackers and chat adapters expose revision-backed lookups, so QML never iterates
notifications per dock item.

---

### Persistent attention-only dot

The dock keeps numeric badge precedence without turning a boolean attention state into a
fake notification count:

```qml
readonly property bool _showCountBadge:
    _badgeCount > 0 && DockSettings.badgeDisplayMode !== 2
readonly property bool _showAttentionDot:
    _isDemandingAttention && !_showCountBadge
readonly property bool _badgeIsDot:
    DockSettings.badgeDisplayMode === 1 || !_showCountBadge
```

When an app requests attention through SNI `NeedsAttention`, Unity `urgent`, or the task
manager attention role while no count badge is being shown, the existing `badge` overlay
shows a small persistent dot. This includes `Badge display: Off` when a positive count
exists: the count remains hidden, but live attention remains visible. The dot follows the
live attention state and remains visible after the transient attention animation ends.
Clearing the underlying attention state removes it immediately.

The precedence is:

1. A positive Unity or watcher count shows the configured numeric badge (or configured
   badge dot) unless Badge display is Off.
2. Live attention with no visible count badge shows the attention-only dot, including
   when Badge display is Off and a count exists.
3. No count badge and no attention shows no badge.

The marker is not a count, so the accessible description says `Attention requested` and
never reports a synthetic `1 notification` or a zero-count label.

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

- **Notification watcher**: Receives the watcher callback needed to maintain
  per-application unread state. Krema stores only the server ID and canonical
  desktop identity; it does not retain notification content or history.

- **SNI monitoring**: Only reads app name, status, and icon. No sensitive data.

- **Respect user settings**: Always check `NotificationManager::Settings::badgesInTaskManager()`
  and `badgeBlacklistedApplications()` before showing any notification-derived badge.

Krema is a dock, not a notification center. Notification watcher state is
limited to active IDs used for badge counts. Replacement and close callbacks
remove IDs from the active set, and no notification payload or lifecycle record
is exposed to QML.

---

## Known Limitations

0. **`Notifications` proxy is broken for external apps** — `Notifications::componentComplete()`
   creates `NotificationsModel` which connects to in-process `Server::self()`, not plasmashell's
   server. Use the D-Bus watcher (see above) or `WatchedNotificationsModel` via QML instead.

1. **`desktop-entry` hint is optional** — apps that don't send it cannot be correlated exactly.
   The hint is canonicalized first. If it is missing or cannot be resolved, the normalized hint
   or `app_name` remains as a fallback identity; Krema does not use fuzzy last-segment matching.

2. **Requires plasmashell** — without Plasma's notification server there is no `RegisterWatcher`.

3. **"Unread" is dock-managed** — Krema clears an app's count when it is focused, from the
   context menu, or when its SNI returns to `Active`; it does not share read state with the
   notification bell applet. A successful local clear emits `revisionChanged()` so QML sees the
   new count immediately.

4. **Electron apps** (Discord, VS Code, etc.) — Notification behavior varies by Electron version.
   Older versions may send `app_name` without `desktop-entry`.

5. **SNI IDs are not standardized** — exact canonical matching avoids false positives, but an
   unresolved or differently named SNI ID may not correlate with a dock item.

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
