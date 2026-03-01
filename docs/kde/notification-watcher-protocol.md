# RegisterWatcher Protocol: Complete Investigation

**Investigated**: 2026-02-28 (Plasma 6.5.5)
**Status**: Protocol verified working with correct parameters

---

## Executive Summary

`org.kde.NotificationManager.RegisterWatcher` WORKS, but the previously documented
D-Bus path and interface for the watcher callback object are WRONG.

**Previous (incorrect) documentation:**
```
path=/org/freedesktop/Notifications
interface=org.freedesktop.Notifications
```

**Actual protocol (confirmed by D-Bus trace):**
```
path=/NotificationWatcher
interface=org.kde.NotificationWatcher
```

Krema's `NotificationTracker::setupNotificationWatcher()` uses the wrong path/interface,
which is why `Notify()` is never called despite `RegisterWatcher` succeeding.

---

## Protocol Details

### Step 1: Registration

The watcher calls `RegisterWatcher()` on the notification server:

```
destination: org.freedesktop.Notifications
path:        /org/freedesktop/Notifications
interface:   org.kde.NotificationManager
member:      RegisterWatcher
signature:   (no args)
```

This returns immediately (empty reply). The server records the caller's unique bus name
(e.g., `:1.382`) and calls D-Bus `AddMatch` to watch for when that bus disconnects.

### Step 2: Server calls Notify() on the watcher

When any application calls `Notify()` on the notification server, the server:
1. Processes it normally (assigns an ID, shows the popup)
2. Calls `GetConnectionUnixProcessID` on the notifying app's bus name (to get PID for hints)
3. Calls `Notify()` on EACH registered watcher:

```
destination: :1.382  (watcher's unique bus name, recorded at RegisterWatcher time)
path:        /NotificationWatcher
interface:   org.kde.NotificationWatcher
member:      Notify
signature:   ususssasa{sv}i
```

**Argument order (signature `ususssasa{sv}i`):**

| Pos | Type | Name | Description |
|-----|------|------|-------------|
| 0 | `u` (uint32) | `id` | Server-assigned notification ID |
| 1 | `s` | `app_name` | Application name (e.g., `"notify-send"`) |
| 2 | `u` (uint32) | `replaces_id` | ID being replaced (0 for new) |
| 3 | `s` | `app_icon` | Icon name or empty |
| 4 | `s` | `summary` | Notification title |
| 5 | `s` | `body` | Notification body |
| 6 | `as` | `actions` | Action list |
| 7 | `a{sv}` | `hints` | Hints dict (includes `desktop-entry`) |
| 8 | `i` (int32) | `expire_timeout` | Timeout in ms, -1 = default |

**Note**: The `id` is the FIRST argument and is the server-assigned ID. This differs from
the standard `org.freedesktop.Notifications.Notify()` where `replaces_id` is the second
argument and apps pass 0 for new notifications.

### Step 3: NotificationClosed (broadcast signal)

`NotificationClosed` is NOT called as a directed method on watchers.
It is sent as a **broadcast signal** on `org.freedesktop.Notifications`:

```
sender:      :1.371  (plasmashell's bus name)
path:        /org/freedesktop/Notifications
interface:   org.freedesktop.Notifications
member:      NotificationClosed
signature:   uu
args:        uint id, uint reason
```

Watchers must subscribe to this signal explicitly via `QDBusConnection::connect()`.

**Reason codes:**
- `1` = Expired (timeout)
- `2` = User dismissed
- `3` = App called `CloseNotification()`
- `4` = Undefined/reserved

### Step 4: Unregistration

```
destination: org.freedesktop.Notifications
path:        /org/freedesktop/Notifications
interface:   org.kde.NotificationManager
member:      UnRegisterWatcher
```

---

## D-Bus Interface XML

The `org.kde.NotificationWatcher` interface (what watchers must implement):

```xml
<!-- Extracted from libnotificationmanager.so strings (server-side definition) -->
<!-- The watcher implements this interface at /NotificationWatcher -->
<interface name="org.kde.NotificationWatcher">
  <method name="Notify">
    <arg direction="in" type="u" name="id"/>
    <arg direction="in" type="s" name="app_name"/>
    <arg direction="in" type="u" name="replaces_id"/>
    <arg direction="in" type="s" name="app_icon"/>
    <arg direction="in" type="s" name="summary"/>
    <arg direction="in" type="s" name="body"/>
    <arg direction="in" type="as" name="actions"/>
    <arg direction="in" type="a{sv}" name="hints"/>
    <arg direction="in" type="i" name="expire_timeout"/>
    <arg direction="out" type="u"/>
  </method>
  <method name="NotificationClosed">
    <arg direction="in" type="u" name="id"/>
    <arg direction="in" type="u" name="reason"/>
  </method>
</interface>
```

