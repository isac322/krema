# Wayland Layer-Shell (LayerShellQt)

> Source: `/usr/include/LayerShellQt/` headers (KDE Plasma 6.5.5)

## Overview

LayerShellQt is a Qt interface library for the `wlr-layer-shell` Wayland protocol. It allows Qt applications to create windows on specific "layers" in a Wayland compositor (docks, panels, overlays, etc.).

**Headers:**
- `/usr/include/LayerShellQt/shell.h` — `Shell` class
- `/usr/include/LayerShellQt/window.h` — `Window` class

---

## Shell Class

Static utility class to enable layer-shell mode.

```cpp
#include <LayerShellQt/Shell>

// Must be called once at application startup before creating any windows
LayerShellQt::Shell::useLayerShell();
```

---

## Window Class

Configures a `QWindow` to use the `wlr-layer-shell` protocol.

### Getting a Window Instance

```cpp
#include <LayerShellQt/Window>

// Get the LayerShell wrapper for a QWindow (ownership NOT transferred)
LayerShellQt::Window *layerWindow = LayerShellQt::Window::get(qwindow);
```

Also available as QML attached properties via `qmlAttachedProperties(QObject*)`.

### Enums

#### Anchor (Flags — combinable with `|`)

```cpp
enum Anchor {
    AnchorNone   = 0,
    AnchorTop    = 1,
    AnchorBottom = 2,
    AnchorLeft   = 4,
    AnchorRight  = 8,
};
Q_DECLARE_FLAGS(Anchors, Anchor)
```

A bottom dock typically uses: `AnchorBottom | AnchorLeft | AnchorRight`

#### Layer

```cpp
enum Layer {
    LayerBackground = 0,   // Wallpaper level
    LayerBottom     = 1,   // Behind normal windows
    LayerTop        = 2,   // Above windows (typical for docks)
    LayerOverlay    = 3,   // Topmost (notifications, lock screens)
};
```

#### KeyboardInteractivity

```cpp
enum KeyboardInteractivity {
    KeyboardInteractivityNone      = 0,  // No keyboard focus
    KeyboardInteractivityExclusive = 1,  // Exclusive keyboard access
    KeyboardInteractivityOnDemand  = 2,  // Keyboard focus only when needed
};
```

#### ScreenConfiguration

```cpp
enum ScreenConfiguration {
    ScreenFromQWindow   = 0,  // Use QWindow::screen() (default)
    ScreenFromCompositor = 1, // Let compositor decide (pass nil)
};
```

### Properties

| Property | Type | Read | Write | Signal |
|----------|------|------|-------|--------|
| `anchors` | `Anchors` | `anchors()` | `setAnchors()` | `anchorsChanged()` |
| `exclusionZone` | `qint32` | `exclusionZone()` | `setExclusiveZone()` | `exclusionZoneChanged()` |
| `margins` | `QMargins` | `margins()` | `setMargins()` | `marginsChanged()` |
| `layer` | `Layer` | `layer()` | `setLayer()` | `layerChanged()` |
| `keyboardInteractivity` | `KeyboardInteractivity` | `keyboardInteractivity()` | `setKeyboardInteractivity()` | `keyboardInteractivityChanged()` |
| `scope` | `QString` | `scope()` | `setScope()` | — |
| `screenConfiguration` | `ScreenConfiguration` | `screenConfiguration()` | `setScreenConfiguration()` | — |
| `activateOnShow` | `bool` | `activateOnShow()` | `setActivateOnShow()` | — |

### Methods

