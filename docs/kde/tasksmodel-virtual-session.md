# TasksModel in Virtual/Nested KWin Sessions

**Topic**: Why `TaskManager::TasksModel` does not detect windows in kwin-mcp virtual sessions
**Relevant headers**: `/usr/include/taskmanager/waylandtasksmodel.h`, `/usr/include/kwin/wayland/plasmawindowmanagement.h`
**Relevant library**: `/usr/lib/libtaskmanager.so.6.5.5`, `/usr/lib/libkwin.so.6.5.5`

---

## Root Cause Summary

`TasksModel` on Wayland uses `WaylandTasksModel` internally, which depends exclusively on the
`org_kde_plasma_window_management` KDE-proprietary Wayland protocol to enumerate windows. This
protocol is subject to a **per-client permission check** enforced by KWin: it reads the connecting
process's `.desktop` file and verifies that the interface is listed under `X-KDE-Wayland-Interfaces`.

In a kwin-mcp virtual session (`kwin_wayland --virtual`), this check is the primary barrier.
The environment variable `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1` bypasses the desktop-file check, and
kwin-mcp already sets it. The remaining issue is that the protocol also carries the note
**"Only one client can bind this interface at a time"** — if a second process in the same D-Bus
session has already claimed the global, the second binding is silently dropped.

---

## Detection Mechanism: How WaylandTasksModel Sees Windows

### Protocol chain

```
TasksModel (QSortFilterProxyModel)
    └── ConcatenateTasksProxyModel
            ├── WaylandTasksModel          ← window source on Wayland
            ├── LauncherTasksModel         ← .desktop launchers
            └── WaylandStartupTasksModel   ← startup notifications
```

`WaylandTasksModel` is a `QWaylandClientExtensionTemplate<PlasmaWindowManagement>`. On
construction it registers with Qt Wayland's extension mechanism. When the client's Wayland
connection receives the `wl_registry.global` event advertising
`org_kde_plasma_window_management`, Qt automatically calls `bind()`.

**It does NOT use D-Bus to get window lists.** The only requirement is `WAYLAND_DISPLAY`.

### Confirmed from binary inspection

```
strings /usr/lib/libtaskmanager.so.6.5.5
  → "org_kde_plasma_window"
  → "QtWayland::org_kde_plasma_window_management"
  → "The PlasmaWindowManagement protocol hasn't activated in time.
      The client possibly got denied by kwin? Check kwin output."
```

### wl_registry flow

```
client connects to WAYLAND_DISPLAY socket
    → wl_registry.global events arrive
    → QWaylandClientExtension finds "org_kde_plasma_window_management"
    → binds the global
    → KWin server-side: permission check fires
        • If KWIN_WAYLAND_NO_PERMISSION_CHECKS is NOT set:
            - KWin resolves client PID → /proc/<pid>/exe → KService lookup
            - Checks X-KDE-Wayland-Interfaces in .desktop file
            - If not listed → binding rejected, client never receives window events
        • If KWIN_WAYLAND_NO_PERMISSION_CHECKS=1:
            - Check skipped, binding allowed
    → KWin sends existing window list via org_kde_plasma_window events
    → WaylandTasksModel populates rows, IsWindow=true appears
```

**Confirmed from KWin binary:**
```
strings /usr/lib/libkwin.so.6.5.5
  → "KWIN_WAYLAND_NO_PERMISSION_CHECKS"
  → "Could not find the desktop file for"
  → "not in X-KDE-Wayland-Interfaces of"
  → "Only screen readers are allowed to use this interface"
```

---

## The Protocol Restriction

### From `/usr/share/plasma-wayland-protocols/plasma-window-management.xml`

```xml
<interface name="org_kde_plasma_window_management" version="20">
  <description summary="application windows management">
    ...
    Only one client can bind this interface at a time.

    Warning! The protocol described in this file is a desktop environment
    implementation detail. Regular clients must not use this protocol.
    Backward incompatible changes may be added without bumping the major
    version of the extension.
  </description>
```

**"Only one client at a time"** is a compositor-level enforcement. If Plasma Shell or another
process already holds the global on the same compositor socket, a second bind is refused. This
is irrelevant in a kwin-mcp virtual session (isolated socket, no Plasma Shell running), but it
explains why running two dock instances against the same compositor would break.

