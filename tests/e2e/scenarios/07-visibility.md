# Visibility Control

## Features
- vis-always-visible: Dock always shown regardless of windows
- vis-auto-hide: Dock hides after timeout, shows on mouse approach
- vis-dodge-windows: Dock hides when windows overlap its area
- vis-smart-hide: Dock hides when active window overlaps its area
- vis-keyboard-lock: Keyboard navigation prevents auto-hide
- vis-screen-edge-trigger: Mouse at screen edge triggers dock show

## Affected Files
- src/shell/dockvisibilitycontroller.h
- src/shell/dockvisibilitycontroller.cpp
- src/platform/waylanddockplatform.h
- src/platform/waylanddockplatform.cpp
- src/shell/dockview.h
- src/shell/dockview.cpp
- src/qml/main.qml
- src/config/krema.kcfg

---

## TC VIS-001: Always Visible Mode

**Precondition:** Visibility mode set to AlwaysVisible.
**Steps:**
1. `screenshot` — verify dock visible
2. Maximize a window (e.g., kcalc)
3. Wait 500ms
4. `screenshot` — verify dock still visible
5. Move mouse to center of screen
6. Wait 2000ms
7. `screenshot` — verify dock still visible

**Expected:**
- Dock remains visible at all times
- Windows do not cover the dock (layer-shell exclusive zone)

**Verification:** screenshot (dock visible in all states)

---

## TC VIS-002: Auto-Hide Mode — Hide on Timeout

**Precondition:** Visibility mode set to AutoHide. Mouse NOT on dock.
**Steps:**
1. `mouse_move` to dock area to show it
2. Wait 300ms — dock should be visible
3. `screenshot` — verify visible
4. `mouse_move` to center of screen (away from dock)
5. Wait for hide timeout (typically 1-2 seconds)
6. `screenshot` — verify dock hidden (slid off screen)

**Expected:**
- Dock hides with slide animation after timeout
- Dock area is reclaimed (windows can use full screen)

**Verification:** screenshot (dock hidden)

---

## TC VIS-003: Auto-Hide Mode — Show on Edge Approach

**Precondition:** VIS-002 completed (dock hidden in AutoHide mode).
**Limitation:** Screen edge trigger does NOT work in kwin-mcp (EIS input does not
reach layer-shell trigger strip). Use D-Bus workaround.
**Steps:**
1. `dbus_call invokeShortcut("focus-dock")` — force show dock
2. Wait 500ms
3. `mouse_move` to dock area (screen coordinates) — switch to mouse mode
4. `screenshot` — verify dock slides back in

**Expected:**
- Dock shows with slide-in animation
- Dock remains visible while mouse is in dock area (setHovered triggers)

**Note:** This TC tests the workaround path only. True edge-trigger behavior
cannot be verified in kwin-mcp due to D-08.

**Verification:** screenshot (dock visible after D-Bus show)

---

## TC VIS-004: Dodge Windows Mode

**Precondition:** Visibility mode set to DodgeWindows. One app window open but not overlapping dock.
**Steps:**
1. `screenshot` — verify dock visible (no overlap)
2. Move/resize window to overlap dock area
3. Wait 500ms
4. `screenshot` — verify dock hidden
5. Move window away from dock area
6. Wait 500ms
7. `screenshot` — verify dock visible again

**Expected:**
- Dock hides when ANY window overlaps its area
- Dock shows when no windows overlap

**Verification:** screenshot (hide/show based on window overlap)

---

## TC VIS-005: Smart Hide Mode

**Precondition:** Visibility mode set to SmartHide. Two app windows open.
**Steps:**
1. Activate window that does NOT overlap dock
2. Wait 500ms
3. `screenshot` — verify dock visible
4. Activate window that DOES overlap dock area
5. Wait 500ms
6. `screenshot` — verify dock hidden

**Expected:**
- Dock hides only when the ACTIVE window overlaps its area
- Inactive windows overlapping dock do not trigger hide

**Verification:** screenshot (hide only for active overlap)

---

## TC VIS-006: Keyboard Navigation Locks Visibility

**Precondition:** Visibility mode set to AutoHide. Dock currently hidden.
**Steps:**
1. Trigger Meta+F5 (keyboard navigation entry)
2. Wait 500ms
3. `screenshot` — verify dock is visible
4. Wait 5 seconds (longer than auto-hide timeout)
5. `screenshot` — verify dock STILL visible (keyboard lock active)
6. `keyboard_key Escape` (exit keyboard nav)
7. `mouse_move` away from dock
8. Wait for hide timeout
9. `screenshot` — verify dock hides after keyboard nav ends

**Expected:**
- Dock forced visible during keyboard navigation regardless of visibility mode
- After keyboard nav ends, normal auto-hide behavior resumes

**Verification:** screenshot (visible during keyboard, hidden after escape + timeout)
