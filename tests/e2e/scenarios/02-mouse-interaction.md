# Mouse Interaction

## Features
- mouse-click-activate: Left-click on dock item activates/launches app
- mouse-new-instance: Middle-click launches new instance
- mouse-hover-zoom: Parabolic zoom on mouse hover
- mouse-hover-tooltip: Tooltip shows app name on hover
- mouse-wheel-cycle: Scroll wheel cycles windows of grouped app
- mouse-drag-reorder: Drag to reorder dock items
- mouse-indicator-dots: Running app indicator dots

## Affected Files
- src/qml/main.qml
- src/qml/DockItem.qml
- src/shell/dockview.h
- src/shell/dockview.cpp
- src/models/dockactions.h
- src/models/dockactions.cpp
- src/models/dockmodel.h
- src/models/dockmodel.cpp

---

## TC MOUSE-001: Left-Click Activates Running App

**Precondition:** Dock visible with a running app (e.g., kcalc running).
Note: If dock is hidden (SmartHide/AutoHide), use the dock show sequence:
```
dbus_call invokeShortcut("focus-dock")   # shows dock, enters keyboard mode
sleep 500ms
mouse_move(icon_x, dock_y)              # cancels keyboard mode, keeps dock visible
sleep 300ms
```
Screen edge trigger does NOT work in kwin-mcp (EIS limitation).
**Steps:**
1. `accessibility_tree app_name="krema"` — find KCalc button surface coordinates
2. Convert to screen coordinates: `screen_y = (600 - surface_height) + surface_y`
3. `mouse_click` on the dock item center (screen coordinates)
4. Wait 300ms
5. `list_windows` — check kcalc is active (note: active window not shown, check screenshot)

**Expected:**
- kcalc window becomes the active/focused window
- If kcalc was minimized, it un-minimizes

**Verification:** screenshot (kcalc in foreground), list_windows (kcalc present)

---

## TC MOUSE-002: Left-Click Launches Pinned App

**Precondition:** Dock has a pinned app that is NOT running.
**Steps:**
1. `find_ui_elements` for the pinned app dock item
2. `mouse_click` on the dock item
3. Wait 2000ms for app launch
4. `list_windows` — check new app window appeared

**Expected:**
- New app instance launches
- Bounce animation plays on the dock item (visual only — verify with screenshot)
- Indicator dot appears under the icon

**Verification:** list_windows (new window), screenshot (indicator dot)

---

## TC MOUSE-003: Parabolic Zoom on Hover

**Precondition:** Dock visible with multiple items.
**Steps:**
1. `screenshot` — capture baseline dock state
2. `mouse_move` to center of a middle dock item
3. Wait 200ms for animation
4. `screenshot` — capture zoomed state

**Expected:**
- Hovered item is visually larger (zoomed)
- Adjacent items have intermediate zoom
- Items further away remain at base size
- Smooth parabolic curve visible

**Verification:** screenshot comparison (zoomed vs baseline)

---

## TC MOUSE-004: Tooltip on Hover

**Precondition:** Dock visible, no keyboard navigation active. Target a pinned-only
(not running) app to get text tooltip instead of preview popup.
**Steps:**
1. `mouse_move` to a pinned-only dock item (screen coordinates)
2. Wait 800ms (tooltip delay)
3. `screenshot` — verify tooltip visible

**Expected:**
- Tooltip appears above/below the dock item with app name
- Tooltip is NOT in AT-SPI (Accessible.ignored: true by design)
- Only screenshot verification possible

**Verification:** screenshot only (tooltip text matches app name)

---

## TC MOUSE-005: Scroll Wheel Cycles Windows

**Precondition:** App with 2+ open windows (e.g., 2 kcalc instances via middle-click).
**Steps:**
1. `mouse_move` to the grouped app's dock item (screen coordinates)
2. `mouse_scroll(x, y, delta=1, discrete=true)` — scroll down
3. Wait 300ms
4. `screenshot` — note which window is in foreground
5. `mouse_scroll(x, y, delta=1, discrete=true)` — scroll down again
6. Wait 300ms
7. `screenshot` — verify different window is in foreground

**Expected:**
- Each scroll switches to the next window of the same app
- Cycling wraps around

**Limitation:** `list_windows` does not show active/focused window (kwin-mcp D-01).
Cannot programmatically verify which window is active. Use screenshot comparison.

**Verification:** screenshot comparison (different window in foreground after scroll)

---

## TC MOUSE-006: Middle-Click Launches New Instance

**Precondition:** App is already running (e.g., kcalc). Dock visible.
**Steps:**
1. `list_windows` — count kcalc entries
2. Show dock and move mouse to kcalc item (see dock show sequence in README)
3. `mouse_click(x, y, button="middle")` on kcalc dock item (screen coordinates)
4. Wait 3000ms (app launch time)
5. `list_windows` — count kcalc entries again

**Expected:**
- One additional kcalc process appears (separate entry in list_windows)
- Bounce animation plays on the dock icon (screenshot verification)

**Verified in PoC:** kcalc went from 1 to 2 separate process entries.

**Verification:** list_windows (kcalc entry count increased by 1)

---

## TC MOUSE-007: Indicator Dots Reflect Running State

**Precondition:** Dock visible with mix of running and pinned-only apps.
**Steps:**
1. `screenshot` — observe indicator dots
2. Launch a pinned-but-not-running app via click
3. Wait 2000ms
4. `screenshot` — verify new indicator dot appeared
5. Close the app (via context menu or window close)
6. Wait 1000ms
7. `screenshot` — verify indicator dot removed

**Expected:**
- Running apps show indicator dot(s) below icon
- Pinned-only apps have no indicator dots
- State updates within 1s of app launch/close

**Verification:** screenshot comparison (dots appear/disappear)
