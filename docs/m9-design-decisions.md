# M9: Widget System + System Tray — Design Decisions

> Status: **Paused** (2026-03-30). Partially designed, implementation not started.
> Interview session ID: `interview_20260330_010041` (ouroboros interview, resumable)
> Priority shifted to settings window improvements.

---

## Decided

### Architecture
- **Option B: 3-section QML layout** — `[leftWidgets | taskRow | rightWidgets]`
- `hoveredIndex` stays isolated to `dockRepeater` (task items only)
- Widget sections use separate `Repeater` + `QAbstractListModel` (not JS arrays)
- Existing `DockWidget` stub (`src/shell/dockwidget.h`) promoted to real base class

### KDE API Strategy
- **No public KDE library** exists for consuming SNI items
- Plasma's `org.kde.plasma.private.systemtray` is internal (requires `Plasmoid` context)
- **Direct D-Bus implementation** via `Qt6::DBus` (already linked)
- D-Bus XML files available: `/usr/share/dbus-1/interfaces/kf6_org.kde.StatusNotifier{Watcher,Item}.xml`
- **No dbusmenu-qt6** — `libdbusmenu-qt5` is Qt5-only, incompatible. Must implement `com.canonical.dbusmenu` consumer manually.
- No new package dependencies needed

### UX Decisions (from user interview)
| Decision | Choice | Notes |
|----------|--------|-------|
| Tray position | User-configurable (left or right slot) | Via settings UI |
| Tray icon size | User-configurable (default = same as app icons) | Setting for independent tray icon size |
| Parabolic zoom on tray | **No** | Tray icons fixed size, only app icons zoom |
| Passive item handling | Expand/collapse button (Plasma pattern) | User can choose behavior in settings |
| MVP scope | Full SNI + all widgets | No phased split (M9a/M9b) |
| Included widgets | Separator, Spacer, Expand/collapse button | No clock widget |

### SNI Feature Scope (full)
- Left-click: `Activate(x, y)` (or `ContextMenu` if `ItemIsMenu=true`)
- Right-click: `ContextMenu(x, y)` via DBusMenu
- Middle-click: `SecondaryActivate(x, y)`
- Scroll: `Scroll(delta, orientation)`
- Hover: Tooltip display
- `NeedsAttention` status: Visual attention indicator
- Wayland: `ProvideXdgActivationToken` before `Activate`
- Ayatana/AppIndicator format support (`:1.X/path` parsing)
- `QDBusServiceWatcher` for kded restart recovery

---

## Undecided (interview interrupted at Q2)

### Tooltip Style
**Options considered:**
1. Simple text (title only) — matches current app icon tooltips
2. Plasma-style rich tooltip (icon + title + subtitle)
3. Title + subtitle text only (no icon)

**Decision needed:** Which style, and what hover delay?

### Expand/Collapse Button
**Options considered:**
1. Chevron button + slide animation, state persisted across restarts
2. Chevron + count badge showing hidden item count
3. Match Plasma behavior exactly

**Decision needed:** Visual design, animation, persistence, count badge

### NeedsAttention Visual Treatment
**Options considered:**
1. Reuse existing attention animations (bounce/wiggle/glow/pulse/dot/blink from NotificationTracker)
2. Tray-specific expression (glow/pulse only, separate from app attention)
3. AttentionIcon swap only (SNI standard, no animation)

**Decision needed:** Which approach, and how it coexists with NotificationTracker

### ItemIsMenu Behavior
**Question:** Honor `ItemIsMenu=true` literally (left-click → context menu) or offer unified behavior?
**Recommendation:** Honor literally (Plasma standard)

### Widget Configuration UX
**Not discussed:**
- How users add/remove/reorder widgets (drag? settings page?)
- Default widget layout shipped out of the box
- Settings UI page for widget/tray configuration

### Multi-Monitor
**Preliminary decision:** `SniHost`/`SniModel` as global singletons, all docks show same tray.
**Not discussed:** "Tray on primary only" option

---

## Architecture Plan

### New C++ Classes

| Class | Base | Location | Role |
|-------|------|----------|------|
| `WidgetSlotModel` | `QAbstractListModel` | `src/widgets/` | Widget list per slot (left/right) |
| `SeparatorWidget` | `DockWidget` | `src/widgets/` | Thin line separator |
| `SpacerWidget` | `DockWidget` | `src/widgets/` | Flexible space filler |
| `SniHost` | `QObject` | `src/tray/` | StatusNotifierWatcher D-Bus client |
| `SniModel` | `QAbstractListModel` | `src/tray/` | Tray item list model for QML |
| `SniItemTracker` | `QObject` | `src/tray/` | Per-item D-Bus property tracker |
| `SniTrayIconProvider` | `QQuickImageProvider` | `src/tray/` | IconName/IconPixmap to QML Image |
| `DBusMenuImporter` | `QObject` | `src/tray/` | com.canonical.dbusmenu → QMenu |
| `SystemTrayWidget` | `DockWidget` | `src/widgets/` | Widget wrapping SniModel |

### New QML Files

| File | Role |
|------|------|
| `src/qml/WidgetSlot.qml` | Flow + Repeater for widget model |
| `src/qml/widgets/SeparatorWidget.qml` | 2px line, Kirigami.Theme.separatorColor |
| `src/qml/widgets/SpacerWidget.qml` | Transparent expanding Item |
| `src/qml/widgets/SystemTrayWidget.qml` | Flow + Repeater(SystemTrayModel) |
| `src/qml/widgets/TrayIcon.qml` | Individual tray icon + MouseArea |

### Key Modifications

| File | Change |
|------|--------|
| `src/qml/main.qml` | `dockPanel` → 3-section layout (left + dockRow + right) |
| `src/shell/dockwidget.h` | Add `virtual QObject *model()` |
| `src/shell/dockshell.cpp` | Create WidgetSlotModels, register as context properties |
| `src/app/application.cpp` | Create SniHost, register SniModel as QML singleton |
| `src/config/krema.kcfg` | Add LeftWidgets, RightWidgets (StringList) |
| `src/CMakeLists.txt` | Add new source files |

---

## Risk Mitigation

| Risk | Grade | Mitigation |
|------|-------|------------|
| hoveredIndex corruption | CAT-1 | Widget Repeaters separate from task Repeater |
| JS array model lifecycle | CAT-2 | All models are QAbstractListModel |
| Zoom surface height | CAT-3 | Tray icons outside dockRow, no zoom |
| _zoomActive boundary | CAT-4 | dockRow coordinates unchanged within dockLayout |
| No dbusmenu-qt6 | — | Manual QDBusInterface implementation |
| kded restart | — | QDBusServiceWatcher on StatusNotifierWatcher |
| Ayatana format | — | Service string parser handles both formats |

---

## Research Documents

| Document | Content |
|----------|---------|
| `docs/kde/system-tray-apis.md` | Complete D-Bus protocol reference, code examples, gotchas |
| `docs/kde/settings-window-patterns.md` | ConfigurationView patterns (used for settings refactor) |

## NotificationTracker Coexistence

Existing `NotificationTracker` (`src/models/notificationtracker.cpp:381-588`) already tracks SNI items for attention/badge purposes. New `SniHost` runs independently for display. Both subscribe to the same D-Bus signals but serve different roles. Future optimization: have NotificationTracker query SniHost instead of maintaining its own SNI tracking.
