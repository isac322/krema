# Multi-Monitor Support

> Source: Qt 6 / LayerShellQt headers, KScreen headers, Krema source

## QScreen API

### Key Properties

| Property | Type | Description |
|----------|------|-------------|
| `geometry()` | QRect | Full screen rectangle in virtual desktop coords |
| `availableGeometry()` | QRect | Geometry minus panels/taskbars |
| `devicePixelRatio()` | qreal | HiDPI scale factor (e.g., 1.0, 1.5, 2.0) |
| `name()` | QString | Output name (e.g., "HDMI-A-1", "eDP-1") — stable across sessions |
| `manufacturer()` | QString | Display manufacturer |
| `model()` | QString | Display model |
| `size()` | QSize | Screen size in pixels |
| `physicalSize()` | QSizeF | Physical size in mm |
| `refreshRate()` | qreal | Refresh rate in Hz |
| `orientation()` | Qt::ScreenOrientation | Current orientation |
| `virtualSiblings()` | QList<QScreen*> | All screens in virtual desktop |

### Key Signals

```cpp
void geometryChanged(const QRect &geometry);
void availableGeometryChanged(const QRect &geometry);
void physicalSizeChanged(const QSizeF &size);
void physicalDotsPerInchChanged(qreal dpi);
void logicalDotsPerInchChanged(qreal dpi);
void virtualGeometryChanged(const QRect &rect);
void primaryOrientationChanged(Qt::ScreenOrientation orientation);
void orientationChanged(Qt::ScreenOrientation orientation);
void refreshRateChanged(qreal refreshRate);
```

---

## QGuiApplication Screen Management

**Header**: `<QGuiApplication>`

### Static Methods

```cpp
static QList<QScreen *> screens();          // All connected screens
static QScreen *primaryScreen();            // Primary screen
static QScreen *screenAt(const QPoint &point); // Screen containing a point
static QWindow *focusWindow();              // Window with keyboard focus (may be null on Wayland)
```

### Signals

```cpp
void screenAdded(QScreen *screen);
void screenRemoved(QScreen *screen);
void primaryScreenChanged(QScreen *screen);
void focusWindowChanged(QWindow *focusWindow);
```

### Usage

```cpp
// Get all screens
for (QScreen *screen : QGuiApplication::screens()) {
    qDebug() << screen->name() << screen->geometry();
}

// React to screen hot-plug
connect(qApp, &QGuiApplication::screenAdded, this, &DockManager::onScreenAdded);
connect(qApp, &QGuiApplication::screenRemoved, this, &DockManager::onScreenRemoved);
connect(qApp, &QGuiApplication::primaryScreenChanged, this, &DockManager::onPrimaryScreenChanged);
```

---

## QWindow Screen Assignment

**Header**: `<QWindow>`

```cpp
QScreen *screen() const;           // Current screen (may be nullptr on virtual compositors)
void setScreen(QScreen *screen);   // Assign to a specific screen — call BEFORE show()
// Signal:
void screenChanged(QScreen *screen); // Emitted when QWindow moves to a new screen
```

**Critical**: Call `setScreen()` BEFORE `show()`. After `show()`, a layer-shell surface is already bound to its `wl_output`; moving it to another output requires destroying the surface and recreating it on the new screen — `hide()`/`setScreen()`/`show()` does **not** re-bind (see the verified correction below).