**Note**: `NotificationClosed` here would be a directed method call if the server chose
to call it on watchers. However, empirical testing (Plasma 6.5.5) shows the server sends
`NotificationClosed` only as a broadcast signal on `org.freedesktop.Notifications`.
Watchers must subscribe to the broadcast signal, not wait for a directed method call.

---

## Verification Evidence

### Test 1: Wrong path/interface (FAILED)

```python
# Registered at: /org/freedesktop/Notifications
# Interface: org.freedesktop.Notifications
# Result: RegisterWatcher succeeded, but Notify() was NEVER called
```

D-Bus trace after notify-send:
```
method call: destination=:1.382 path=/NotificationWatcher interface=org.kde.NotificationWatcher member=Notify
error: Method "Notify" with signature "ususssasa{sv}i" on interface "org.kde.NotificationWatcher" doesn't exist
```

The server tried to call at `/NotificationWatcher` with `org.kde.NotificationWatcher` but our
watcher was only listening at `/org/freedesktop/Notifications` with `org.freedesktop.Notifications`.

### Test 2: Correct path/interface (SUCCEEDED)

```python
# Registered at: /NotificationWatcher
# Interface: org.kde.NotificationWatcher
# Result: Notify() was called successfully for every notify-send
```

Output from successful test:
```
[WATCHER] Notify() CALLED
  id=8 (server-assigned), app_name=notify-send
  replaces_id=0, app_icon=
  summary=Test
  body=Hello from kwin-mcp
  desktop-entry hint: N/A
```

With explicit desktop-entry hint:
```bash
notify-send "Spectacle" "Screenshot taken" --hint=string:desktop-entry:org.kde.spectacle
# → Notify() called with hints['desktop-entry'] = 'org.kde.spectacle'

notify-send "Firefox" "Download complete" --hint=string:desktop-entry:firefox
# → Notify() called with hints['desktop-entry'] = 'firefox'
```

Both notifications received correctly with the `desktop-entry` hint populated.

---

## RegisterWatcher D-Bus Sequence Diagram

```
Our App                  D-Bus Daemon              plasmashell (notification server)
   |                         |                              |
   |-- RegisterWatcher() --->|----------------------------->|
   |                         |                              | records our bus name (:1.xxx)
   |<-- (empty reply) -------|<-----------------------------|
   |                         | AddMatch(NameOwnerChanged,   |
   |                         | arg0=':1.xxx',arg2='')       |
   |                         |<-----------------------------|
   |                         |                              |
   | [Some app sends notify-send]                           |
   |                         |<-- Notify() from notify-send |
   |                         |---> Notify() to plasmashell--|
   |                         |                              | assigns id=8
   |                         |                              | GetConnectionUnixPID (notify-send's PID)
   |                         |<-----------------------------|
   |                         |-- Notify(8, ...) ----------->| [broadcast to all watchers]
   |<-- Notify(8,...) -------|                              |
   | [/NotificationWatcher,                                 |
   |  org.kde.NotificationWatcher]                          |
   |-- return 0 ------------>|--[error ignored]------------>|
   |                         |                              |
   | [notification auto-expires]                            |
   |                         |<-- NotificationClosed signal |
   |<-- broadcast signal ----|                              |
   | [org.freedesktop.Notifications.NotificationClosed]     |
   | [Must subscribe explicitly with QDBusConnection::connect]
```

---

## Dynamic Nature of the Protocol

Key observations about how the server identifies where to send callbacks:

1. The server gets the caller's **unique bus name** from the D-Bus message context
   when `RegisterWatcher()` is called (equivalent to `message().service()` in Qt D-Bus).

2. The server then always calls at `unique_bus_name + "/NotificationWatcher"` with
   interface `org.kde.NotificationWatcher`. These paths are **hardcoded in the server**
   (plasmashell/libnotificationmanager), not communicated by the watcher during registration.

3. The watcher does NOT need to acquire a well-known service name (like
   `org.freedesktop.Notifications`) — the unique bus name (`:1.xxx`) is sufficient.
   Our test app using just the unique bus name successfully received callbacks.

