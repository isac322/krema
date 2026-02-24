# Keyboard Navigation

## Features
- dock-keyboard-entry: Meta+F5 global shortcut to focus the dock
- dock-keyboard-nav: Left/Right arrow keys to move between dock items
- dock-keyboard-activate: Enter/Space to activate focused app
- dock-keyboard-new-instance: Shift+Enter to launch new instance
- preview-keyboard-entry: Down arrow to enter preview popup keyboard mode
- preview-keyboard-nav: Left/Right arrows to move between preview thumbnails
- preview-keyboard-activate: Enter to activate focused thumbnail window
- preview-keyboard-close: Delete to close focused thumbnail window
- keyboard-escape: Escape to exit keyboard navigation

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
- src/app/application.cpp

---

## TC KBD-001: Dock Focus Entry via Meta+F5

**Precondition:** Dock with at least 1 running app (e.g., kcalc). No keyboard focus on dock.
**Steps:**
1. Trigger focus-dock via D-Bus:
   ```
   dbus_call service="org.kde.kglobalaccel" path="/component/krema"
     interface="org.kde.kglobalaccel.Component" method="invokeShortcut"
     args=["string:focus-dock"]
   ```
   Note: `keyboard_key "super+F5"` does NOT work in kwin-mcp — KGlobalAccel
   requires D-Bus invocation in the isolated session.
2. Wait 500ms for Wayland async round-trip
3. `accessibility_tree app_name="krema"`

**Expected:**
- `[tool bar] "Krema Dock"` has `focused` state
- First dock item `[button]` has both `focusable` and `focused` states
- Dock items have `showing` and `visible` states (dock slid in)
- Parabolic zoom applied: focused item has larger bounding box than others
- `screenshot` shows blue focus ring on first item

**Verification:** accessibility_tree (button `focused` state), screenshot (focus ring visible)

---

## TC KBD-002: Arrow Key Navigation Between Items

**Precondition:** KBD-001 completed (dock in keyboard mode, first item focused).
**Steps:**
1. `keyboard_key Right`
2. `accessibility_tree app_name="krema"` — check which button has `focused`
3. `keyboard_key Right` (one more)
4. `accessibility_tree app_name="krema"`
5. `keyboard_key Left`
6. `accessibility_tree app_name="krema"`

**Expected:**
- After step 1: second button has `focused`, first does not
- After step 3: third button has `focused`
- After step 5: second button regains `focused`
- Parabolic zoom center shifts with focus (focused item has largest bounding box)

**Verification:** accessibility_tree (`focused` state shifts between buttons)

---

## TC KBD-003: Down Arrow Opens Preview + Thumbnail Focus

**Precondition:** KBD-001 completed. Navigate to item with open windows (e.g., KCalc).
**Steps:**
1. Navigate to the running app's item using `keyboard_key Right` (repeat as needed)
2. Verify target button has `focused` state
3. `keyboard_key Down`
4. Wait 500ms for preview popup
5. `accessibility_tree app_name="krema"`

**Expected:**
- New `[popup menu]` appears with name `"Preview for <AppName>"` (e.g., `"Preview for KCalc"`)
- Popup has `showing` and `visible` states
- Inside popup: `[label]` with app name, `[separator]`, `[button]` per window thumbnail
- First thumbnail `[button]` has `focused` state
- Thumbnail button contains: `[button] "Close <Title>"` and `[label] "<Title>"`
- `screenshot` shows preview popup with live PipeWire thumbnail

**Verification:** accessibility_tree (popup name format, thumbnail `focused`), screenshot

---

## TC KBD-004: Enter Activates Focused Thumbnail Window

**Precondition:** KBD-003 completed (preview open, thumbnail focused).
**Steps:**
1. Note focused thumbnail's window title from accessibility_tree
2. `keyboard_key Return`
3. Wait 500ms
4. `accessibility_tree app_name="krema"`
5. `list_windows`