---

## Why kwin-mcp Sessions Fail

### Verified session setup (`session.py`)

kwin-mcp starts the virtual session with:

```python
# _build_env() — environment passed to dbus-run-session
env = {
    "KWIN_WAYLAND_NO_PERMISSION_CHECKS": "1",
    "KWIN_SCREENSHOT_NO_PERMISSION_CHECKS": "1",
    ...
}

# _build_wrapper_script() — the bash script inside dbus-run-session
env -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM \
    kwin_wayland --virtual --no-lockscreen --socket {socket_name}
```

`KWIN_WAYLAND_NO_PERMISSION_CHECKS=1` is present in the shell environment (inherited from
`_build_env()`) and is NOT removed by the `env -u` call. So KWin itself starts with the flag.

### The actual failure point

The issue is **not** the permission check (that should be bypassed). The failure is likely one
of two things:

**Hypothesis A — The global is never advertised in `--virtual` mode**

KWin's virtual backend (`KWin::VirtualBackend`) is a headless rendering backend. It is
possible that `PlasmaWindowManagementInterface` is only created in the full compositor
initialization path and is omitted in the virtual backend. When the global is absent from the
registry, `QWaylandClientExtension` never binds it, and `WaylandTasksModel` stays empty
indefinitely until its internal timeout fires:

> "The PlasmaWindowManagement protocol hasn't activated in time."

**Hypothesis B — D-Bus session isolation breaks the protocol lookup**

`WaylandTasksModel` is pure Wayland (no D-Bus). However, `TasksModel` internally also
consults `VirtualDesktopInfo` and `ActivityInfo`, both of which use D-Bus
(`org.kde.KWin.VirtualDesktopManager`, `org.kde.ActivityManager`). If these services are
absent in the isolated D-Bus session, `filterByVirtualDesktop` might silently drop all tasks.

Hypothesis A is more likely because the error message specifically mentions "hasn't activated
in time" — the global never appears on the registry.

### How to confirm

Run `wayland-info` inside the kwin-mcp session and check whether
`org_kde_plasma_window_management` appears in the globals list. kwin-mcp already provides a
`wayland_info(filter_protocol="plasma_window")` tool for this.

If the global is absent, the fix must be on the kwin-mcp side (make KWin advertise it). If it
is present but binding is denied, `KWIN_WAYLAND_NO_PERMISSION_CHECKS` is not reaching KWin.

---

## The Desktop File Check

### How KWin grants access

KWin resolves the connecting Wayland client's PID, finds its `.desktop` file via `KService`,
and checks `X-KDE-Wayland-Interfaces`. The correct declaration for a dock is:

```ini
[Desktop Entry]
X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,org_kde_plasma_activation_feedback,zkde_screencast_unstable_v1
```

**Krema already has this** in `src/org.krema.desktop.in` and the installed
`~/.local/share/applications/org.krema.desktop`. Confirmed from file inspection.

### Apps that use this protocol (reference)

```
org.kde.plasmashell.desktop:
    X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,org_kde_kwin_keystate,...

org.kde.plasmawindowed.desktop:
    X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,...

org.kde.spectacle.desktop:
    X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,zkde_screencast_unstable_v1
```

All confirmed at `/usr/share/applications/`.

---

## Workaround Strategies

### Strategy 1: Verify the global with `wayland_info` (diagnostic)

```python
# In kwin-mcp session
wayland_info(filter_protocol="plasma_window")
```

If `org_kde_plasma_window_management` appears → the global exists, check if binding is denied.
If it does not appear → KWin virtual mode does not create the global.

### Strategy 2: Force the global via KWin D-Bus scripting

KWin exposes a scripting interface. A KWin script can be injected to verify window state and
relay it via custom D-Bus signals. This would bypass `WaylandTasksModel` entirely for testing
purposes. Not viable for production code, only for test scaffolding.

### Strategy 3: Run kwin-mcp with `--nested` instead of `--virtual`

`kwin_wayland --nested` runs KWin as a client of the host compositor (nesting). In nested
mode, KWin creates a full compositor stack, which is more likely to initialize
`PlasmaWindowManagementInterface`. However, this requires a running compositor (display
server) on the host, which adds complexity and breaks session isolation.

