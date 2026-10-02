# Keyboard Navigation

## Features
- dock-keyboard-entry: Meta+Alt+D global shortcut to focus the dock
- dock-keyboard-nav: Left/Right arrow keys to move between dock items
- dock-keyboard-activate: Enter/Space to activate focused app
- dock-keyboard-new-instance: Shift+Enter to launch new instance
- preview-keyboard-entry: Down arrow to enter preview popup keyboard mode
- preview-keyboard-nav: Left/Right arrows to move between preview thumbnails
- preview-keyboard-activate: Enter to activate focused thumbnail window
- preview-keyboard-close: Delete to close focused thumbnail window
- keyboard-escape: Escape to exit keyboard navigation
- dock-task-zones: Keyboard focus crosses the pinned and unpinned task zones without focusing the separator

## Affected Files
- src/qml/main.qml
- src/qml/DockItem.qml
- src/qml/PreviewPopup.qml
- src/qml/PreviewThumbnail.qml
- src/shell/previewcontroller.h
- src/shell/previewcontroller.cpp
- src/shell/dockshell.h
- src/shell/dockshell.cpp
- src/shell/dockvisibilitycontroller.h
- src/shell/dockvisibilitycontroller.cpp
- src/platform/waylanddockplatform.h
- src/platform/waylanddockplatform.cpp
- src/platform/kwinpointermotionwatcher.h
- src/platform/kwinpointermotionwatcher.cpp
- src/app/application.cpp
- src/config/krema.kcfg
- src/models/dockactions.h
- src/models/dockactions.cpp
- src/models/dockmodel.h
- src/models/dockmodel.cpp

**Tier:** Tier 2 (Appium), with Tier 1 QML coverage for shared `main.qml`
keyboard behavior.

**Click-policy scope:** The new left-click choices do not change Enter/Space,
Meta+N, preview keyboard navigation, Escape, or visibility locking. The
existing keyboard assertions remain the consumer contract for those paths.

---

## TC KBD-001: Dock Focus Entry via Meta+Alt+D

**Precondition:** Dock with at least 1 running app (e.g., kcalc). No keyboard focus on dock.
**Steps:**
1. Press Meta+Alt+D (or invoke `focus-dock` over D-Bus, see README "Focus Dock shortcut")
2. Wait 500ms for Wayland async round-trip
3. Check the krema AT-SPI tree

**Expected:**
- `[tool bar] "Krema Dock"` has `focused` state
- First dock item `[button]` has both `focusable` and `focused` states
- Dock items have `showing` and `visible` states (dock slid in)
- Parabolic zoom applied: focused item has a larger bounding box than others, and neighbouring items move aside per the configured zoom style
- Screenshot shows blue focus ring on first item

**Verification:** AT-SPI (button `focused` state), screenshot (focus ring visible)

**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd001_meta_alt_d_focuses_first_dock_item (real key press; also asserts kglobalaccel keeps Meta+Alt+D bound to focus-dock)
**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd001_focus_dock_shortcut_focuses_first_dock_item

---

## TC KBD-002: Arrow Key Navigation Between Items

**Precondition:** KBD-001 completed (dock in keyboard mode, first item focused).
**Steps:**
1. Press Right
2. Check the AT-SPI tree — which button has `focused`
3. Press Right (one more)
4. Check the AT-SPI tree
5. Press Left
6. Check the AT-SPI tree

**Expected:**
- After step 1: second button has `focused`, first does not
- After step 3: third button has `focused`
- After step 5: second button regains `focused`
- Parabolic zoom glides with focus: focused item has the largest bounding box, neighbours on its sides move apart per the configured zoom style, and the dock background widens to contain them

**Verification:** AT-SPI (`focused` state shifts between buttons)

**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd002_arrow_keys_move_focus_between_items

---

## TC KBD-003: Down Arrow Opens Preview + Thumbnail Focus

**Precondition:** KBD-001 completed. Navigate to item with open windows (e.g., KCalc).
**Steps:**
1. Navigate to the running app's item with Right (repeat as needed)
2. Verify target button has `focused` state
3. Press Down
4. Wait 500ms for preview popup
5. Check the AT-SPI tree

**Expected:**
- New `[popup menu]` appears with name `"Preview for <AppName>"` (e.g., `"Preview for KCalc"`)
- Popup has `showing` and `visible` states
- Inside popup: `[label]` with app name, `[separator]`, `[button]` per window thumbnail
- First thumbnail `[button]` has `focused` state
- Thumbnail button contains: `[button] "Close <Title>"` and `[label] "<Title>"`
- Screenshot shows preview popup with live PipeWire thumbnail

**Verification:** AT-SPI (popup name format, thumbnail `focused`), screenshot

**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd003_down_opens_preview_with_first_thumbnail_focused

---

## TC KBD-004: Enter Activates Focused Thumbnail Window

**Precondition:** KBD-003 completed (preview open, thumbnail focused).
**Steps:**
1. Note focused thumbnail's window title from the AT-SPI tree
2. Press Return
3. Wait 500ms
4. Check the AT-SPI tree and the window list

**Expected:**
- Keyboard navigation mode ends: no dock button has `focused` state
- Preview popup reverts to 0x0 size with no `showing`/`visible` states
  (popup element remains in tree but hidden — check for absence of `showing`)
- Dock slides out (items at y > screen height, no `showing` on buttons)
- Target window is the active window

**Verification:** AT-SPI (no `focused` buttons, popup not `showing`), active window

**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd004_enter_activates_focused_thumbnail_window

---

## TC KBD-005: Escape Exits Keyboard Navigation

