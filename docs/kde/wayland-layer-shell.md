# Wayland Layer-Shell (LayerShellQt)

> Source: `/usr/include/LayerShellQt/` headers (KDE Plasma 6)

## Overview

LayerShellQt is a Qt interface library for the `wlr-layer-shell` Wayland protocol. It allows Qt applications to create windows on specific "layers" in a Wayland compositor (docks, panels, overlays, etc.).

**Headers:**
- `/usr/include/LayerShellQt/shell.h` — `Shell` class
- `/usr/include/LayerShellQt/window.h` — `Window` class

---

## Shell Class

Static utility class to enable layer-shell mode.

```cpp
// Preferred (useLayerShell() is deprecated since Qt 6.10 / LayerShellQt 6.6;
// it only sets this variable). Must run before the first QWindow is created.
qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");

// Deprecated equivalent — literally calls the qputenv above (verified in
// LayerShellQt 6.7.5 source/disassembly):
//   LayerShellQt::Shell::useLayerShell();
```

> **Verified (issue #18):** `useLayerShell()` is deprecated and does nothing
> but `qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell")`. Set the
> environment variable directly and drop the `LayerShellQt/Shell` include.

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

**KWin focus hand-back (verified in KWin 6.7.5 `src/layershellv1window.cpp`
`LayerShellV1Window::handleAcceptsFocusChanged`):** when a Top/Overlay layer
surface switches to a focus-accepting mode, KWin calls
`workspace()->activateWindow(this)` and the surface becomes the *active
window* (the previously active window gets `IsActive=false` in the
TaskManager models). Switching back to `KeyboardInteractivityNone` does
**nothing**: KWin does not call `activateNextWindow()`, so the layer surface
stays active and no task is active. The client must hand focus back itself,
e.g. remember `TasksModel::activeTask()` before enabling interactivity and
call `TasksModel::requestActivate()` on it afterwards (KWin honours that
request with forced activation, `window.cpp` `activeRequested` →
`activateWindow(this, true)`). Krema does this in
`DockVisibilityController::setKeyboardActive()`.

#### ScreenConfiguration (deprecated since 6.6)

```cpp
enum ScreenConfiguration {
    ScreenFromQWindow   = 0,  // Use QWindow::screen() (default)
    ScreenFromCompositor = 1, // Let compositor decide (pass nil)
};
```

Deprecated: use `setScreen(QScreen*)` / `setWantsToBeOnActiveScreen(bool)`
instead. `ScreenFromCompositor` ≡ `setWantsToBeOnActiveScreen(true)`;
`ScreenFromQWindow` ≡ `setWantsToBeOnActiveScreen(false)` + `setScreen(nullptr)`
(the `QWindow::screen()` fallback).

### Properties

| Property | Type | Read | Write | Signal |
|----------|------|------|-------|--------|
| `anchors` | `Anchors` | `anchors()` | `setAnchors()` | `anchorsChanged()` |
| `exclusionZone` | `qint32` | `exclusionZone()` | `setExclusiveZone()` | `exclusionZoneChanged()` |
| `margins` | `QMargins` | `margins()` | `setMargins()` | `marginsChanged()` |
| `layer` | `Layer` | `layer()` | `setLayer()` | `layerChanged()` |
| `keyboardInteractivity` | `KeyboardInteractivity` | `keyboardInteractivity()` | `setKeyboardInteractivity()` | `keyboardInteractivityChanged()` |
| `scope` | `QString` | `scope()` | `setScope()` | — |
| `screen` | `QScreen*` | `screen()` | `setScreen()` | `screenChanged()` |
| `wantsToBeOnActiveScreen` | `bool` | `wantsToBeOnActiveScreen()` | `setWantsToBeOnActiveScreen()` | `wantsToBeOnActiveScreenChanged()` |
| `screenConfiguration` | `ScreenConfiguration` | `screenConfiguration()` | `setScreenConfiguration()` | — (deprecated) |
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

// Screen selection — the output the layer surface binds to.
// setScreen() resets wantsToBeOnActiveScreen to false. When screen() is null
// and wantsToBeOnActiveScreen() is false, QWindow::screen() is used.
// NOTE (verified, issue #18): QWindow::setScreen() alone is NOT honored for
// layer surfaces on QtWayland — the surface re-derives the primary wl_output
// when it maps. Pin via LayerShellQt::Window::setScreen() before show().
void setScreen(QScreen *screen);
QScreen *screen() const;
void setWantsToBeOnActiveScreen(bool set);
bool wantsToBeOnActiveScreen() const;

// Screen configuration (deprecated — use setScreen/wantsToBeOnActiveScreen)
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

## Dynamic Exclusive-Zone Updates

`Window::setExclusiveZone()` changes the layer-shell exclusion-zone request, but the `wlr-layer-shell` state is double-buffered. The compositor applies the new zone only when the client commits the `wl_surface`; changing the wrapper property alone does not publish a new work-area reservation.

When a dock's reservation setting changes while it is mapped, the setting-change callback should update the dock view's geometry and then request a repaint from the public Qt API:

```cpp
void onReserveScreenSpaceChanged()
{
    view->updateSize();
    view->update(); // QQuickWindow::update()
}
```

`QQuickWindow::update()` requests a repaint even when the scene has not otherwise changed. Qt and the Wayland integration then own the frame and surface commit; callers should not add a private Wayland commit, timer, resize workaround, or direct protocol commit for this path.

For a preview surface whose edge-dependent layout changes, call its existing edge-layout update and request `QQuickWindow::update()` only when the preview is visible. Keep this refresh scoped to the setting-change/layout path rather than adding a repaint to every geometry update.

---

## Input Region

LayerShellQt does **NOT** have a `setInputRegion` method. Input region control uses:
- `QWindow::setMask(QRegion)` — standard Qt way
- Direct Wayland protocol access (Krema abstracts this via `DockPlatform::setInputRegion`)

Notes:
- Empty `QRegion()` means the **entire surface** receives input (no restriction)
- A hidden dock should still keep a thin trigger strip for hover detection

### Publishing a changed preview mask

QtWayland's `QWaylandWindow::setMask()` sends `wl_surface.set_input_region`
without committing the surface. The input region is double-buffered, so
the compositor uses the old region until `wl_surface.commit`.
AT-SPI visibility does not establish that the new region has been committed.

`PreviewController::updateInputRegion()` commits each changed mask through
`wl_surface_commit()`. It obtains the existing surface with
`QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window)`;
both the native interface and the surface may be null. This Qt 6.8-compatible
API needs `<qpa/qplatformnativeinterface.h>` and `Qt6::GuiPrivate`.
Qt 6.9+ requires finding the separate `Qt6GuiPrivate` CMake package at the
exact public Qt GUI version.

The preview stays mapped with a 1x1 mask while hidden. Its initial mask is
set before `show()` and left to Qt's normal layer-shell configure sequence;
the explicit commit is only for later mask changes. Unchanged regions do
not generate extra commits. Keep the input region limited to the popup:
expanding it or increasing the hide delay does not fix publication order.

Verified sources:
- [Qt 6.8 QtWayland window implementation](https://github.com/qt/qtwayland/blob/v6.8.0/src/client/qwaylandwindow.cpp) (`setMask`, `updateInputRegion`)
- [Qt 6.8 native surface lookup](https://github.com/qt/qtwayland/blob/v6.8.0/src/client/qwaylandnativeinterface.cpp) (`nativeResourceForWindow`)
- [Qt 6.9 private CMake package handling](https://github.com/qt/qtbase/blob/v6.9.0/cmake/QtModuleConfig.cmake.in)

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
qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");  // at startup, before first QWindow

auto *layerWindow = LayerShellQt::Window::get(qwindow);
layerWindow->setLayer(LayerShellQt::Window::LayerTop);
layerWindow->setAnchors(LayerShellQt::Window::AnchorBottom
                      | LayerShellQt::Window::AnchorLeft
                      | LayerShellQt::Window::AnchorRight);
layerWindow->setExclusiveZone(0);  // 0 = no reserved space (auto-hide dock)
layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
layerWindow->setScope("dock");  // KWin types only "dock" as a Dock (see below)
layerWindow->setCloseOnDismissed(false);  // allow re-mapping on screen change
```

## Design Notes

- **Wrapping pattern**: `Window` wraps `QWindow`, does not subclass it
- **Anchor flags**: Combine with `|` — e.g., top+left+right for a top panel
- **Exclusive zone**: Positive = reserve space; 0 = no reservation; -1 = special (depends on compositor)
- **Scope**: The layer-shell namespace. KWin derives the window type from it (`layershellv1window.cpp` `scopeToType`, case-insensitive): only `"dock"` becomes `WindowType::Dock` (others: `desktop`, `notification`, `tooltip`, `on-screen-display`, `dialog`, `splash`, `utility`); any other namespace becomes `WindowType::Normal`. A normal-typed dock is hidden by Show Desktop (`Workspace::setShowingDesktop` hides every window for which `breaksShowingDesktop()` is true, which excludes docks). Never use a branded namespace such as `"krema-dock"` for the dock surface (issue #16).
- **Known limitation: Krema's popups lack the exemption Plasma's own popups get.** KWin exempts every window of a client that also owns a `desktop`-typed window (`WaylandWindow::belongsToDesktop()`, KWin 6.7.5 `waylandwindow.cpp:136-146`). plasmashell owns a `desktop` layer surface (plasma-workspace 6.3.6 `desktopview.cpp:51`), so its applet popups (`AppletPopup` toplevels, libplasma 6.3.6 `appletpopup.cpp:71`, KWin 6.7.5 `xdgshellwindow.cpp:376-377`) and its `xdg_popup` context menus survive Show Desktop. Krema owns no desktop window, so (a) the window preview (`krema-preview`, a Normal window) is hidden by Show Desktop, and (b) a dock context menu (`xdg_popup`, `WindowType::Unknown`, `xdgshellwindow.cpp` `XdgPopupWindow`) ends Show Desktop when it maps (`Workspace::addWaylandWindow`). Typing the always-mapped preview `"dock"` is not sound: it would count as a second panel for every `isDock()` consumer (`slide`, `slideback`, `magiclamp`, `diminactive`; `slidingnotifications` on 6.7). Adding a `desktop` surface to borrow the exemption is a hack (`setShowingDesktop` focuses the Desktop window). The sound direction: map the preview only while shown and type it `"dock"`, as libplasma does for stay-on-top dialogs (libplasma 6.3.6 `dialog.cpp:753-757`), and/or render the context menu in-scene.
- **Screen management**: `ScreenFromCompositor` is useful for single-dock setups; `ScreenFromQWindow` for multi-monitor
