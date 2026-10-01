# Drag and Drop

## Features
- dnd-reorder: Drag dock items to reorder position
- dnd-click-policy-isolation: Drag release preserves window state for all six click-policy pairs
- dnd-pin-on-drop: Dropping an item at a new position pins it when separation is off; separation on keeps the task in its source zone
- dnd-visual-feedback: Visual feedback during drag (placeholder, opacity change)
- dnd-file-drop: Drop file onto app icon to open with that app
- dnd-task-zones: Separation constrains drags to the source zone and preserves free reorder when disabled

## Affected Files
- src/qml/main.qml
- src/config/krema.kcfg
- src/qml/DockItem.qml
- src/models/dockactions.h
- src/models/dockactions.cpp
- src/models/dockmodel.h
- src/models/dockmodel.cpp
- src/shell/dockvisibilitycontroller.h
- src/shell/dockvisibilitycontroller.cpp

**Tier:** Tier 2 (Appium) for real drag input and KWin state. Tier 1 QML
tests cover the shared `main.qml` drag and hit-testing seam. Tier 3 reuses
the Appium cases against installed packages.
Keyboard Escape cancellation, context-menu actions, and middle/right input
remain unchanged; the added contract checks drag release state only.


---

## TC DND-001: Drag Reorder Dock Items