### Strategy 4: Accept the limitation, test on real desktop

The window-detection functionality (`IsWindow`, `IsActive`, preview triggers) cannot be
reliably tested in virtual KWin sessions. These features must be verified on a real Plasma
desktop. All other dock features (rendering, zoom, tooltips, drag-and-drop, input regions)
work correctly in kwin-mcp virtual sessions.

This is the current approach in Krema (see `work-state.md`).

---

## Plasma Reference: How Task Manager Uses the Protocol

Plasma's own task manager (`org.kde.plasma.taskmanager`) uses `TasksModel` identically to
Krema. Source at `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/main.qml`:

```qml
readonly property TaskManager.TasksModel tasksModel: TaskManager.TasksModel {
    ...
}
```

Plasma Shell (`plasmashell`) holds the `org_kde_plasma_window_management` binding when running
on a real desktop. This is why task manager works on the real desktop: plasmashell's binding
brings up the protocol, and TasksModel reuses it through the Qt Wayland extension sharing
mechanism.

In a kwin-mcp virtual session, there is no plasmashell, so no pre-existing binding exists.
`WaylandTasksModel` must bind it itself, which requires both the global to be advertised and
the permission check to pass.

---

## Key Error Messages

### In libtaskmanager (client-side timeout)

```
"The PlasmaWindowManagement protocol hasn't activated in time.
 The client possibly got denied by kwin? Check kwin output."
```

Emitted after ~5 seconds if the `QWaylandClientExtension` never activates. Means either:
- global was never in the registry, or
- binding was rejected server-side (permission denied)

### In libkwin (server-side rejection)

```
"Could not find the desktop file for <pid>"
"not in X-KDE-Wayland-Interfaces of <desktop-file>"
```

These appear in KWin's log when a client without proper `.desktop` declaration tries to bind
a restricted protocol (and `KWIN_WAYLAND_NO_PERMISSION_CHECKS` is not set).

---

## API Reference

### `TaskManager::WaylandTasksModel`

**Header**: `/usr/include/taskmanager/waylandtasksmodel.h`
**Inheritance**: `AbstractWindowTasksModel` → `AbstractTasksModel` → `QAbstractItemModel`

Uses `QWaylandClientExtensionTemplate<PlasmaWindowManagement>` internally. Populated
exclusively from `org_kde_plasma_window_management` Wayland events. No D-Bus dependency for
the window list itself.

### `QWaylandClientExtension` activation

When `WAYLAND_DISPLAY` is set and the compositor advertises the global:
1. `wl_registry.global` event fires
2. `QWaylandClientExtension::initialize()` binds the global
3. `activeChanged(true)` signal emitted
4. `WaylandTasksModel` starts receiving window creation events

When the global is absent or binding is denied, `activeChanged` is never emitted.
The model stays empty.

---

## Constraints Summary

| Constraint | Source | Bypass |
|---|---|---|
| Desktop file check | KWin reads `/proc/<pid>/exe`, checks `X-KDE-Wayland-Interfaces` | `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1` (env var for KWin) |
| Single-client restriction | Protocol XML: "Only one client can bind at a time" | Not applicable in isolated sessions (no other client) |
| Global availability in `--virtual` | Unknown — possibly not created in virtual backend | Unresolved; use `wayland_info` to diagnose |
| D-Bus services absent | `VirtualDesktopInfo`, `ActivityInfo` may not connect | Ensure isolated D-Bus has no filter effects |

---

## Usage in Krema

Krema's `DockModel` creates a `TaskManager::TasksModel` directly. The model works correctly
on a real Plasma 6 desktop. In kwin-mcp virtual sessions, the model stays empty (no `IsWindow`
rows appear) because the underlying `WaylandTasksModel` never receives window events.

**Current status**: Window-dependent features (preview, running indicator dots, active state)
must be tested on a real desktop. All non-window features can be tested via kwin-mcp.

**Future investigation**: Run `wayland_info(filter_protocol="plasma_window")` in a kwin-mcp
session to determine whether the global is advertised. If it is, the problem is the permission
check and verifying `KWIN_WAYLAND_NO_PERMISSION_CHECKS` propagation. If it is not, the
virtual backend simply does not create the interface.
