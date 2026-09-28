# Mouse Interaction

## Features
- mouse-click-activate: Left-click on dock item activates/launches app; on a grouped app it cycles through the app's windows
- mouse-new-instance: Middle-click launches new instance
- mouse-hover-zoom: Parabolic zoom on mouse hover; magnified icons push neighbours aside, the dock background grows, and in the middle of the dock the background edges and far icons stay still
- mouse-hover-tooltip: Tooltip shows app name on hover
- mouse-wheel-cycle: Scroll wheel cycles windows of grouped app; it never launches a pinned app that isn't running
- mouse-drag-reorder: Drag to reorder dock items
- mouse-indicator-dots: Running app indicator dots

## Affected Files
- src/qml/main.qml
- src/qml/DockItem.qml
- src/utils/zoomcalculator.h
- src/config/krema.kcfg
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
**Automated:** tests/appium/test_02_mouse.py::test_mouse001_left_click_activates_and_unminimizes_running_app, tests/appium/test_02_mouse.py::test_mouse001_click_without_motion_on_an_item_that_appeared_under_the_pointer

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
**Automated:** tests/appium/test_02_mouse.py::test_mouse002_left_click_launches_pinned_app (launch bounce: tests/appium/test_02_mouse.py::test_mouse002_pinned_launch_bounces, strict xfail for a krema bug)

---

## TC MOUSE-003: Parabolic Zoom on Hover

**Precondition:** Dock visible with multiple items. `ZoomStyle=0` (Parabolic, the default).
**Steps:**
1. `screenshot` — capture baseline dock state
2. `accessibility_tree app_name="krema"` — record rest bounding boxes (position + size) of all dock items
3. `mouse_move` to center of a middle dock item
4. Wait 200ms for animation
5. `screenshot` — capture zoomed state
6. `accessibility_tree app_name="krema"` — record zoomed bounding boxes
7. `mouse_move` horizontally across several middle items in small steps, taking a `screenshot` + `accessibility_tree` after each step
8. `mouse_move` toward one dock end in small steps, then `mouse_move` away from the dock

**Expected:**
- Hovered item is visually larger (zoomed) and stays under the pointer
- Adjacent items have intermediate zoom; items far from the pointer remain at base size
- Neighbouring items are pushed aside along the dock axis; the gaps between icons stay constant (equal to the rest spacing), so magnified icons never overlap
- The dock background grows to contain the magnified icons
- While the pointer sweeps across the middle of the dock, both background edges and the far icons stay still (no shaking or back-and-forth)
- Toward a dock end, the background grows smoothly toward that end in one direction only
- All items and the background return to the rest layout when the pointer leaves the dock

**Verification:** screenshot comparison (zoomed vs baseline), accessibility_tree (item bounding boxes: shifted positions, grown sizes, no overlap)
**Automated:** tests/appium/test_02_mouse.py::test_mouse003_parabolic_zoom_on_hover

---

## TC MOUSE-004: Tooltip on Hover

**Precondition:** Dock visible, no keyboard navigation active. Target a pinned-only
(not running) app to get text tooltip instead of preview popup.
**Steps:**
1. `mouse_move` to a pinned-only dock item (screen coordinates)
2. Wait 800ms (tooltip delay)
3. `screenshot` — verify tooltip visible

**Expected:**
- Tooltip appears above/below the dock item with app name (horizontal docks)
- Left/right docks: tooltip appears beside the panel and is fully visible, not cut
  off at the dock surface edge; very long names end with an ellipsis
- Tooltip is NOT in AT-SPI (Accessible.ignored: true by design)
- Only screenshot verification possible

**Verification:** screenshot only (tooltip text matches app name)
**Note:** Compute target coordinates from the rest layout before hovering. With
zoom active the item under the pointer comes from its visual position, not its
rest position, and neighbouring items have shifted outward — never reuse a
shifted item's zoomed bounding box as a target.
**Automated:** tests/appium/test_02_mouse.py::test_mouse004_tooltip_shows_app_name_on_hover

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
- Scrolling over a pinned app that is NOT running does nothing (it does not launch the app)

**Limitation:** `list_windows` does not show active/focused window (kwin-mcp D-01).
Cannot programmatically verify which window is active. Use screenshot comparison.

**Verification:** screenshot comparison (different window in foreground after scroll)
**Automated:** tests/appium/test_02_mouse.py::test_mouse005_scroll_wheel_cycles_grouped_windows

**Automated:** `tests/kwin/test_grouped_activation.cpp` (ctest `krema_grouped_activation_tests`) covers the no-launch case

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
**Automated:** tests/appium/test_02_mouse.py::test_mouse006_middle_click_launches_new_instance (bounce until the new window maps: tests/appium/test_02_mouse.py::test_mouse006_launch_bounce_lasts_until_the_new_window_maps, strict xfail for a krema bug)

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

**Automated:** tests/appium/test_02_mouse.py::test_mouse007_indicator_dots_reflect_running_state

---

## TC MOUSE-008: Left-Click Cycles Windows of a Grouped App

**Precondition:** App with 2+ open windows (e.g., 2 kcalc instances via middle-click).
**Steps:**
1. Click one of the grouped app's windows (call it the last-used window), then click another app's window so the grouped app is not active
2. `mouse_click` on the grouped app's dock item (screen coordinates)
3. Wait 300ms
4. `screenshot` — the last-used window is in the foreground
5. `mouse_click` on the same dock item again
6. Wait 300ms
7. `screenshot` — the app's other window is in the foreground

**Expected:**
- The first click activates the app's most recently used window (scrolling onto the app does the same)
- Each further click activates the app's next window, wrapping around (A→B→A)
- No new instance is launched

**Automated:** `tests/kwin/test_grouped_activation.cpp` (ctest `krema_grouped_activation_tests`)

**Verification:** screenshot comparison (different window in foreground after each click)
**Note:** Click coordinates must come from the rest layout (e.g., the first click
in a sequence). Which item sits under the pointer comes from its visual
position, and adjacent items have shifted outward — never reuse a shifted
neighbour's zoomed position as a click target.

---

## TC MOUSE-009: In-Place Zoom Style

**Precondition:** Dock visible with multiple items.
**Steps:**
1. Set `ZoomStyle=1` in `kremarc` (or choose "In place - icons overlap" in the "Zoom style" combo
   in Appearance settings) and restart krema
2. `accessibility_tree app_name="krema"` — record rest bounding boxes of all dock items
3. `mouse_move` to center of a middle dock item
4. Wait 200ms for animation
5. `screenshot` — capture zoomed state
6. `accessibility_tree app_name="krema"` — record zoomed bounding boxes
7. `mouse_move` away from the dock; restore `ZoomStyle=0`

**Expected:**
- Hovered item and its neighbours grow via the same parabolic zoom curve
- Icons scale in place: bounding-box centres do not move and the dock background does not grow
- Magnified icons may overlap each other
- All icons return to base size when the pointer leaves the dock

**Verification:** screenshot comparison (zoomed vs baseline), accessibility_tree (bounding-box sizes grow while centres stay fixed)
**Automated:** tests/appium/test_02_mouse.py::test_mouse009_in_place_zoom_scales_icons_without_moving_them