**Expected:**
- Keyboard navigation mode ends: no dock button has `focused` state
- Preview popup reverts to 0x0 size with no `showing`/`visible` states
  (popup element remains in tree but hidden — check for absence of `showing`)
- Dock slides out (items at y > screen height, no `showing` on buttons)
- Target window is active in `list_windows`

**Verification:** accessibility_tree (no `focused` buttons, popup not `showing`), list_windows

---

## TC KBD-005: Escape Exits Keyboard Navigation

**Precondition:** Re-enter keyboard mode via D-Bus invokeShortcut (KBD-001 steps).
**Steps:**
1. Verify a dock button has `focused` state
2. `keyboard_key Escape`
3. Wait 500ms
4. `accessibility_tree app_name="krema"`

**Expected:**
- No dock `[button]` has `focused` state
- Note: `[tool bar]` may retain `focused` (QML activeFocus residue) — this is OK
- Dock may auto-hide depending on visibility mode (buttons lose `showing`/`visible`)

**Verification:** accessibility_tree (no button-level `focused`)

---

## TC KBD-006: Preview Thumbnail Navigation (Left/Right)

**Precondition:** App with 2+ windows open. Enter keyboard mode → focus that app → Down to open preview.
**Steps:**
1. `keyboard_key Right` — move to second thumbnail
2. `accessibility_tree app_name="krema"` — verify second thumbnail button has `focused`
3. `keyboard_key Left` — back to first thumbnail
4. `accessibility_tree app_name="krema"` — verify first thumbnail button has `focused`

**Expected:**
- `focused` state moves between thumbnail `[button]` elements inside `[popup menu]`
- Focus ring visible on the focused thumbnail in screenshot

**Verification:** accessibility_tree (`focused` on correct thumbnail button)

---

## TC KBD-007: Delete Closes Preview Thumbnail

**Precondition:** Preview open with keyboard focus on a thumbnail (KBD-003 or KBD-006).
**Steps:**
1. Note window title of focused thumbnail from accessibility_tree
2. `keyboard_key Delete`
3. Wait 500ms
4. `accessibility_tree app_name="krema"`
5. `list_windows`

**Expected:**
- Window is closed (not in `list_windows`)
- Thumbnail `[button]` removed from popup menu
- If windows remain: focus shifts to adjacent thumbnail
- If no windows remain: popup closes (0x0, no `showing`), keyboard returns to dock buttons

**Verification:** accessibility_tree (thumbnail removed), list_windows (window gone)

---

## TC KBD-008: Mouse Movement Cancels Keyboard Mode

**Precondition:** Dock in keyboard navigation mode (KBD-001 completed, button has `focused`).
**Steps:**
1. Verify a dock button has `focused` state
2. `mouse_move x=400 y=300` (center of screen)
3. Wait 500ms
4. `accessibility_tree app_name="krema"`

**Expected:**
- No dock `[button]` has `focused` state
- Keyboard mode cancelled — normal mouse hover behavior resumes

**Verification:** accessibility_tree (no button `focused`)

---

## TC KBD-009: Keyboard Mode Prevents Dock Auto-Hide

**Precondition:** Dock visibility set to AutoHide, DodgeWindows, or SmartHide. A window overlapping dock area.
**Steps:**
1. Verify dock is hidden: dock buttons lack `showing`/`visible` states or have offscreen coordinates
2. Trigger focus-dock via D-Bus invokeShortcut
3. Wait 500ms
4. `accessibility_tree app_name="krema"` — dock buttons should have `showing`, `visible`
5. `screenshot` — dock visible on screen
6. Verify first button has `focused` state
7. Wait 3+ seconds (exceeds normal auto-hide timeout)
8. `accessibility_tree` — dock still has `showing`/`visible` buttons

**Expected:**
- `setKeyboardActive(true)` overrides visibility mode → dock stays visible
- First button has `focused` state
- After `keyboard_key Escape` + `mouse_move` away: dock resumes auto-hide

**Verification:** accessibility_tree (persistent `showing`+`focused` during keyboard mode)
