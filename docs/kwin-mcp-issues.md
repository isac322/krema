# kwin-mcp Issues and Limitations for E2E Testing

Issues discovered during Krema E2E PoC testing (Phase 0).
Each entry includes reproduction steps, impact on testing, and suggested workarounds.

## D-01: `list_windows` Does Not Show Active/Focused Window

**Status:** Open
**Priority:** Medium (blocks scroll-cycle verification)

**Symptom:** `list_windows` returns app names and window counts but no indication of which window is active/focused.

```
Current output:
- kcalc (1 windows)
- kcalc (1 windows)

Needed output:
- kcalc (1 windows) [active]
- kcalc (1 windows)
```

**Reproduction:**
1. Launch 2 kcalc instances
2. `mouse_scroll` on dock to cycle between them
3. `list_windows` — cannot determine which kcalc is now active

**Impact:**
- Cannot verify scroll-wheel window cycling (TC MOUSE-005)
- Cannot verify thumbnail click activates correct window (TC PREV-003)
- Cannot verify window activation after dock item click

**Workaround:** Use `accessibility_tree` to check for `active` state on frame elements, but this is unreliable for non-dock windows.

**Suggestion:** Add per-window title and active state to `list_windows`, or add a `get_active_window` tool.

---

## D-02: `find_ui_elements` Cannot Filter by AT-SPI State

**Status:** Open
**Priority:** Low (workaround exists)

**Symptom:** Elements can only be searched by name/role/description text. Cannot search for "focused button" or "visible popup menu".

**Reproduction:**
1. Enter keyboard navigation mode
2. Need to find which button has `focused` state
3. `find_ui_elements query="focused"` — matches text "focused" in names, not state

**Impact:** Keyboard navigation tests require parsing full `accessibility_tree` output to find focused elements.

**Workaround:** Use `accessibility_tree` and parse the states list from the text output.

**Suggestion:** Add `states` filter parameter: `find_ui_elements(role="push button", states=["focused"])`.

---

## D-03: `wait_for_element` Cannot Wait for State Changes

**Status:** Open
**Priority:** Low (workaround exists)

**Symptom:** Can only wait for element appearance in tree. Cannot wait for state transitions (e.g., element gains `focused` state).

**Impact:** Wayland async operations (keyboard interactivity, surface positioning) require fixed-time `sleep` waits instead of condition-based waits.

**Workaround:** Use `sleep` with generous timeouts (500ms-1000ms).

**Suggestion:** Add `expected_states` parameter: `wait_for_element(name="KCalc", expected_states=["focused"])`.

---

## D-04: KGlobalAccel Shortcuts Don't Work via `keyboard_key`

**Status:** Confirmed (EIS limitation)
**Priority:** Medium (blocks all global shortcut tests)

**Symptom:** `keyboard_key("super+F5")` does not trigger KGlobalAccel-registered shortcuts. D-Bus `invokeShortcut` works.

**Reproduction:**
1. `keyboard_key "super+F5"` — no response from dock
2. `dbus_call invokeShortcut("focus-dock")` — dock activates keyboard mode

**Impact:** All global shortcut tests require D-Bus invocation instead of keyboard simulation.

**Workaround:** Use D-Bus `invokeShortcut`:
```
dbus_call service="org.kde.kglobalaccel" path="/component/krema"
  interface="org.kde.kglobalaccel.Component" method="invokeShortcut"
  args=["string:focus-dock"]
```

**Root cause:** EIS (libei) input events bypass KWin's global shortcut processing path. This is likely a fundamental limitation of the EIS input backend.

**Suggestion:** Document this limitation in kwin-mcp README. Consider adding an `invoke_global_shortcut(component, shortcut_name)` convenience tool.

---

## D-05: `accessibility_tree` Cannot Filter by Role

**Status:** Open
**Priority:** Low (workaround exists)

**Symptom:** Can only filter by `app_name` and `max_depth`. Cannot extract elements of a specific role (e.g., "all push buttons").

**Impact:** Complex apps produce large trees that need text parsing to find elements by role.

