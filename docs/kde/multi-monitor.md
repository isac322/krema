# Multi-Monitor Support

> Source: Qt 6 / LayerShellQt headers

## QScreen API

### Key Properties

| Property | Type | Description |
|----------|------|-------------|
| `geometry()` | QRect | Full screen rectangle in virtual desktop coords |
| `availableGeometry()` | QRect | Geometry minus panels/taskbars |
| `devicePixelRatio()` | qreal | HiDPI scale factor (e.g., 1.0, 1.5, 2.0) |
| `name()` | QString | Output name (e.g., "HDMI-A-1", "eDP-1") |
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
void orientationChanged(Qt::ScreenOrientation orientation);
void refreshRateChanged(qreal refreshRate);
```

---

## QGuiApplication Screen Management

### Static Methods

```cpp
static QList<QScreen *> screens();       // All connected screens
static QScreen *primaryScreen();          // Primary screen
```

### Signals

```cpp
void screenAdded(QScreen *screen);
void screenRemoved(QScreen *screen);
void primaryScreenChanged(QScreen *screen);
```

### Usage

```cpp
// Get all screens
for (QScreen *screen : QGuiApplication::screens()) {
    qDebug() << screen->name() << screen->geometry();
}

// React to screen changes
connect(qApp, &QGuiApplication::screenAdded, this, &DockManager::onScreenAdded);
connect(qApp, &QGuiApplication::screenRemoved, this, &DockManager::onScreenRemoved);
```

---

## LayerShellQt ScreenConfiguration

```cpp
enum ScreenConfiguration {
    ScreenFromQWindow = 0,    // Use QWindow::screen() — app controls placement
    ScreenFromCompositor = 1, // Let compositor decide — pass nil output
};
```

### ScreenFromQWindow (Multi-Monitor)

- App explicitly sets `QWindow::setScreen(screen)` before showing
- LayerShellQt sends the selected output to the compositor
- **Use this for multi-monitor**: one dock window per screen

### ScreenFromCompositor (Single Monitor)

- App doesn't specify output
- Compositor decides which output to place the surface on
- Simpler, but no multi-monitor control

---

## Multi-Monitor Dock Architecture

### Option A: One Window Per Screen

```
Screen 1 (eDP-1)     Screen 2 (HDMI-A-1)
+----------------+    +----------------+
|                |    |                |
|                |    |                |
|  [dock window] |    |  [dock window] |
+----------------+    +----------------+
```

- Create separate `QWindow` for each screen
- Each window gets its own `LayerShellQt::Window` configuration
- Use `ScreenFromQWindow` configuration
- React to `screenAdded`/`screenRemoved` to create/destroy windows

```cpp
void DockManager::setupForScreen(QScreen *screen) {
    auto *window = new DockWindow();
    window->setScreen(screen);

    auto *layer = LayerShellQt::Window::get(window);
    layer->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
    layer->setAnchors(LayerShellQt::Window::AnchorBottom
                    | LayerShellQt::Window::AnchorLeft
                    | LayerShellQt::Window::AnchorRight);
    layer->setLayer(LayerShellQt::Window::LayerTop);

    window->show();
    m_windows[screen] = window;
}

void DockManager::onScreenRemoved(QScreen *screen) {
    if (auto *window = m_windows.take(screen)) {
        window->deleteLater();
    }
}
```

### Option B: Single Window, User-Selected Screen

```
Screen 1 (eDP-1)     Screen 2 (HDMI-A-1)
+----------------+    +----------------+
|                |    |                |
|                |    |                |
|                |    |  [dock window] |
+----------------+    +----------------+
```

- One `QWindow` with user-configured screen
- Switch screen via `window->setScreen(newScreen)`
- Simpler state management
- Settings store screen name for persistence

### Recommendation

**Start with Option B** (single window, user-selected screen) for initial implementation. Expand to Option A when multi-monitor feature is needed.

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

// Logical pixels (what Qt uses)
QRect logicalGeo = screen->geometry();

// Physical pixels
qreal ratio = screen->devicePixelRatio();
QRect physicalGeo(logicalGeo.x() * ratio, logicalGeo.y() * ratio,
                  logicalGeo.width() * ratio, logicalGeo.height() * ratio);
```

### Finding Screen for a Point

```cpp
QScreen *screenAt = QGuiApplication::screenAt(QPoint(x, y));
```

---

## Screen Change Handling

```cpp
// Monitor geometry changes (e.g., resolution change, rotation)
connect(screen, &QScreen::geometryChanged, this, [this](const QRect &geo) {
    // Recalculate dock position and size
    updateDockGeometry();
});

// Monitor available geometry (panel added/removed)
connect(screen, &QScreen::availableGeometryChanged, this, [this](const QRect &geo) {
    updateDockGeometry();
});

// Monitor DPI changes (e.g., user changed scaling)
connect(screen, &QScreen::physicalDotsPerInchChanged, this, [this](qreal dpi) {
    updateDockScaling();
});
```

---

## Design Considerations

1. **Screen naming**: Use `QScreen::name()` for persistent config (e.g., "HDMI-A-1")
2. **Hot-plug**: Always handle `screenAdded`/`screenRemoved` — monitors can be connected/disconnected anytime
3. **DPI differences**: Each screen can have different DPI — scale dock accordingly
4. **Primary screen**: `QGuiApplication::primaryScreen()` may change at runtime
5. **Virtual siblings**: `QScreen::virtualSiblings()` returns all screens in the same virtual desktop
6. **Layer-shell closeOnDismissed**: Set to `false` to allow re-mapping when screens change
