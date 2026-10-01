# Mouse Interaction

## Features
- mouse-click-activate: Default left-click activates/launches app; grouped default cycles through windows
- mouse-click-policy: Independent single-window and grouped-window left-click choices
- mouse-minimize-state: Active, background, minimized, MRU, and current-child state
- mouse-membership: 1→2→1 membership reselects the current action
- mouse-new-instance: Middle-click launches new instance
- mouse-hover-zoom: Parabolic zoom on mouse hover; magnified icons push neighbours aside, the dock background grows, and in the middle of the dock the background edges and far icons stay still; the unscaled hover transition baseline is configurable from 0 to 1000 ms (default 100 ms, matching normal-speed `Kirigami.Units.shortDuration`), Plasma animation scaling still applies, and 0 ms makes zoom snap instantly
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

**Tier:** Tier 2 (Appium/KWin) for real pointer and window state; Tier 1 QML
tests cover the shared `main.qml` input seam. Tier 3 reuses the Appium cases
against installed packages.

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
**Automated:** tests/appium/test_02_mouse.py::test_mouse002_left_click_launches_pinned_app (launch bounce: tests/appium/test_02_mouse.py::test_mouse002_pinned_launch_bounces)

---

## TC MOUSE-003: Parabolic Zoom on Hover

**Precondition:** Dock visible with multiple items. `ZoomStyle=0` (Parabolic, the default) and `ZoomAnimationDuration=100` (the default unscaled baseline; 100 ms matches normal-speed `Kirigami.Units.shortDuration`).
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
- All items and the background return to the rest layout when the pointer leaves the dock, using the configured hover transition baseline and Plasma animation scaling

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
- Bounce animation plays on the dock icon until the new window maps (screenshot verification); if the app is already active and no new window appears within 5s (single-instance no-op), the bounce stops

**Historical PoC note:** KCalc went from 1 to 2 separate process entries.

**Verification:** list_windows (kcalc entry count increased by 1)
**Automated:** tests/appium/test_02_mouse.py::test_mouse006_middle_click_launches_new_instance (bounce until the new window maps: tests/appium/test_02_mouse.py::test_mouse006_launch_bounce_lasts_until_the_new_window_maps)

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

**Precondition:** Dock visible with multiple items. `ZoomAnimationDuration=100` (the default unscaled baseline; 100 ms matches normal-speed `Kirigami.Units.shortDuration`).
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
- All icons return to base size when the pointer leaves the dock, using the configured hover transition baseline and Plasma animation scaling

**Verification:** screenshot comparison (zoomed vs baseline), accessibility_tree (bounding-box sizes grow while centres stay fixed)
**Automated:** tests/appium/test_02_mouse.py::test_mouse009_in_place_zoom_scales_icons_without_moving_them

---

## TC MOUSE-010: Click policies observe real single and group state

**Precondition:** Run each of the six saved policy pairs:
`single0-group0`, `single0-group1`, `single0-group2`, `single1-group0`,
`single1-group1`, and `single1-group2`. Use real fixture windows and a visible
dock.

**Steps:**
1. Click an active, background, and minimized single window.
2. Open a three-window group and exercise the selected grouped action.
3. Observe KWin active/minimized state and the preview popup after each click.

**Expected:**
- Single0 activates and keeps the active window unminimized.
- Single1 minimizes only an active minimizable single; it activates a
  background or minimized single instead.
- Group0 keeps MRU cycling.
- Group1 opens one explicit popup without activating the group.
- Group2 minimizes only the current active child and follows the current
  child on the next click.

**Verification:** KWin active/minimized state, AT-SPI popup state, and
consumer-visible dock state.
**Automated (Tier 2):** `tests/appium/test_02_mouse.py::test_mouse010_click_policies_observe_single_and_group_window_state`
(six policy cases).

---

## TC MOUSE-011: Membership changes use the current click action

**Precondition:** Single-window action `Minimize active window`, grouped-window
action `Show window previews`, and one running fixture window.

**Steps:**
1. Click the single window and observe its minimized state.
2. Add a second window to form a group, then click the group.
3. Close one child so the task returns to a single window and click again.

**Expected:**
- The 1→2→1 transition immediately uses the current single or grouped
  action.