**Workaround:** Use `find_ui_elements` with role-based search terms, or parse `accessibility_tree` output.

**Suggestion:** Add `role` filter: `accessibility_tree(app_name="krema", role="push button")`.

---

## D-06: QMenu (Native Context Menu) Not Exposed in AT-SPI

**Status:** Confirmed
**Priority:** Medium (blocks context menu AT-SPI testing)

**Symptom:** Qt's native `QMenu` (used for dock right-click context menu) does not appear in `accessibility_tree` or `find_ui_elements` within the kwin-mcp isolated session.

**Reproduction:**
1. Right-click on dock item → context menu appears (visible in screenshot)
2. `accessibility_tree` — no menu/popup elements found
3. `find_ui_elements query="Pin to Dock"` — no results

**Impact:** Context menu items cannot be located or clicked via AT-SPI. Must use screenshot-based coordinate estimation.

**Workaround:** Use screenshot to identify menu item positions, then click at estimated coordinates. Menu layout is deterministic (items in fixed order), so coordinates can be calculated from the menu's top-left position.

**Root cause:** Likely AT-SPI bus issue in isolated D-Bus session — `QMenu` creates a separate top-level window that may not register with the session's AT-SPI bus.

---

## D-07: AT-SPI Bus Instability After Heavy Usage

**Status:** Intermittent
**Priority:** High (blocks all AT-SPI operations)

**Symptom:** After multiple tool calls (especially full `accessibility_tree` without `app_name` filter), the AT-SPI bus becomes unreachable:
```
dbind-WARNING: Couldn't connect to accessibility bus:
Failed to connect to socket /run/user/1000/at-spi/bus: No such file or directory
```

**Reproduction:** Occurred after ~20-30 AT-SPI operations in a single session, specifically after attempting `accessibility_tree` without app_name filter.

**Impact:** All AT-SPI-dependent tools fail. Requires session restart.

**Workaround:** Always use `app_name` filter with `accessibility_tree`. If AT-SPI fails, stop and restart the session.

---

## D-08: Screen Edge Trigger Does Not Work for Layer-Shell Surfaces

**Status:** Confirmed
**Priority:** Low (D-Bus workaround exists)

**Symptom:** Moving mouse to screen edge (y=0 for top, y=599 for bottom) does not trigger dock visibility in AutoHide/SmartHide modes. The dock's trigger strip (4px at screen edge) does not respond to EIS mouse input.

**Reproduction:**
1. Dock in SmartHide mode, hidden behind active window
2. `mouse_move(400, 0)` → wait → no dock appearance
3. Also tried y=1, y=2 — no response

**Impact:** Cannot test edge-trigger show behavior. All "show dock" operations require D-Bus `invokeShortcut("focus-dock")`.

**Workaround:** Use `dbus_call invokeShortcut("focus-dock")` followed by `mouse_move` to dock area (to switch from keyboard to mouse mode while keeping dock visible).

**Root cause:** EIS mouse coordinates may not reach the layer-shell surface's input region at screen edge. The trigger strip relies on pointer entering a narrow (4px) zone that may be handled differently by KWin's EIS backend.

---

## D-09: AT-SPI Coordinates Are Surface-Local, Not Screen Coordinates

**Status:** Confirmed (expected Wayland behavior)
**Priority:** Informational

**Symptom:** AT-SPI bounding box coordinates (x, y, width, height) reported by `accessibility_tree` and `find_ui_elements` are relative to the Wayland surface, not the screen.

**Conversion formula for bottom-anchored layer-shell surfaces:**
```
screen_x = surface_x  (horizontal position unchanged)
screen_y = (screen_height - surface_height) + surface_y
```

**Example:** Preview surface is 800x400, screen is 800x600.
- Close button at surface (494, 230) → screen (494, 200 + 230) = (494, 430)

**Impact:** All mouse clicks on AT-SPI-located elements need coordinate conversion. Without conversion, clicks may land on wrong elements or different windows.

**Workaround:** Apply the conversion formula. Surface dimensions are available from the frame element in `accessibility_tree`.
