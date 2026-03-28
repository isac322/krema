# Screen Transition Animations — Other Dock Research

> Research for M8: Follow-Active screen mode animation decisions

## Summary

No existing reference dock "migrates" between screens with animation. The industry convention is **one dock per screen** (independent instances). "Follow active screen" is a Krema-specific UX feature with no direct reference.

---

## Reference Dock Behavior

### Latte Dock (KDE Plasma, discontinued)

**Architecture**: One containment (plasmoid container) per screen.

**Multi-monitor behavior**:
- Each screen always has its own independent dock
- No animated migration — each dock is fully independent
- "Follow active" was NOT a Latte feature

**Source**: Plasma containment architecture — each `Plasma::Containment` is bound to a screen number via `Plasma::Containment::screen()`. When screens are added/removed, the shell reassigns containments.

### macOS Dock

**Architecture**: Primary dock always visible on main display; secondary displays get a "stub" that auto-shows.

**Multi-monitor behavior**:
- Primary dock: always visible on primary display
- Secondary displays: dock auto-appears when cursor approaches the bottom edge, then hides when cursor leaves
- Animation: **fade + slide-up** from below screen edge (~300ms, ~springy easing)
- No "follow active window" mode — follows mouse proximity instead

### GNOME Dash-to-Dock

**Architecture**: One dock per screen (like Plasma panels).

**Multi-monitor behavior**:
- Each screen has an independent dock instance
- No animated migration between screens
- "All monitors" setting creates one dock per screen simultaneously

### GNOME Dash-to-Panel

**Architecture**: Taskbar panel on each screen.

**Multi-monitor behavior**:
- One panel per screen, each panel shows its own set of windows (filtered by screen)
- OR all panels show all windows (shared list)
- No animated migration

### Plank Dock (elementary OS)

**Architecture**: One Plank process per screen (users run multiple instances).

**Multi-monitor behavior**:
- Each screen runs an independent Plank instance
- No migration between screens

### Windows 11 Taskbar

**Architecture**: One taskbar per screen.

**Multi-monitor behavior**:
- Each screen has its own taskbar
- Option to show all taskbars or only primary
- No animated migration — it's always per-screen

---

## Animation Types Considered for Krema Follow-Active

### Option A: Fade (Recommended)

- **Fade out** current active dock (opacity 0, 150ms)
- **Fade in** new active dock (opacity 1, 150ms, after brief overlap or after fade-out completes)
- Total: 150-300ms

**Pros**:
- Screen-geometry-independent (works for any screen arrangement)
- Simple to implement in QML (`NumberAnimation on opacity`)
- Professional feel, matches macOS-style behavior

**Cons**:
- Doesn't communicate directionality

### Option B: Slide (Directional)

- Calculate relative position of source vs. destination screen
- Animate dock sliding horizontally/vertically to new position
- Requires knowing the virtual desktop coordinate relationship between screens

**Pros**:
- Communicates spatial relationship between screens

**Cons**:
- Complex: must handle screens of different sizes, non-adjacent screens, vertical arrangements
- Layer-shell surfaces don't participate in the same coordinate space visually
- Would require a separate "bridge" window to animate across screens
- Impractical without significant engineering effort

### Option C: Instant (No Animation)

- Simply hide old dock, show new dock
- No transition overhead

**Pros**: Simple, predictable

**Cons**: Jarring for users

---

## Recommended Approach for M8

**Use fade animation (Option A)** with:
- Fade-out duration: 150ms (`Easing.InQuad`)
- Fade-in duration: 150ms (`Easing.OutQuad`)
- Debounce: 200ms before initiating switch (prevents flicker on rapid alt-tab)
- Implementation: Each `DockView` QML root has an `opacity` property animated by `NumberAnimation`
- Trigger: `DockManager` signals each view whether it's "active" via a C++ property

### QML Pattern

```qml
// DockWindow.qml root
property bool isActiveScreen: false

Behavior on opacity {
    NumberAnimation {
        duration: 150
        easing.type: isActiveScreen ? Easing.OutQuad : Easing.InQuad
    }
}

// In DockManager.cpp:
// Set isActiveScreen=true on new active view, false on old
// The Behavior handles the animation automatically
```

---

## Krema Follow-Active: Trigger Strategy

For detecting "which screen should be active":

### Option 1: Mouse Cursor Position

```cpp
// Poll on mouseMoveEvent or QTimer
QScreen *s = QGuiApplication::screenAt(QCursor::pos());
```

**Trigger**: Any mouse movement that crosses a screen boundary.

**Problem**: Dock follows cursor even when user is just moving mouse while typing on the other screen.

### Option 2: Active Window's Screen (Recommended)

```cpp
// Watch TasksModel::dataChanged for IsActive role
// When active task changes, read its ScreenGeometry role
// Switch active dock to that screen (with debounce)
QRect windowScreenGeo = activeIdx.data(AbstractTasksModel::ScreenGeometry).toRect();
QScreen *screen = QGuiApplication::screenAt(windowScreenGeo.center());
```

**Trigger**: Alt-Tab or clicking a window on another screen.

**Advantages**:
- Matches user intent (dock follows work, not cursor)
- Pairs well with virtual desktop mode
- `ScreenGeometry` is already tracked by Krema's TasksModel

### Hybrid (Most Intuitive)

Switch on: active window change (primary) OR mouse enters new screen without active window there.

---

## Notes

- All reference docks use independent-per-screen architecture; Krema's follow-active is unique
- Fade animation requires each screen's `DockView` to be instantiated even when inactive (for the fade-in)
- In "Follow Active" mode, inactive docks should still occupy their layer-shell slot (to prevent unintentional window coverage) but use `setExclusiveZone(0)` and be visually transparent
- Consider making "Follow Active" a sub-mode under "Primary Only" for simplicity in M8 (skip for MVP if complex)
