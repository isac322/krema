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

**Critical**: Call `setScreen()` BEFORE `show()`. After `show()`, a layer-shell surface is already committed to the compositor output; changing screen requires `hide()` then reconfiguration then `show()`.

---

## LayerShellQt ScreenConfiguration

**Header**: `<LayerShellQt/Window>`

```cpp
enum ScreenConfiguration {
    ScreenFromQWindow   = 0,  // Use QWindow::screen() — app controls placement (default)
    ScreenFromCompositor = 1, // Let compositor decide — pass nil output
};
```

### ScreenFromQWindow (Multi-Monitor)

- App explicitly calls `QWindow::setScreen(screen)` before showing
- LayerShellQt sends the selected output to the compositor
- **Use this for all multi-monitor modes** (all-screens, primary-only, follow-active)
- This is the **default** (value 0) — Krema's WaylandDockPlatform doesn't need to set it explicitly

### ScreenFromCompositor (Single Monitor — Deprecated pattern)

- App doesn't specify output
- Compositor decides which output to place the surface on
- Use only when you genuinely don't care which screen the dock appears on

---

## Multi-Monitor Architecture for M8

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
    view->setScreen(screen);  // MUST be before show()
    auto *layerWin = LayerShellQt::Window::get(view);
    layerWin->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);  // explicit for clarity
    view->initialize(...);    // calls show() internally
    m_views[screen] = view;
}

void DockManager::onScreenRemoved(QScreen *screen) {
    if (auto *view = m_views.take(screen)) {
        view->deleteLater();  // compositor already dismissed the surface
    }
}
```

### Mode 2: Primary Only (Single Dock on Chosen Monitor)

- One `DockView` that tracks `QGuiApplication::primaryScreen()`
- On `primaryScreenChanged`: call `hide()`, `setScreen(newScreen)`, `show()`

```cpp
void DockManager::onPrimaryScreenChanged(QScreen *newPrimary) {
    m_view->hide();
    m_view->setScreen(newPrimary);
    m_view->show();
}
```

### Mode 3: Follow Active (Single Dock Follows Mouse/Focus)

See "Active Screen Detection" section below for detection strategy.

Architecture is the same as "All Screens" but:
- All dock windows exist simultaneously
- Only the "active" one is visible
- Transition animation: fade-out on old screen + fade-in on new screen

---

## Active Screen Detection

### Strategy 1: Follow Mouse Cursor

```cpp
QPoint cursorPos = QCursor::pos();   // Global cursor position
QScreen *screen = QGuiApplication::screenAt(cursorPos);
```

- Reliable and simple
- Use `QGuiApplication::focusWindowChanged` or a polling `QTimer` (for mouse-based switching)
- **Problem**: Cursor can be on a screen with no important work; feels wrong when typing on screen A but cursor is on screen B

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

**Note**: KScreen is optional for M8. `QGuiApplication::primaryScreenChanged` is simpler and sufficient for detecting primary screen changes. KScreen is needed only if Krema wants to read output metadata (e.g., serial numbers for stable per-monitor identification).

---

## Design Considerations

1. **Screen naming**: Use `QScreen::name()` for persistent config (e.g., "HDMI-A-1"). Changes on cable swap.
2. **Hot-plug**: Always handle `screenAdded`/`screenRemoved` — monitors are connected/disconnected at runtime
3. **DPI differences**: Each screen can have different `devicePixelRatio()` — Krema's zoom/sizing must scale per-screen
4. **closeOnDismissed=false**: Already set in Krema. When output removed, compositor dismisses but window object survives for reassignment.
5. **Virtual siblings**: `QScreen::virtualSiblings()` returns all screens in the same virtual desktop
6. **Null screen check**: Always check `screen()` — can be nullptr on virtual compositors (CLAUDE.md anti-pattern)
7. **Surface recreation after setScreen**: `hide()` + `show()` is required — compositor must create a new layer-shell surface on the new output
8. **Initialization order**: `setScreen()` must be called BEFORE `show()` for the first showing
9. **QQuickView subclass**: `DockView` inherits from `QQuickView` which inherits from `QWindow`. `setScreen()` is available directly.