> **Verified correction (issue #18, Qt 6.10 / LayerShellQt 6.6):** `QWindow::setScreen()` alone is **not** honored for layer-shell surfaces on QtWayland. Empirically (2-output `kwin_wayland --virtual` + `WAYLAND_DEBUG`), the window reports the requested screen before show, then re-derives the *primary* `wl_output` when the layer surface maps — `get_layer_surface` binds the wrong output regardless of the earlier `setScreen`. The surface must instead be pinned via `LayerShellQt::Window::setScreen()` (see below). `QWindow::setScreen` is still worth calling so pre-show geometry reads see the right screen, but it does not pin the layer surface by itself.

---

## LayerShellQt ScreenConfiguration

**Header**: `<LayerShellQt/Window>`

```cpp
enum ScreenConfiguration {
    ScreenFromQWindow   = 0,  // Use QWindow::screen() — app controls placement (default)
    ScreenFromCompositor = 1, // Let compositor decide — pass nil output
};
```

### LayerShellQt::Window::setScreen (the working output pin)

The deprecated `ScreenConfiguration` enum maps to the modern pair
`Window::setScreen(QScreen*)` + `Window::setWantsToBeOnActiveScreen(bool)`:

```cpp
auto *layerWin = LayerShellQt::Window::get(view);
layerWin->setScreen(screen);              // resets wantsToBeOnActiveScreen to false
// optional explicit:
// layerWin->setWantsToBeOnActiveScreen(false);
```

- `layerWin->screen()` is what `QWaylandLayerSurface` passes to `get_layer_surface` as the `wl_output`. When `screen()` is null and `wantsToBeOnActiveScreen()` is false, it falls back to `QWindow::screen()` — which, per the note above, is unreliable on QtWayland once the surface maps.
- **Use `LayerShellQt::Window::setScreen` for all multi-monitor modes** (all-screens, primary-only, follow-active, selected-screens). Verify against `qwaylandlayersurface.cpp:29-46` in the LayerShellQt source: `m_interface->screen()` is read first, then `window->window()->screen()`.

### ScreenFromCompositor (Single Monitor — Deprecated pattern)

- App doesn't specify output
- Compositor decides which output to place the surface on
- Use only when you genuinely don't care which screen the dock appears on

---

## Multi-Monitor Architecture

`src/config/krema.kcfg` stores `MonitorMode` as an integer with the following values:

| Value | C++ mode | Behavior settings label |
|---|---|---|
| 0 | `PrimaryOnly` | Primary monitor only |
| 1 | `AllScreens` | All monitors |
| 2 | `FollowActive` | Follow active screen |
| 3 | `SelectedScreens` | Selected monitors |

The default remains 0. `SelectedOutputs` is a KConfigXT `StringList` (`QStringList` in C++) with an empty default. It stores exact `QScreen::name()` values, not display labels or hardware identities. Modes 0, 1, and 2 do not use this list and do not clear it.

### Mode 1: All Screens (One Dock Per Screen)

```
Screen 1 (eDP-1)         Screen 2 (HDMI-A-1)
+-------------------+    +-------------------+
|                   |    |                   |
|   [dock window]   |    |   [dock window]   |
+-------------------+    +-------------------+
```

- Maintain `QMap<QScreen*, DockView*>` in a `DockManager` class
- On `screenAdded`: create a new `DockView`, call `setScreen()`, then `show()`
- On `screenRemoved`: call `deleteLater()` on that view (compositor already dismissed it)
- Each dock gets its own `DockModel` OR shares a single model with per-screen `filterByScreen=true`

```cpp
void DockManager::onScreenAdded(QScreen *screen) {
    auto *view = new DockView(createPlatform(), m_settings);
    view->setScreen(screen);  // for pre-show geometry reads; does NOT pin the surface
    auto *layerWin = LayerShellQt::Window::get(view);
    layerWin->setScreen(screen);  // this is what get_layer_surface binds to
    view->initialize(...);    // calls show() internally
    m_views[screen] = view;
}

void DockManager::onScreenRemoved(QScreen *screen) {
    if (auto *view = m_views.take(screen)) {
        view->deleteLater();  // compositor already dismissed the surface
    }
}
```

### Mode 0: Primary Only (Single Dock on the Plasma Primary)

### The Plasma primary is NOT `QGuiApplication::primaryScreen()` (verified)

On QtWayland, `primaryScreen()` is the **first `wl_output` the registry announces** and `primaryScreenChanged` never fires afterwards — it has nothing to do with KWin's per-user output priority. KWin publishes the user-visible order (the same priority the System Settings display page edits via `kde_output_device_v2`) through the **`kde_output_order_v1`** protocol — the one plasmashell uses for panel placement. Krema resolves it via `OutputOrderMonitor` (`src/shell/outputordermonitor.{h,cpp}`), vendored protocol XML at `src/protocols/kde-output-order-v1.xml` (upstream treats it as a DE implementation detail and does not install it; license MIT-CMU). The monitor:

- emits `primaryOutputChanged` when the resolved primary changes, and `orderReadyChanged` when the first order list arrives — callers should wait for readiness before placing surfaces so they never briefly land on the first-announced output;
- resolves to the first ordered name mapping to a live `QScreen` (unknown names skipped; mirrors LibKWorkspace `OutputOrderWatcher`);
- falls back to `QGuiApplication::primaryScreen()` when the global is absent;
- is intentionally leaked: destroying a Wayland client object after the Qt Wayland platform tears down the display crashes on some Qt versions.

In Primary Only mode, a primary change recreates the dock and preview shells: layer surfaces bind their `wl_output` at `get_layer_surface` time and cannot migrate. Selected Screens instead retains shells whose selected outputs are still usable.

- One `DockView` that tracks `OutputOrderMonitor::instance()->primaryScreen()`
- On `primaryOutputChanged`: destroy the shell and `createShellForScreen(newPrimary)` (hide/setScreen/show is insufficient — the surface is already bound)

```cpp
void DockManager::onPrimaryScreenChanged(QScreen *newPrimary) {
    // Layer surfaces bind their wl_output at get_layer_surface() time;
    // re-showing the same view keeps the old output. Recreate the shell.
    m_shell.reset();
    m_shell = createShellForScreen(newPrimary);
}
```

### Mode 2: Follow Active (Single Dock Follows Mouse/Focus)

See "Active Screen Detection" section below for detection strategy.

Architecture is the same as "All Screens" but:
- All dock windows exist simultaneously
- Only the "active" one is visible
- Switching currently shows/hides the docks instantly; configured fade/slide animations remain unimplemented

### Mode 3: Selected Screens (One Dock Per Selected Usable Output)

`MultiDockManager::reconcileSelectedScreens()` intersects `SelectedOutputs` with live `QScreen` names whose geometry is non-empty. It removes only shells outside that set and creates only missing shells. Changing the primary output or adding/removing an unselected output does not recreate retained docks or their preview surfaces.

If no selected output is usable, the manager keeps one temporary shell on the resolved Plasma primary output, provided that output has usable geometry. It never adds that name to `SelectedOutputs`. When a selected output returns, its dock replaces the fallback; if the last selected output disappears, the temporary primary dock returns.

`SettingsWindow::availableScreens` exposes output-name rows with `name`, `label`, `available`, and `primary` fields. The Behavior page shows native switches only in mode 3. Saved names that are disconnected remain as removable rows; switching one off removes it from the list and the row. `hasSelectedMonitorFallback` controls the warning when the selection is empty or unavailable.

Selection changes keep the shared Settings window open and focused. Newly created docks inherit its interaction lock, so Auto hide cannot hide them until Settings closes. Mode 3 and the exact saved name list restore after restart. This adds no per-screen styling editor, dependency, or Wayland protocol.

#### Shortcut routing

`MultiDockManager::primaryShell()` chooses the selected primary dock when it exists, otherwise the first selected live dock in `OutputOrderMonitor::outputOrder()` (the adopted compositor order), then the first live name in saved selection order. Temporary fallback uses the primary dock.

Toggle Dock and Meta+number actions use this target through `activeShell()`. Focus Dock uses `shellAtCursor()`: it prefers a dock on the cursor's screen and uses the same deterministic fallback when the cursor is on an unselected output. Preview surfaces stay pinned to their own selected output.


---

## Active Screen Detection

### Strategy 1: Follow Mouse Cursor

**Verified (SET-008): `QCursor::pos()` is not a global pointer position on Wayland.** QtWayland only knows the pointer while it is over one of the client's own surfaces, and polling is forbidden anyway (`.agents/rules/async-state.md`). The pointer has to enter a mapped surface of the dock on the target screen.

Follow Active unmaps the inactive screens' docks (`setShellVisible(false)` → `QWindow::hide()`), and an unmapped surface receives no `wl_pointer.enter`, so reacting to a hover on the hidden dock (its `DockVisibilityController`) never fires. Krema instead maps an `EdgeTrigger` (`src/shell/edgetrigger.{h,cpp}`) on every inactive screen while the Mouse or Composite trigger is selected:

- a transparent `QRasterWindow` (no scene graph) configured through the same `DockPlatform` as the dock: layer-shell `LayerTop`, namespace `dock` (Show Desktop keeps it), pinned output, exclusive zone -1, anchored to the dock edge
- `kEdgeTriggerThickness` (4 px, `src/utils/inputregion.h`) thick along the whole edge: the same area as a hidden auto-hide dock's trigger strip
- `QEvent::Enter` / `QEvent::Leave` on the window drive `MultiDockManager::onEdgeTriggerHovered()`; the switch is debounced (300 ms) and cancelled if the pointer leaves first
- ignored while the active dock is held by an interaction lock or keyboard navigation (`DockVisibilityController::isInteracting()`): an open Settings dialog, context menu, preview or drag keeps the dock on its screen
- a surface of the strip's size must still get a buffer: set the `QWindow` size after `DockPlatform::setSize()`, which on `KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE` resizes the window to `QSize(0, h)` itself

### Strategy 2: Follow Active Window (Recommended)

Use `TaskManager::AbstractTasksModel::IsActive` and `ScreenGeometry` roles:

```cpp
// In DockModel/DockManager — watch for active window changes
connect(m_tasksModel.get(), &QAbstractItemModel::dataChanged, this,
    [this](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
        if (roles.contains(TaskManager::AbstractTasksModel::IsActive)) {
            updateActiveScreen();
        }
    });

void DockManager::updateActiveScreen() {
    for (int row = 0; row < m_tasksModel->rowCount(); ++row) {
        auto idx = m_tasksModel->index(row, 0);
        if (idx.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            QRect windowScreen = idx.data(TaskManager::AbstractTasksModel::ScreenGeometry).toRect();
            QScreen *screen = QGuiApplication::screenAt(windowScreen.center());
            if (screen && screen != m_activeScreen) {
                m_debounceTimer.start(200);  // Debounce 200ms
                m_pendingScreen = screen;
            }
            break;
        }
    }
}
```

- `AbstractTasksModel::ScreenGeometry` — returns `QRect` of the screen the window is on
- `AbstractTasksModel::IsActive` — returns `bool`, true for the currently active (focused) window
- Requires `dataChanged` signal with role filtering

### Debounce for Follow-Active Mode

```cpp
// In constructor:
m_debounceTimer.setSingleShot(true);
m_debounceTimer.setInterval(200);  // 200ms debounce
connect(&m_debounceTimer, &QTimer::timeout, this, &DockManager::performScreenSwitch);
```

- 200-300ms debounce prevents flicker when rapidly alt-tabbing between apps on different screens
- Timer is permissible per Krema rules (UI delay, not business logic)

---

## Screen Transition Animations (Follow-Active Mode)

### Research Results from Other Docks

| Dock | Multi-Monitor Behavior | Animation Type |
|------|----------------------|----------------|
| Latte Dock (KDE, discontinued) | One dock per screen, independent containments | No migration animation |
| macOS Dock | Auto-show on secondary screens when cursor approaches edge | Fade + slide-up (~300ms) |
| GNOME Dash-to-Dock | One dock per screen (like Plasma panels) | No migration animation |
| GNOME Dash-to-Panel | Identical panel on each screen | No migration animation |
| Plank (elementary OS) | One dock per screen (or multiple instances) | No migration animation |
| KDE Taskmanager Panel | Bound to containment screen via shell | No migration animation |

**Key insight**: No reference dock "moves" between screens. The conventional pattern for docks is independent-docks-per-screen. "Follow active" with migration is a Krema-specific UX.

### Recommended Animation for Krema Follow-Active

**Fade-out + fade-in** (not slide):
- Slide requires knowing screen positions relative to each other — complex, platform-dependent
- Fade is screen-geometry-independent and works at any screen arrangement
- Duration: 150ms fade-out, 150ms fade-in (total 300ms)

QML implementation sketch:
```qml
// DockView.qml — controlled by DockManager
property bool isActiveDock: false

NumberAnimation on opacity {
    id: fadeIn
    to: 1.0; duration: 150; easing.type: Easing.OutCubic
}
NumberAnimation on opacity {
    id: fadeOut
    to: 0.0; duration: 150; easing.type: Easing.InCubic
    onFinished: dockWindow.visible = false
}
```

---

## Screen Geometry and Coordinate Systems

### Virtual Desktop Coordinates

All screens share a virtual desktop coordinate space:

```
(0,0)
  +--Screen 1 (1920x1080)--+--Screen 2 (2560x1440)--+
  |                         |                          |
  |   geo: (0,0,1920,1080) |   geo: (1920,0,2560,1440)|
  |                         |                          |
  +-------------------------+--------------------------+
```

### DPI-Aware Geometry

```cpp
QScreen *screen = window->screen();

// Logical pixels (what Qt uses for layout)
QRect logicalGeo = screen->geometry();

// Physical pixels
qreal ratio = screen->devicePixelRatio();
```

### Finding Screen for a Point

```cpp
QScreen *screen = QGuiApplication::screenAt(QPoint(x, y));
// Returns nullptr if point is not on any screen
```

---

## Screen Change Handling (Already in DockView)

Krema's `DockView::handleScreenChanged()` already handles:
1. Disconnect old screen's `geometryChanged` signal
2. Reconnect to new screen's `geometryChanged` signal
3. `hide()` + `updateSize()` + `applyBackgroundStyle()` + `show()` for surface recreation

For M8, the `DockManager` layer above `DockView` will additionally need to:
- Update `DockModel::tasksModel()->setScreenGeometry()` when active screen changes
- Manage `filterByScreen=true` when in "All Screens" mode to show only that screen's windows

---

## Per-Screen Settings Pattern

KConfigXT does not support dynamic group names, so per-screen overrides must use raw KConfig:

```cpp
#include <KSharedConfig>
#include <KConfigGroup>

// Reading per-screen override with fallback to global
KConfigGroup global = KSharedConfig::openConfig()->group(QStringLiteral("General"));
KConfigGroup screenGroup = global.group(QStringLiteral("Screen-") + screen->name());

// screenGroup falls back to global when key not present:
int edge = screenGroup.readEntry("Edge", global.readEntry("Edge", 2));
```

**Pattern**: Store screen-specific data under group `"Screen-{output-name}"` in `kremarc`.
- Key: `QScreen::name()` (e.g., "eDP-1", "HDMI-A-1") — stable across reboots, changes on cable swap
- Alternative: `QScreen::serialNumber()` — more stable but not always available

---

## KScreen Integration (Optional — Advanced)

For reading display configuration (primary screen designation, output priorities):

**Header**: `<kscreen/config.h>`, `<kscreen/getconfigoperation.h>`, `<kscreen/configmonitor.h>`
**Package**: `libkscreen`

```cpp
#include <kscreen/getconfigoperation.h>
#include <kscreen/configmonitor.h>

// Async config fetch
auto *op = new KScreen::GetConfigOperation();
connect(op, &KScreen::ConfigOperation::finished, this, [this](KScreen::ConfigOperation *op) {
    auto config = qobject_cast<KScreen::GetConfigOperation*>(op)->config();
    KScreen::OutputPtr primary = config->primaryOutput();
    // primary->name() matches QScreen::name()

    // Watch for configuration changes (plug/unplug, primary changes)
    KScreen::ConfigMonitor::instance()->addConfig(config);
    connect(KScreen::ConfigMonitor::instance(), &KScreen::ConfigMonitor::configurationChanged,
            this, &DockManager::onDisplayConfigChanged);
});
op->start();
```

**Note**: Selected Screens adds no KScreen dependency. Krema uses the existing `OutputOrderMonitor` for the Plasma primary and adopted output order; `QGuiApplication::primaryScreenChanged` is not sufficient on QtWayland. The optional KScreen example above is for additional output metadata.

---

## Design Considerations

1. **Screen naming**: Use `QScreen::name()` for persistent config (e.g., "HDMI-A-1"). Changes on cable swap.
2. **Hot-plug**: Always handle `screenAdded`/`screenRemoved` — monitors are connected/disconnected at runtime
3. **DPI differences**: Each screen can have different `devicePixelRatio()` — Krema's zoom/sizing must scale per-screen
4. **closeOnDismissed=false**: Already set in Krema. When output removed, compositor dismisses but window object survives for reassignment.
5. **Virtual siblings**: `QScreen::virtualSiblings()` returns all screens in the same virtual desktop
6. **Null screen check**: Always check `screen()` — can be nullptr on virtual compositors (anti-pattern rule in `.agents/rules/wayland-surfaces.md`)
7. **Surface recreation after setScreen**: `hide()` + `show()` is required — compositor must create a new layer-shell surface on the new output
8. **Initialization order**: `setScreen()` must be called BEFORE `show()` for the first showing
9. **QQuickView subclass**: `DockView` inherits from `QQuickView` which inherits from `QWindow`. `setScreen()` is available directly.