```cpp
// Anchor — which edges of the screen the window attaches to
void setAnchors(Anchors anchor);
Anchors anchors() const;

// Exclusive zone — space the window reserves (in pixels)
void setExclusiveZone(int32_t zone);
int32_t exclusionZone() const;

// Exclusive edge — which edge the zone applies to
void setExclusiveEdge(Window::Anchor edge);
Window::Anchor exclusiveEdge() const;

// Margins around the window
void setMargins(const QMargins &margins);
QMargins margins() const;

// Desired size
void setDesiredSize(const QSize &size);
QSize desiredSize() const;

// Layer (z-order)
void setLayer(Layer layer);
Layer layer() const;

// Keyboard focus behavior
void setKeyboardInteractivity(KeyboardInteractivity interactivity);
KeyboardInteractivity keyboardInteractivity() const;

// Screen configuration
void setScreenConfiguration(ScreenConfiguration screenConfiguration);
ScreenConfiguration screenConfiguration() const;

// Scope — string identifier for compositor stacking within a layer
void setScope(const QString &scope);
QString scope() const;

// Close when dismissed by compositor (useful for multi-monitor re-mapping)
void setCloseOnDismissed(bool close);
bool closeOnDismissed() const;

// Request activation on show (default: true, ignored if keyboard = None)
void setActivateOnShow(bool activateOnShow);
bool activateOnShow() const;
```

### Signals

| Signal | Description |
|--------|-------------|
| `anchorsChanged()` | Anchors changed |
| `exclusionZoneChanged()` | Exclusive zone changed |
| `exclusiveEdgeChanged()` | Exclusive edge changed |
| `marginsChanged()` | Margins changed |
| `desiredSizeChanged()` | Desired size changed |
| `keyboardInteractivityChanged()` | Keyboard mode changed |
| `layerChanged()` | Layer changed |

---

## Input Region

LayerShellQt does **NOT** have a `setInputRegion` method. Input region control uses:
- `QWindow::setMask(QRegion)` — standard Qt way
- Direct Wayland protocol access (Krema abstracts this via `DockPlatform::setInputRegion`)

Notes:
- Empty `QRegion()` means the **entire surface** receives input (no restriction)
- A hidden dock should still keep a thin trigger strip for hover detection

---

## Surface Coordinate Calculation

Layer-shell surfaces do **not** report screen position via `QWindow::geometry()`. Instead, compute from screen geometry + anchored edge + margin:

```cpp
const QRect screenGeo = dockWindow->screen()->geometry();
const int surfaceW = dockWindow->width();
const int surfaceH = dockWindow->height();

switch (edge) {
case Bottom:
    surfaceX = screenGeo.x();
    surfaceY = screenGeo.y() + screenGeo.height() - surfaceH;
    break;
case Top:
    surfaceX = screenGeo.x();
    surfaceY = screenGeo.y();
    break;
case Left:
    surfaceX = screenGeo.x();
    surfaceY = screenGeo.y();
    break;
case Right:
    surfaceX = screenGeo.x() + screenGeo.width() - surfaceW;
    surfaceY = screenGeo.y();
    break;
}

// Panel screen coordinates = surface origin + panel local offset
QRect panelScreenRect(surfaceX + panelX, surfaceY + panelY, panelW, panelH);
```

---

## Typical Dock Configuration

```cpp
LayerShellQt::Shell::useLayerShell();  // at startup

auto *layerWindow = LayerShellQt::Window::get(qwindow);
layerWindow->setLayer(LayerShellQt::Window::LayerTop);
layerWindow->setAnchors(LayerShellQt::Window::AnchorBottom
                      | LayerShellQt::Window::AnchorLeft
                      | LayerShellQt::Window::AnchorRight);
layerWindow->setExclusiveZone(0);  // 0 = no reserved space (auto-hide dock)
layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
layerWindow->setScope("krema-dock");
layerWindow->setCloseOnDismissed(false);  // allow re-mapping on screen change
```

## Design Notes

- **Wrapping pattern**: `Window` wraps `QWindow`, does not subclass it
- **Anchor flags**: Combine with `|` — e.g., top+left+right for a top panel
- **Exclusive zone**: Positive = reserve space; 0 = no reservation; -1 = special (depends on compositor)
- **Scope**: Compositor may use it for stacking order within the same layer
- **Screen management**: `ScreenFromCompositor` is useful for single-dock setups; `ScreenFromQWindow` for multi-monitor
