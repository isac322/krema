# System Tray Consumer APIs (SNI Protocol)

> Research date: 2026-03-29
> Plasma version: 6.5.5, KF6, Qt 6.11

## Overview

Krema needs to *consume* system tray icons, not produce them. This document covers the
consumer side of the StatusNotifierItem (SNI) protocol — how a dock/panel discovers and
displays tray icons from other applications.

**Key finding**: No ready-made public KDE library exists for consuming SNI items.
Plasma's own system tray (`org.kde.plasma.private.systemtray`) is internal and requires
a `Plasmoid` context. The only viable path for Krema is a direct D-Bus implementation.

---

## Architecture Overview

```
[App running]
    |
    | KStatusNotifierItem (producer side, not our concern)
    v
org.kde.StatusNotifierWatcher  ← kded plugin, always running
    |
    | signals: StatusNotifierItemRegistered/Unregistered
    v
[Krema tray consumer]          ← what we must implement
    |
    | reads D-Bus properties from each SNI item
    | calls Activate/ContextMenu methods on click
    v
[Display in dock]
```

---

## Part 1: StatusNotifierWatcher (Discovery)

### D-Bus Interface

Service: `org.kde.StatusNotifierWatcher`
Path: `/StatusNotifierWatcher`
Interface: `org.kde.StatusNotifierWatcher`

XML definition: `/usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierWatcher.xml`

**Methods:**

| Method | Args | Description |
|--------|------|-------------|
| `RegisterStatusNotifierHost` | `service: string` | Register as a tray host (call once on startup) |
| `RegisterStatusNotifierItem` | `service: string` | (Producer side, ignore for consumer) |

**Properties:**

| Property | Type | Description |
|----------|------|-------------|
| `RegisteredStatusNotifierItems` | `QStringList` | List of currently registered SNI services |
| `IsStatusNotifierHostRegistered` | `bool` | True when a host is registered |
| `ProtocolVersion` | `int` | Currently 0 on live Plasma 6.5.5 |

**Signals:**

| Signal | Args | When |
|--------|------|------|
| `StatusNotifierItemRegistered` | `service: string` | New tray item appeared |
| `StatusNotifierItemUnregistered` | `service: string` | Tray item removed |
| `StatusNotifierHostRegistered` | — | A host registered |
| `StatusNotifierHostUnregistered` | — | A host unregistered |

### How Items Identify Themselves

Each registered item string is in one of these formats:
- `org.kde.StatusNotifierItem-<PID>-<instance>` — normal KDE items (e.g. `org.kde.StatusNotifierItem-2910-1`)
- `:1.97/org/ayatana/NotificationItem/spotify_client` — Ayatana/appindicator items
- `<bus-name>/StatusNotifierItem` — generic items

The service name is used as the D-Bus service, and the path is always `/StatusNotifierItem`
unless specified as a full `objectPath/service` pair.

### Generating D-Bus Proxy with qdbusxml2cpp

```cmake
# In CMakeLists.txt
qt_add_dbus_interface(
    SOURCES
    /usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierWatcher.xml
    statusnotifierwatcher_interface
)
```

This generates `statusnotifierwatcher_interface.h/.cpp` with a `OrgKdeStatusNotifierWatcherInterface` class.

---

## Part 2: StatusNotifierItem (Per-Item Protocol)

### D-Bus Interface