- The dock does not reuse a cached single/group mode or target.

**Verification:** KWin active/minimized state and AT-SPI preview state.
**Automated (Tier 2):** `tests/appium/test_02_mouse.py::test_mouse011_membership_change_reselects_single_and_group_actions`.

---

## TC MOUSE-012: Group2 restores only the existing MRU child

**Precondition:** Grouped-window action `Minimize active window`; another app
is active; the group has a known MRU child.

**Steps:**
1. Enter the background group with no minimized child and confirm its known
   MRU child becomes active.
2. Minimize every child, click the group, and confirm only the MRU child
   restores.
3. Click the current child to minimize it, then click again to restore only
   one child.

**Expected:**
- The existing MRU child activates or restores.
- Other children keep their prior minimized state.
- A group click does not launch a new window or restore an arbitrary child.

**Verification:** KWin `IsActive` and `IsMinimized` state for every child.
**Automated (Tier 2):** `tests/appium/test_02_mouse.py::test_mouse012_group2_restores_one_mru_child_when_all_children_are_minimized`.

---

## TC MOUSE-013: Explicit preview keeps tooltip text separate

**Precondition:** Grouped-window action `Show window previews`, hover previews
disabled, and one of the existing parameter cases: `window-hover500` or
`launcher-hover0`.

**Steps:**
1. Hover the target and capture the text-tooltip pixels when applicable.
2. Click the grouped item repeatedly while the explicit popup is visible.
3. Re-enter the target while the popup hide delay is pending, then close the
   popup.

**Expected:**
- The explicit popup is shown once and text tooltip glyphs do not overlap it.
- Repeated group clicks keep the same popup.
- After the popup closes, the applicable text tooltip returns without extra
  pointer motion.

**Verification:** ScreenShot2 tooltip glyphs and popup visibility. This visual
  check requires the existing DRM/vgem capture path.
**Automated (Tier 2):** `tests/appium/test_02_mouse.py::test_mouse013_group_preview_clears_tooltip_and_restores_it_after_close`
(two parameter cases).

---

## TC MOUSE-014: Zero-delay launcher tooltip control

**Precondition:** A pinned launcher with `PreviewHoverDelay=0`; no running
window for the launcher.

**Steps:**
1. Move the pointer to the launcher.
2. Capture the dock after the zero-delay hover.

**Expected:** The launcher tooltip still paints with no explicit group popup.

**Verification:** ScreenShot2 tooltip pixels. This visual check requires
DRM/vgem capture.
**Automated (Tier 2):** `tests/appium/test_02_mouse.py::test_mouse014_fast_hover_launcher_tooltip_healthy_control`.

---

## TC MOUSE-015: Defaults preserve activation and MRU cycling

**Precondition:** Omit both new click-action keys so the defaults apply.

**Steps:**
1. Click an active single window.
2. Move focus away from a grouped app, then click the group repeatedly.

**Expected:**
- The active single remains focused and unminimized.
- The grouped app enters its MRU child and cycles A→B→A.

**Verification:** KWin active/minimized state and group child sequence.
**Automated (Tier 2):** `tests/appium/test_02_mouse.py::test_mouse015_unconfigured_defaults_keep_single_active_and_group_cycle_mru`.

---

## TC MOUSE-016: Non-left and keyboard paths ignore click policies

**Precondition:** Run all six policy pairs with a running single and grouped
app.

**Steps:**
1. Use AT-SPI `Press`, keyboard Return and Space, and Meta+N.
2. Open the native context menu with a right click.
3. Exercise the existing middle-click and wheel cases.

**Expected:**
- These paths keep their existing activation, launch, menu, and cycling
  behavior.
- They do not invoke single/group left-click minimize or explicit-preview
  policies.

**Verification:** KWin window state, AT-SPI state, and native menu effect.
**Automated (Tier 2):** `tests/appium/test_02_mouse.py::test_mouse016_nonleft_activation_paths_ignore_mouse_click_policies`
(six policy cases), plus the existing MOUSE-005 and MOUSE-006 six-case
parameterizations.

The dedicated normal-hover pending-deadline and drag-time invalidation seams
are Tier 1 QML checks, not additional Appium fixtures. See the named QML
tests in `tests/appium/README.md`.
