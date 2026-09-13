# KWindowSystem / KWindowEffects / KWaylandExtras

> Source: `/usr/include/KF6/KWindowSystem/` headers

## KWindowEffects Namespace

Visual effects applied to windows (blur, contrast, slide).

**Header:** `/usr/include/KF6/KWindowSystem/kwindoweffects.h`

### Enums

```cpp
enum Effect {
    Slide           = 1,
    BlurBehind      = 7,
    BackgroundContrast = 9,
};

enum SlideFromLocation {
    NoEdge     = 0,
    TopEdge,
    RightEdge,
    BottomEdge,
    LeftEdge,
};
```

### Functions

```cpp
// Check if compositor supports an effect
bool isEffectAvailable(Effect effect);

// Enable blur behind window
// region: logical pixels relative to client area top-left; empty = whole window
void enableBlurBehind(QWindow *window, bool enable = true, const QRegion &region = QRegion());

// Background contrast for readability (panel/dock backgrounds)
void enableBackgroundContrast(QWindow *window,
                             bool enable = true,
                             qreal contrast = 1,    // 0.0-1.0
                             qreal intensity = 1,   // 0.0-1.0
                             qreal saturation = 1,  // 0.0-1.0
                             const QRegion &region = QRegion());

// Frost effect (tinted translucent)
void setBackgroundFrost(QWindow *window,
                       QColor frostColor,
                       const QRegion &region = QRegion());

// Slide window from screen edge (e.g., dock show/hide)
// offset: -1 = auto-calculate from window geometry
void slideWindow(QWindow *window, SlideFromLocation location, int offset = -1);
```

### Usage Notes
- All functions take `QWindow*` (not WId)
- Region excludes shadow/halo areas
- Empty region = effect on entire window
- Effects are compositor-dependent (KWin supports all)

---

## KWindowSystem Class

Platform-aware window management singleton.

**Header:** `/usr/include/KF6/KWindowSystem/kwindowsystem.h`

### Properties

```cpp
Q_PROPERTY(bool isPlatformWayland READ isPlatformWayland CONSTANT)
Q_PROPERTY(bool isPlatformX11 READ isPlatformX11 CONSTANT)
Q_PROPERTY(bool showingDesktop READ showingDesktop WRITE setShowingDesktop NOTIFY showingDesktopChanged)
```

### Enums

```cpp
enum class Platform { Unknown, X11, Wayland };
```

### Static Methods

```cpp
static KWindowSystem *self();

// Window activation
static void activateWindow(QWindow *window, long time = 0);  // since 5.89

// Desktop showing
static bool showingDesktop();
static void setShowingDesktop(bool showing);

// Parent window (cross-app dialogs)
static void setMainWindow(QWindow *subwindow, WId mainwindow);            // X11
static void setMainWindow(QWindow *subwindow, const QString &mainwindow); // Wayland (XDG Foreign token)

// Platform detection
static Platform platform();
static bool isPlatformX11();
static bool isPlatformWayland();

// Startup/activation token
static void updateStartupId(QWindow *window);
static void setCurrentXdgActivationToken(const QString &token);
```

### Signals

```cpp
void showingDesktopChanged(bool showing);
```

---

## KWaylandExtras Class

Wayland-specific extensions.

**Header:** `/usr/include/KF6/KWindowSystem/kwaylandextras.h`

### Static Methods

```cpp
static KWaylandExtras *self();

// Input serial for event handling
static quint32 lastInputSerial(QWindow *window);

// XDG Foreign (window parent relationship)
static void exportWindow(QWindow *window);
static void unexportWindow(QWindow *window);

// XDG Activation token (async, since 6.19)
static QFuture<QString> xdgActivationToken(QWindow *window, uint32_t serial, const QString &appId);
static QFuture<QString> xdgActivationToken(QWindow *window, const QString &appId);

// XDG Toplevel properties (since 6.22)
static void setXdgToplevelTag(QWindow *window, const QString &tag);
static void setXdgToplevelDescription(QWindow *window, const QString &description);
```

### Signals

```cpp
void windowExported(QWindow *window, const QString &handle);
```

---

## Wayland vs X11 Differences

| Feature | Wayland | X11 |
|---------|---------|-----|
| Window activation | XDG Activation protocol | X server time-based |
| Parent window | XDG Foreign handle (string) | Window ID (WId) |
| Effects (blur, etc.) | Compositor-dependent | EWMH atoms |
| Activation tokens | QFuture async | N/A |

---

## Dock-Relevant Patterns

### Blur Behind Dock

```cpp
// Enable blur for the entire dock surface
KWindowEffects::enableBlurBehind(dockWindow, true);
```

### Slide Animation

```cpp
// Dock slides from bottom edge
KWindowEffects::slideWindow(dockWindow, KWindowEffects::BottomEdge);
```

---

## KWin Minimize Animations (Magic Lamp / Squash) — icon geometry

KWin's built-in Magic Lamp and Squash effects animate a window into its taskbar/dock icon
when minimized, and out of it on restore. They only work if the dock **publishes each
icon's on-screen rectangle** to KWin via the Task Manager protocol:

```cpp
// Screen (global) coordinates; delegate = the visual QQuickItem (optional)
tasksModel->requestPublishDelegateGeometry(index, globalRect, delegate);
```

This maps to `org_kde_plasma_window.set_minimized_geometry` on Wayland. Rules verified
from KWin source (`src/plugins/magiclamp`):

- KWin finds the panel by scanning for a layer-shell window of type Dock (`isDock()`),
  whose frame geometry **intersects** the published rect. Krema's layer-shell scope
  `"dock"` maps to `WindowType::Dock`, so it qualifies. **Scope must be exactly
  `"dock"`** — KWin's `scopeToType()` falls back to `WindowType::Normal` for unknown
  scopes, which breaks type-filtered effects (verified: Slide Back never fired with
  scope `"krema-dock"` because the full-width Normal surface permanently topped the
  usable-window stacking list).
- Rect must be **global** (use `QQuickItem::mapToGlobal`), not scene-local.
- Launchers/startup tasks have no window — skip publishing for them (Plasma does the same).
- Publish when the icon moves/zooms on layout settle (Plasma uses a 500 ms timer) and
  before clicking so the lamp targets the current (zoomed) icon.
- Empty rects are ignored by KWin.

## KWin "Slide Back" effect (window rises over the covering window)

KWin ships a built-in effect `slideback` (System Settings → Desktop Effects → category
**Focus**, name **"Slide Back"**, disabled by default via `kwinrc` `[Plugins]
slidebackEnabled`). When a window is raised (`activateWindow` → `raiseWindow` →
`stackingOrderChanged`), covering windows are **slid aside** by a spring motion and slide
back when another window is raised — the "window rises in front of the maximized one"
feel. It fires on ANY raise: taskbar clicks, titlebar clicks, and **dock icon clicks**
(the dock's `requestActivate` goes through the same raise path). No dock-side code needed;
users enable it in KDE System Settings.

**Critical dock-side requirements (verified via KWin scripting dump):**

- The dock surface's layer-shell scope must be exactly `"dock"` so KWin classifies it
  as `WindowType::Dock`. Slide Back only tracks "usable" windows (normal/dialog); a
  Normal-typed dock surface permanently tops that list, so raises are never detected.
- Any auxiliary overlay surfaces (preview popups) must not stay mapped full-size.
  Map them only while visible, and use a scope KWin excludes from usable windows
  (e.g. `"tooltip"`).

### Background Contrast

```cpp
// Translucent dock with readable text
KWindowEffects::enableBackgroundContrast(dockWindow, true, 0.5, 0.8, 1.2);
```