**Precondition:** Dock visible with 3+ items.
**Important:** Krema drag requires press-hold (300ms) then move (10px+).
Use `mouse_button_down` + sleep + `mouse_move` + `mouse_button_up` sequence,
NOT `mouse_drag` (which doesn't support hold delay).
**Steps:**
1. Show dock and note item order via `accessibility_tree`
2. `mouse_move` to first item center (screen coordinates)
3. Wait 300ms
4. `mouse_button_down(x, y)` — press on first item
5. Wait 400ms (hold timer 300ms + buffer)
6. `mouse_move(x + 80, y)` — drag to third item position (>10px threshold)
7. `screenshot` — verify drag ghost and drop indicator visible
8. `mouse_button_up(x + 80, y)` — drop
9. Wait 300ms
10. `accessibility_tree` — verify item order changed

**Expected:**
- During drag: ghost icon follows cursor (opacity 0.8), drop indicator line appears
- After drop: item moved to new position, order persists
- AT-SPI button order reflects new arrangement
- The window that was active before the drag is active again (the dock holds keyboard interactivity only while dragging)

**Historical PoC note:** Dolphin moved from position 1 to position 3, and
AT-SPI reported the reordered buttons.


**Verification:** accessibility_tree (button order changed), screenshot (drag ghost visible during drag)

**Automated:** tests/appium/test_05_drag.py::test_dnd_001_drag_reorders_dock_items

---

## TC DND-002: Reorder Persists After Restart

**Precondition:** DND-001 completed (items reordered).
**Steps:**
1. Note current item order via `screenshot`
2. Restart krema
3. Wait 2000ms for startup
4. `screenshot` — verify order preserved

**Expected:**
- Pin order saved to KConfig
- After restart, dock items appear in the reordered position

**Verification:** screenshot (same order after restart)

**Automated:** tests/appium/test_05_drag.py::test_dnd_002_reorder_persists_after_restart

---

## TC DND-003: Drag Visual Feedback

**Precondition:** Dock with multiple items, visible.
**Steps:**
1. `mouse_button_down` on a dock item (screen coordinates)
2. Wait 400ms (hold timer)
3. `mouse_move` slowly to the right (>10px, step by step)
4. `screenshot` — capture mid-drag state
5. `mouse_button_up` at new position

**Expected:**
- Dragged item: opacity reduced at original position
- Ghost icon: follows cursor at 80% opacity (Image with source icon)
- Drop indicator: 2px wide highlight-colored line at insertion point
- Other items: base scale (zoom disabled during drag)

**Historical PoC note:** Earlier capture showed the ghost icon and reduced
opacity source item.

**Verification:** screenshot (ghost icon + opacity change + drop indicator line)

**Automated:** tests/appium/test_05_drag.py::test_dnd_003_drag_shows_ghost_dimmed_source_and_drop_indicator

---

## TC DND-004: Cancel Drag Returns to Original Position

**Precondition:** Dock with multiple items.
**Steps:**
1. `screenshot` — capture initial order
2. Start dragging an item
3. `keyboard_key Escape` or drag outside dock area
4. Wait 300ms
5. `screenshot` — verify original order restored

**Expected:**
- Item returns to original position
- No reorder occurs
- The window that was active before the drag is active again

**Verification:** screenshot (order unchanged)

**Automated:** tests/appium/test_05_drag.py::test_dnd_004_drag_released_outside_dock_keeps_order (drag outside the dock; unpinned task, since dragging a pinned launcher out of the dock unpins it by design)

**Automated:** tests/appium/test_05_drag.py::test_dnd_004_escape_cancels_drag (Escape while dragging; the dock grabs layer-shell keyboard interactivity for the drag)

---

## TC DND-005: Drag release preserves window state across click policies

**Precondition:** Run the six policy pairs:
`single0-group0`, `single0-group1`, `single0-group2`, `single1-group0`,
`single1-group1`, and `single1-group2`. Use grouped `Alpha`/`Beta`, single
`Solo`, and unrelated `Focus` fixture windows.

**Steps:**
1. Start a real drag from a grouped or single dock item.
2. Release inside the dock, outside the dock, and after leaving and
   re-entering the dock.
3. Compare every fixture window's active/minimized state with the pre-drag
   map, and inspect the popup state.

**Expected:**
- The base matrix has 72 scenarios (six policy pairs × 12 source/release
  cases); the separate held-left/right release-order controls are not included
  in that base count.
- Drag hit testing follows the current zoomed item bounds.
- Release does not invoke a configured minimize, activation, or explicit
  preview action.
- No preview popup opens after a drag release.

**Verification:** KWin active/minimized state for every fixture window,
AT-SPI item geometry, and popup visibility. Visual drag capture requires the
existing DRM/vgem path.
**Automated (Tier 2):** `tests/appium/test_05_drag.py::test_dnd005_release_inside_outside_and_exit_reenter_preserves_window_state`
(six policy pairs × 12 source/release cases = 72 base scenarios, plus 24
held-left/right release-order controls).

---

## TC DND-006: Task-Zone Drag Boundaries

**Precondition:** Populate both zones with at least two pinned tasks and two unpinned running tasks. Run the check once with `Separate pinned and running apps` enabled and once with it disabled.

**Steps:**
1. With separation enabled, drag a pinned task to another position within the pinned zone and release.
2. Drag that pinned task toward the unpinned zone and release beyond the divider.
3. Drag an unpinned running task within its own zone, then drag it toward the pinned zone and release beyond the divider.
4. After each release, inspect the drop indicator, ordered dock buttons, and each app's pin membership.
5. Disable separation and repeat one pinned-to-unpinned and one unpinned-to-pinned drag.

**Expected:**
- With separation enabled, within-zone drags reorder only tasks in the source zone.
- A cross-boundary release clamps to the nearest valid position in the source zone; the drop indicator and any accessible drag announcement identify that clamped position.
- Cross-boundary dragging with separation enabled does not auto-pin or unpin the task, and no duplicate item appears.
- With separation disabled, the existing free reorder behavior permits the cross-zone moves without an artificial boundary.

**Verification:** AT-SPI item order and drag feedback, persisted pin membership, and the post-release task order for both option states.
**Automated (native Tier 2 coverage):** `tests/appium/test_09_task_zones.py::test_tzone005_on_reorders_inside_both_zones_and_clamps_cross_boundary`, `tests/appium/test_09_task_zones.py::test_tzone006_off_allows_real_cross_zone_reorder_without_auto_pin` (both pinned-to-running and running-to-pinned directions; exact nearest-valid source-zone positions and membership asserted; visual drop-indicator/drag ghost/paint remain unverified)