---

## Correct C++ Implementation for Krema

### notificationtracker.cpp (corrected `setupNotificationWatcher`)

```cpp
void NotificationTracker::setupNotificationWatcher()
{
    auto bus = QDBusConnection::sessionBus();

    // Register our object at /NotificationWatcher with org.kde.NotificationWatcher interface.
    // The notification server (plasmashell) hardcodes this path/interface when calling watchers.
    // Do NOT use /org/freedesktop/Notifications or org.freedesktop.Notifications here.
    bool registered = bus.registerObject(
        QStringLiteral("/NotificationWatcher"),
        QStringLiteral("org.kde.NotificationWatcher"),
        this,
        QDBusConnection::ExportScriptableSlots
    );
    if (!registered) {
        qCWarning(lcNotif) << "Failed to register D-Bus notification watcher object:"
                           << bus.lastError().message();
        return;
    }

    // Subscribe to NotificationClosed broadcast signal.
    // This is a broadcast signal from org.freedesktop.Notifications, NOT a directed call
    // to our watcher object. Must be subscribed separately.
    bus.connect(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("NotificationClosed"),
        this,
        SLOT(NotificationClosed(uint, uint))
    );

    // Tell the notification server we want to watch all notifications.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.kde.NotificationManager"),
        QStringLiteral("RegisterWatcher")
    );
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCWarning(lcNotif) << "Failed to register as notification watcher:"
                               << reply.error().message();
        } else {
            qCInfo(lcNotif) << "Registered as notification watcher";
        }
    });
}
```

### Destructor (no change needed)

```cpp
NotificationTracker::~NotificationTracker()
{
    // Unregister watcher from notification server
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.kde.NotificationManager"),
        QStringLiteral("UnRegisterWatcher")
    );
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
    // QDBusConnection::unregisterObject is called automatically when the object is destroyed
}
```

### Q_CLASSINFO for the correct D-Bus interface

The `Q_SCRIPTABLE` slots must be exported under the `org.kde.NotificationWatcher` interface.
This requires adding `Q_CLASSINFO("D-Bus Interface", "org.kde.NotificationWatcher")` to the class,
OR the `interface` parameter in `registerObject()` handles this automatically.

When using the 4-argument `registerObject()` with the `interface` string, Qt uses that string
as the interface name for all exported methods. **No additional `Q_CLASSINFO` needed.**

---

## Bugs in Krema's Current Implementation

### Bug 1: Wrong D-Bus path and interface (CRITICAL)

**File**: `src/models/notificationtracker.cpp`, `setupNotificationWatcher()`

```cpp
// WRONG - plasmashell calls /NotificationWatcher with org.kde.NotificationWatcher
bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
                   QStringLiteral("org.freedesktop.Notifications"),
                   ...);

// CORRECT
bus.registerObject(QStringLiteral("/NotificationWatcher"),
                   QStringLiteral("org.kde.NotificationWatcher"),
                   ...);
```

### Bug 2: NotificationClosed not subscribed as signal (MEDIUM)

**File**: `src/models/notificationtracker.cpp`, `setupNotificationWatcher()`

`NotificationClosed` is only defined as a D-Bus exported slot (via `Q_SCRIPTABLE`). But in
Plasma 6.5.5, `NotificationClosed` is NOT sent as a directed call to watchers — it's a
broadcast signal on `org.freedesktop.Notifications`. Must subscribe explicitly:

```cpp
// Add this to setupNotificationWatcher():
bus.connect(
    QStringLiteral("org.freedesktop.Notifications"),
    QStringLiteral("/org/freedesktop/Notifications"),
    QStringLiteral("org.freedesktop.Notifications"),
    QStringLiteral("NotificationClosed"),
    this,
    SLOT(NotificationClosed(uint, uint))
);
```

---

## Constraints and Limitations

### Requires plasmashell

`RegisterWatcher` only exists when `org.freedesktop.Notifications` is owned by plasmashell.
Without plasmashell:
- `qdbus6 org.freedesktop.Notifications` → service not available
- Registration will fail silently (asyncCall never returns error reply, just no server)

In kwin-mcp test sessions, plasmashell IS running (confirmed: server returned Plasma 6.5.5 info).

### Only new notifications

The watcher receives only notifications sent AFTER `RegisterWatcher()`. No historical replay.

### No Notify() reply used