Service: `<registered_service_name>` (from watcher's RegisteredStatusNotifierItems)
Path: `/StatusNotifierItem`
Interface: `org.kde.StatusNotifierItem`

XML definition: `/usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierItem.xml`

### Properties to Read

| Property | D-Bus Type | Qt Type | Description |
|----------|-----------|---------|-------------|
| `Category` | `s` | `QString` | `"ApplicationStatus"`, `"Communications"`, `"SystemServices"`, `"Hardware"` |
| `Id` | `s` | `QString` | Unique identifier |
| `Title` | `s` | `QString` | Human-readable name |
| `Status` | `s` | `QString` | `"Passive"`, `"Active"`, `"NeedsAttention"` |
| `WindowId` | `i` | `int` | Associated X11 window ID (may be 0) |
| `IconThemePath` | `s` | `QString` | Extra path for icon lookup |
| `Menu` | `o` | `QDBusObjectPath` | D-Bus object path of context menu |
| `ItemIsMenu` | `b` | `bool` | If true, primary click shows menu (not Activate) |
| `IconName` | `s` | `QString` | Theme icon name (preferred over pixmap) |
| `IconPixmap` | `a(iiay)` | `KDbusImageVector` | Fallback icon as raw pixel data |
| `OverlayIconName` | `s` | `QString` | Badge overlay icon name |
| `OverlayIconPixmap` | `a(iiay)` | `KDbusImageVector` | Badge overlay pixel data |
| `AttentionIconName` | `s` | `QString` | Icon shown when `Status == NeedsAttention` |
| `AttentionIconPixmap` | `a(iiay)` | `KDbusImageVector` | Attention icon pixel data |
| `AttentionMovieName` | `s` | `QString` | Animated attention (rarely used) |
| `ToolTip` | `(sa(iiay)ss)` | `KDbusToolTipStruct` | struct: (iconName, iconPixmap, title, subTitle) |

### Methods to Call

| Method | Args | When to call |
|--------|------|-------------|
| `Activate` | `x: int, y: int` | Left click on icon (screen coordinates) |
| `SecondaryActivate` | `x: int, y: int` | Middle click |
| `ContextMenu` | `x: int, y: int` | Right click (if `!ItemIsMenu`) |
| `Scroll` | `delta: int, orientation: string` | Scroll wheel (`"Horizontal"` or `"Vertical"`) |
| `ProvideXdgActivationToken` | `token: string` | Before Activate on Wayland (focus token) |

### Signals to Watch

| Signal | Args | Action |
|--------|------|--------|
| `NewTitle` | — | Re-read `Title` property |
| `NewIcon` | — | Re-read `IconName`/`IconPixmap` |
| `NewAttentionIcon` | — | Re-read `AttentionIconName` |
| `NewOverlayIcon` | — | Re-read `OverlayIconName` |
| `NewMenu` | — | Re-read `Menu` path |
| `NewToolTip` | — | Re-read `ToolTip` |
| `NewStatus` | `status: string` | Update visibility/animation |

### Generating D-Bus Proxy

```cmake
qt_add_dbus_interface(
    SOURCES
    /usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierItem.xml
    statusnotifieritem_interface
)
```

---

## Part 3: DBusMenu (Context Menus)

### Protocol

Context menus use the `com.canonical.dbusmenu` D-Bus interface.
The `Menu` property of an SNI item gives the D-Bus object path on the same service.

### Critical: No Qt6 Library Available

- `libdbusmenu-qt5` exists but links against Qt5 (`libQt5DBus.so.5`) — **cannot use in Qt6**
- There is no `libdbusmenu-qt6` package installed
- Plasma's system tray plugin has an internal DBusMenu implementation but no public headers

**Available options:**

1. **Implement directly** (recommended for minimal dependency): Call `GetLayout` and
   `AboutToShow` via `QDBusInterface`, build a `QMenu` from the response
2. **Vendor the implementation**: The Plasma system tray's internal DBusMenu code is in
   `libsystemtrayplugin.so` but not exposed; could be copied from plasma-workspace source
3. **Use libdbusmenu-qt5 (NOT recommended)**: Qt5 ABI incompatible with Qt6 project

### DBusMenu Interface Summary

Service: same as SNI item's service name
Path: value of `Menu` property (e.g. `/MenuBar`)
Interface: `com.canonical.dbusmenu`

Key methods:
- `GetLayout(parentId: int, recursionDepth: int, propertyNames: string[]) → (revision: uint, layout: DBusMenuLayoutItem)`
- `GetGroupProperties(ids: int[], propertyNames: string[]) → DBusMenuItemList`
- `AboutToShow(id: int) → (needUpdate: bool)` — call before showing menu
- `Event(id: int, eventId: string, data: variant, timestamp: uint)` — send `"clicked"` event

Key signals:
- `LayoutUpdated(revision: uint, parentId: int)` — menu structure changed
- `ItemsPropertiesUpdated(updatedProps: ..., removedProps: ...)` — item properties changed
- `ItemActivationRequested(id: int, timestamp: uint)` — app requests menu item activation

DBusMenuItem properties: `label`, `enabled`, `visible`, `icon-name`, `icon-data`, `type`
(`"separator"` or `"standard"`), `toggle-type` (`"checkmark"` or `"radio"`),
`toggle-state` (0/1), `children-display` (`"submenu"`)

---

## Part 4: What Plasma's Own System Tray Uses (Internal, Not Reusable)

The installed plugin at `/usr/lib/qt6/qml/org/kde/plasma/private/systemtray/libsystemtrayplugin.so`
exposes two classes to QML (via `org.kde.plasma.private.systemtray` module, version 254.0):

- `BaseModel` — abstract base, `QAbstractListModel`
- `StatusNotifierModel` — concrete model with `addSource(source: QString)`, `removeSource(source: QString)`, `dataUpdated(sourceName: QString)` methods

Model roles used in QML (from binary string analysis):
- `category`, `status`, `effectiveStatus`, `decoration`, `displayText`
- `sourceName`, `itemId`, `itemType` (`"StatusNotifier"` or `"Plasmoid"`)
- `DataEngineSource` — key for calling `Plasmoid.activate()`, `Plasmoid.openContextMenu()`, etc.
- `AttentionIconName`, `AttentionIconPixmap`, `ItemIsMenu`

**Why Krema cannot use this**: Requires `Plasmoid` global (only available inside Plasma
applets loaded by the shell), and the module has no public C++ headers installed.

---

## Part 5: Implementation Plan for Krema

### Consumer Registration Protocol

```cpp
// 1. Register as a StatusNotifierHost
QDBusInterface watcher(
    "org.kde.StatusNotifierWatcher",
    "/StatusNotifierWatcher",
    "org.kde.StatusNotifierWatcher",
    QDBusConnection::sessionBus()
);
watcher.call("RegisterStatusNotifierHost", QDBusConnection::sessionBus().baseService());

// 2. Get current items
QDBusReply<QStringList> items = watcher.call("Get",
    "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems");

// 3. Watch for new/removed items
QDBusConnection::sessionBus().connect(
    "org.kde.StatusNotifierWatcher",
    "/StatusNotifierWatcher",
    "org.kde.StatusNotifierWatcher",
    "StatusNotifierItemRegistered",
    this, SLOT(onItemRegistered(QString))
);
QDBusConnection::sessionBus().connect(
    "org.kde.StatusNotifierWatcher",
    "/StatusNotifierWatcher",
    "org.kde.StatusNotifierWatcher",
    "StatusNotifierItemUnregistered",
    this, SLOT(onItemUnregistered(QString))
);
```

### Reading Item Properties

```cpp
void SniHost::loadItem(const QString &serviceAndPath) {
    // Parse service / object path
    // Format: "org.kde.StatusNotifierItem-PID-N" → service = same, path = "/StatusNotifierItem"
    // Format: ":1.X/path" → split on first '/'
    QString service = serviceAndPath;
    QString path = QStringLiteral("/StatusNotifierItem");
    if (serviceAndPath.startsWith(':')) {
        int slash = serviceAndPath.indexOf('/');
        if (slash > 0) {
            service = serviceAndPath.left(slash);
            path = serviceAndPath.mid(slash);
        }
    }

    // Read all properties at once
    QDBusInterface props(service, path, "org.freedesktop.DBus.Properties",
                         QDBusConnection::sessionBus());
    QDBusReply<QVariantMap> reply = props.call("GetAll", "org.kde.StatusNotifierItem");
    if (reply.isValid()) {
        QVariantMap propMap = reply.value();
        QString iconName = propMap["IconName"].toString();
        QString status   = propMap["Status"].toString();
        QString title    = propMap["Title"].toString();
        // ...
    }

    // Subscribe to property-change signals
    QDBusConnection::sessionBus().connect(
        service, path, "org.kde.StatusNotifierItem",
        "NewIcon", this, SLOT(onIconChanged())
    );
}
```

### Activating an Item (Wayland-safe)

```cpp
void SniHost::activateItem(const QString &service, const QString &path,
                           int x, int y) {
    // On Wayland: provide xdg-activation token first
    if (isWayland) {
        QString token = getXdgActivationToken(); // from KWaylandExtras or xdg_activation_v1
        QDBusInterface sni(service, path, "org.kde.StatusNotifierItem",
                           QDBusConnection::sessionBus());
        sni.call("ProvideXdgActivationToken", token);
    }

    QDBusInterface sni(service, path, "org.kde.StatusNotifierItem",
                       QDBusConnection::sessionBus());
    sni.asyncCall("Activate", x, y);
}
```

### CMakeLists.txt Integration

```cmake
find_package(Qt6 REQUIRED COMPONENTS DBus)

# Generate proxies from installed XML files
qt_add_dbus_interface(sni_dbus_sources
    /usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierWatcher.xml
    statusnotifierwatcher_interface
)
qt_add_dbus_interface(sni_dbus_sources
    /usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifierItem.xml
    statusnotifieritem_interface
)

target_sources(krema PRIVATE ${sni_dbus_sources})
target_link_libraries(krema PRIVATE Qt6::DBus)
```

**Note**: Do NOT add `dbusmenu-qt5` to dependencies — it is Qt5-only and will cause
ABI conflicts with the Qt6 build.

---

## Part 6: C++ Model Design for Krema

```cpp
// Suggested model structure (to implement ourselves)
class SniModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ServiceRole       = Qt::UserRole + 1,
        PathRole,
        ItemIdRole,        // SNI "Id" property
        TitleRole,         // SNI "Title" property
        CategoryRole,      // "ApplicationStatus"|"Communications"|"SystemServices"|"Hardware"
        StatusRole,        // "Passive"|"Active"|"NeedsAttention"
        IconNameRole,      // theme icon name
        IconPixmapRole,    // QIcon from raw pixmap data (fallback)
        OverlayIconNameRole,
        AttentionIconNameRole,
        ToolTipTitleRole,
        ToolTipSubTitleRole,
        MenuPathRole,      // QDBusObjectPath for context menu
        ItemIsMenuRole,    // bool: show menu on primary click
    };
    // ...
};
```

---

## Package Dependencies

| Package | Version | Use |
|---------|---------|-----|
| `qt6-base` | 6.11+ | Qt6::DBus for D-Bus communication |
| `kstatusnotifieritem` | KF6 | NOT needed for consumer (producer-only library) |
| ~~`dbusmenu-qt5`~~ | 0.9.2 | NOT compatible (Qt5-only) |

**Arch Linux packages**: only `qt6-base` needed (Qt6::DBus) for SNI consumer implementation.
No new packages required beyond what Krema already links.

---

## Plasma Reference

Plasma's system tray source (plasma-workspace, not installed as headers):
- Model: `applets/systemtray/systemtraymodel.h` (internal to plasma-workspace)
- QML applet: compiled into `org.kde.plasma.systemtray.so` — QML sources embedded as compiled AOT
- Private plugin: `org.kde.plasma.private.systemtray` — the model classes
- kded watcher: `statusnotifierwatcher.so` at `/usr/lib/qt6/plugins/kf6/kded/`

The kded `statusnotifierwatcher.so` is what runs as the central registry. It's always
available when Plasma is running, so Krema can rely on `org.kde.StatusNotifierWatcher`
being present.

---

## Gotchas

1. **Ayatana/AppIndicator items**: Format is `":BUS_ID/org/ayatana/NotificationItem/APP_NAME"` —
   the path is not `/StatusNotifierItem`. Always parse the service string carefully.

2. **No Qt6 DBusMenu library**: Must implement context menu D-Bus calls manually or vendor
   the implementation. `libdbusmenu-qt5` links against Qt5 and must not be used.

3. **Wayland icon coordinates**: When calling `Activate(x, y)`, use screen-relative coordinates
   (not window-relative). Also call `ProvideXdgActivationToken` before `Activate` on Wayland.

4. **ItemIsMenu**: If this property is `true`, the left click should show the context menu
   (call `ContextMenu`), not `Activate`. Applications like network manager set this.

5. **Icon fallback chain**: Prefer `IconName` (theme lookup) → if empty, use `IconPixmap`
   (raw image data, type `a(iiay)` = array of (width, height, ARGB32 bytes)).

6. **Status visibility rules**:
   - `"Passive"` → hide unless user shows all items (like Plasma's "hidden" area)
   - `"Active"` → always show
   - `"NeedsAttention"` → show with attention animation

7. **PropertiesChanged signal**: Some apps send `org.freedesktop.DBus.Properties.PropertiesChanged`
   instead of individual `NewIcon`/`NewStatus` signals. Subscribe to both for robustness.

8. **kded statusnotifierwatcher may restart**: Monitor the D-Bus name
   `org.kde.StatusNotifierWatcher` with `QDBusServiceWatcher` and re-register when it appears.