**Precondition:** Re-enter keyboard mode via Focus Dock (KBD-001 steps).
**Steps:**
1. Verify a dock button has `focused` state
2. Press Escape
3. Wait 500ms
4. Check the AT-SPI tree

**Expected:**
- No dock `[button]` has `focused` state
- Note: `[tool bar]` may retain `focused` (QML activeFocus residue) — this is OK
- Dock may auto-hide depending on visibility mode (buttons lose `showing`/`visible`)

**Verification:** AT-SPI (no button-level `focused`)

**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd005_escape_exits_keyboard_navigation

---

## TC KBD-006: Preview Thumbnail Navigation (Left/Right)

**Precondition:** App with 2+ windows open. Enter keyboard mode → focus that app → Down to open preview.
**Steps:**
1. Press Right — move to second thumbnail
2. Check the AT-SPI tree — second thumbnail button has `focused`
3. Press Left — back to first thumbnail
4. Check the AT-SPI tree — first thumbnail button has `focused`

**Expected:**
- `focused` state moves between thumbnail `[button]` elements inside `[popup menu]`
- Focus ring visible on the focused thumbnail in screenshot

**Verification:** AT-SPI (`focused` on correct thumbnail button)

**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd006_left_right_move_between_thumbnails

---

## TC KBD-007: Delete Closes Preview Thumbnail

**Precondition:** Preview open with keyboard focus on a thumbnail (KBD-003 or KBD-006).
**Steps:**
1. Note window title of focused thumbnail from the AT-SPI tree
2. Press Delete
3. Wait 500ms
4. Check the AT-SPI tree and the window list

**Expected:**
- Window is closed (no longer in the window list)
- Thumbnail `[button]` removed from popup menu
- If windows remain: focus shifts to adjacent thumbnail
- If no windows remain: popup closes (0x0, no `showing`), keyboard returns to dock buttons

**Verification:** AT-SPI (thumbnail removed), window list (window gone)

**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd007_delete_closes_focused_thumbnail_window[pointer-parked]
**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd007_delete_closes_focused_thumbnail_window[pointer-at-centre]

---

## TC KBD-008: Mouse Movement Cancels Keyboard Mode

**Precondition:** Dock in keyboard navigation mode (KBD-001 completed, button has `focused`).
**Steps:**
1. Verify a dock button has `focused` state
2. Move the pointer to the center of the screen
3. Wait 500ms
4. Check the AT-SPI tree

**Expected:**
- No dock `[button]` has `focused` state
- Keyboard mode cancelled — normal mouse hover behavior resumes

**Verification:** AT-SPI (no button `focused`)

**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd008_mouse_movement_cancels_keyboard_mode
**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd008_mouse_movement_over_dock_cancels_keyboard_mode

---

## TC KBD-009: Keyboard Mode Prevents Dock Auto-Hide

**Precondition:** Dock visibility set to Auto hide, or Dodge windows (with or without "Only dodge active window"). A window overlapping dock area.
**Steps:**
1. Verify dock is hidden: dock buttons lack `showing`/`visible` states or have offscreen coordinates
2. Trigger Focus Dock (Meta+Alt+D)
3. Wait 500ms
4. Check the AT-SPI tree — dock buttons should have `showing`, `visible`
5. Screenshot — dock visible on screen
6. Verify first button has `focused` state
7. Wait 3+ seconds (exceeds normal auto-hide timeout)
8. Check the AT-SPI tree — dock still has `showing`/`visible` buttons

**Expected:**
- `setKeyboardActive(true)` overrides visibility mode → dock stays visible
- First button has `focused` state
- After Escape + moving the pointer away: dock resumes auto-hide

**Verification:** AT-SPI (persistent `showing`+`focused` during keyboard mode)

**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd009_keyboard_mode_keeps_hidden_dock_visible[autohide|dodge|smarthide] (SmartHide = VisibilityMode=2 + DodgeActiveOnly=true)
**Automated:** tests/appium/test_01_keyboard_nav.py::test_kbd009_dock_auto_hides_again_after_escape[autohide|dodge|smarthide] (Escape returns focus to the previously active window, so SmartHide sees it again)

---

## TC KBD-010: Keyboard Traversal Crosses Task Zones

**Precondition:** Enable `Separate pinned and running apps`. Provide at least two pinned apps (including one running app) and two unpinned running apps. The dock is visible and no dock item currently has keyboard focus.

**Steps:**
1. Invoke the existing focus-dock shortcut and record the dock item names in visual order from `accessibility_tree`.
2. Press `keyboard_key Right` until focus reaches the last pinned item, then press `keyboard_key Right` once more.
3. Inspect the accessibility tree while focus crosses the boundary.
4. Press `keyboard_key Left` to return to the last pinned item, then continue left to the first item.
5. Repeat the traversal once after one pinned app is launched and once after an unpinned app is closed.

**Expected:**
- Each app appears in exactly one focusable dock button; running pinned apps remain in the pinned zone.
- Right and Left move directly between adjacent task buttons across the zone boundary.
- With both zones non-empty, the visual separator is visible but not focusable, selectable, or an extra keyboard stop.
- Adding or removing a task updates the traversal order without duplicate buttons or skipped tasks.

**Verification:** AT-SPI `focused` state and ordered button names before and after the lifecycle changes; the separator has no `focusable` state.
**Automated (native Tier 2; rerunnable 4-edge coverage):** `tests/appium/test_10_task_zone_input.py::test_kbd010_task_navigation_survives_native_launch_and_close` covers native launch/close lifecycle and the task-boundary focus sequence on all four edges. The divider never receives focus or selection; F12 remains a delivery probe, not launch proof. KWin RPC and painted-divider quality are not claimed.