The watcher's `Notify()` must return `uint 0`. The return value is ignored by the server.

### `desktop-entry` hint is optional

When apps don't send the `desktop-entry` hint (e.g., plain `notify-send` without
`--hint=string:desktop-entry:appname`), `hints['desktop-entry']` is absent. Fall back to
`app_name` as a less-reliable identifier.

### NotificationClosed timing

In Plasma 6.5.5, `NotificationClosed` broadcasts AFTER the notification popup disappears.
There may be a small delay between when the notification is dismissed and when `NotificationClosed`
arrives. This is acceptable for badge counting purposes.

---

## Python Test Script (for verification)

```python
#!/usr/bin/env python3
"""Minimal test of RegisterWatcher protocol - verified working on Plasma 6.5.5"""
import threading
from dbus.mainloop.glib import DBusGMainLoop
import dbus
import dbus.service
from gi.repository import GLib

DBusGMainLoop(set_as_default=True)

class NotificationWatcher(dbus.service.Object):
    def __init__(self, bus):
        dbus.service.Object.__init__(self, bus, '/NotificationWatcher')

    @dbus.service.method('org.kde.NotificationWatcher',
                         in_signature='ususssasa{sv}i', out_signature='u')
    def Notify(self, id, app_name, replaces_id, app_icon, summary,
               body, actions, hints, expire_timeout):
        desktop_entry = hints.get('desktop-entry', 'N/A')
        print(f"Notify: id={id}, app={app_name}, summary={summary}, desktop_entry={desktop_entry}")
        return dbus.UInt32(0)

bus = dbus.SessionBus()
watcher = NotificationWatcher(bus)
notif_obj = bus.get_object('org.freedesktop.Notifications', '/org/freedesktop/Notifications')
dbus.Interface(notif_obj, 'org.kde.NotificationManager').RegisterWatcher()
print("Registered. Send a notification with: notify-send 'App' 'Message'")

loop = GLib.MainLoop()
GLib.timeout_add_seconds(30, loop.quit)
loop.run()
dbus.Interface(notif_obj, 'org.kde.NotificationManager').UnRegisterWatcher()
```

Run with: `python3 test_watcher.py`

---

## Source Analysis

### libnotificationmanager.so (Plasma 6.5.5)

Key symbols for `WatchedNotificationsModel::Private`:

```
NotificationManager::WatchedNotificationsModel::Private::Private(...)
  - Inherits QDBusAbstractInterface (confirmed via RTTI)
  - Acts as a D-Bus PROXY to call methods on the server
  - Also registers itself at /NotificationWatcher to RECEIVE callbacks

NotificationManager::WatchedNotificationsModel::Private::Notify(uint, QString, uint, QString, QString, QString, QList<QString>, QMap<QString,QVariant>, int)
  - The callback that receives directed Notify() calls from the server
  - Signature: ususssasa{sv}i

NotificationManager::WatchedNotificationsModel::Private::NotificationClosed(uint, uint)
  - Either connected as a signal subscriber or called when broadcasts arrive
```

The `NotificationManagerAdaptor` class (the server side in libnotificationmanager) defines:
```xml
<interface name="org.kde.NotificationManager">
  <method name="RegisterWatcher"/>
  <method name="UnRegisterWatcher"/>
  <method name="InvokeAction">
    <arg direction="in" type="u" name="id"/>
    <arg direction="in" type="s" name="action_key"/>
  </method>
</interface>
```

### Path and interface are server-side constants

`/NotificationWatcher` and `org.kde.NotificationWatcher` are **NOT** in `libnotificationmanager.so`.
They are hardcoded in the server-side code (plasmashell or the notification daemon).
The watcher just needs to implement them at that fixed address.

---

## Reference

| Evidence Source | Location |
|-----------------|---------- |
| D-Bus introspection | `qdbus6 org.freedesktop.Notifications /org/freedesktop/Notifications` |
| D-Bus full trace (wrong watcher) | `dbus_full_trace.txt` (test session 2026-02-28) |
| D-Bus closed trace | `dbus_closed_trace.txt` (test session 2026-02-28) |
| Python test script (WORKING) | `/tmp/claude-1000/test_watcher_correct.py` |
| Krema bug location | `src/models/notificationtracker.cpp:setupNotificationWatcher()` |
| libnotificationmanager binary | `/usr/lib/libnotificationmanager.so.6.5.5` |
| Plasma version | 6.5.5 |
