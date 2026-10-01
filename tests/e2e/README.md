# E2E test scenarios

Accessibility-first end-to-end test scenarios for the Krema dock. The
scenario checklists are automated by the Tier 2 `tests/appium/` harness in a
real KWin session. MOUSE-008 and VIS-007 also have Tier 2 KWin ctests. Tier 3
reuses the Appium scenarios against installed distro packages. See
`tests/appium/README.md` for the coverage matrix and current run status.
The kwin-mcp workflow remains useful for exploratory QA and one-off manual
checks.

The Issue 54 click-policy additions are documented as consumer observations,
not source-text or mock-call checks. New rows stay pending until the parent
runs the source-built and installed suites.
The screen-reservation and pinned/running-section additions follow the same
rule: document real KWin frame geometry and real app/input outcomes, and
leave new coverage pending until its run is recorded. Missing DRM capture
does not prevent the reservation geometry, settings, lifecycle, or drag
checks; painted-divider and pixel checks need a capture-capable session.


## Convention

Each scenario file follows this structure:

```markdown
# <Module Name>

## Features
- <feature-id>: <description>

## Affected Files
- <src/path/to/file>

## TC-XXX: <Title>

**Precondition:** ...
**Steps:**
1. ...
**Expected:** ...
**Verification:** accessibility_tree / screenshot / find_ui_elements
```

### Feature IDs

Human-readable identifiers for functional areas. Used to:
- Find related scenarios when a feature changes
- Cross-reference between scenario files

### Affected Files

Source file paths that, when modified, may require re-running the scenario.
This enables the "changed file → affected scenario" lookup in the Stop hook.
### Test tiers

- **Tier 1:** `tests/qml/` loads production QML headlessly with mocked
  backends and checks consumer-visible QML state.
- **Tier 2:** `tests/appium/` drives a source-built dock in a real KWin
  session. `tests/kwin` ctests use the same virtual-compositor model-state
  boundary.
- **Tier 3:** `tests/distro/` runs the Tier 2 Appium scenarios against Krema
  installed from each distro package.

Two-output cases require the existing command:

```sh
KREMA_E2E_OUTPUT_COUNT=2 tests/appium/run-e2e.sh -m outputs
```

The `outputs(2)` marker selects the existing multi-screen cases; a one-output
session skips them. The scenarios use the existing `krema_e2e` helpers and
retain their documented AT-SPI, EIS, QMenu, coordinate, and DRM/vgem limits.


## Execution Guide

### Environment Setup

```bash
# Start kwin-mcp session (800x600 for visual verification)
session_start screen_width=800 screen_height=600
  app_command="/home/bhyoo/projects/c++/krema/build/dev/bin/krema"

# Launch test apps
launch_app command="kcalc"
```

Note: `krema` is not in PATH — use the full build path.

### Verification Tools

| Tool | Use When |
|------|----------|
| `accessibility_tree` | Checking AT-SPI roles, names, states (focused, expanded) |
| `find_ui_elements` | Searching for specific UI elements by name/role |
| `screenshot` | Visual verification (zoom, animations, layout) |
| `keyboard_key` | Simulating key presses |
| `mouse_click` / `mouse_move` | Mouse interactions |
| `dbus_call` | Triggering global shortcuts (focus-dock) |

### AT-SPI State Reference (Verified via PoC)

Key states used in assertions:
- `focused` — element has keyboard focus (string `"focused"` in states list)
- `focusable` — element can receive focus (always present on interactive items)
- `visible` — element is rendered on screen
- `showing` — element is visible and not obscured
- `sensitive` — element accepts user interaction
- `active` — frame/window is the active window

### AT-SPI Structure for Krema (Verified)

```
[application] "krema"
  [frame] ""                          ← dock surface (layer-shell)
    [tool bar] "Krema Dock"
      [button] "<AppName>"            ← dock items (focusable, focused when keyboard-active)
  [frame] ""                          ← preview surface (layer-shell)
    [filler] ""
      [popup menu] "Preview for <AppName>"   ← preview popup (0x0 when hidden)
        [label] "<AppName>"
        [separator]
        [button] "<WindowTitle>"       ← thumbnails (focusable, focused when keyboard-active)
          [button] "Close <Title>"     ← close button
          [label] "<Title>"
```

### Important Patterns

1. **Global shortcuts**: `keyboard_key "super+alt+d"` does NOT trigger KGlobalAccel in
   kwin-mcp (EIS limitation). Use D-Bus `invokeShortcut` instead:
   ```
   dbus_call service="org.kde.kglobalaccel" path="/component/krema"
     interface="org.kde.kglobalaccel.Component" method="invokeShortcut"
     args=["string:focus-dock"]
   ```

2. **Preview popup hidden state**: The popup menu element stays in the tree with
   0x0 size when hidden. Check for absence of `showing`/`visible` states, NOT for
   element absence.

3. **Tool bar focused residue**: After keyboard navigation ends, `[tool bar]` may
   retain `focused` state. Always assert on **button-level** `focused` state.

4. **Parabolic zoom in AT-SPI**: The focused item has a larger bounding box than
   neighbors (e.g., 83x88 vs 64x68). Can be used to verify zoom is active.

5. **AT-SPI coordinates are surface-local**: Bounding boxes from `accessibility_tree`
   are relative to the Wayland surface, not the screen. For bottom-anchored surfaces:
   ```
   screen_y = (screen_height - surface_height) + surface_y
   ```
   Example: Preview surface 800x400, screen 800x600 → offset = 200.
   Close button at surface (494, 230) → screen (494, 430).

6. **Screen edge trigger does not work**: Mouse movement to screen edge does not
   trigger dock show in AutoHide/SmartHide. Use D-Bus `focus-dock` then `mouse_move`
   to dock area to switch to mouse mode while keeping dock visible.

7. **Dock show sequence for mouse tests**: When dock is hidden (SmartHide/AutoHide):
   ```
   dbus_call invokeShortcut("focus-dock")    # show dock (enters keyboard mode)
   sleep 500ms
   mouse_move(icon_x, dock_y)                # move to dock (cancels keyboard mode)
   sleep 300ms                                # wait for hover to register
   ```

8. **QMenu context menu not in AT-SPI**: Native QMenu (right-click) does not appear
   in `accessibility_tree`. Use screenshot to estimate menu item coordinates.
   Menu items are in fixed order: AppName, Pin/Unpin, New Instance, Close,
   separator, Settings..., About Krema, Quit.

9. **Dock-to-preview mouse transition**: Moving mouse from dock to preview popup
   must be gradual (step by step, 20px increments). Direct jump from dock to
   preview area causes preview to close (hidePreviewDelayed triggers).

10. **AT-SPI bus instability**: Avoid `accessibility_tree` without `app_name` filter.
    If AT-SPI bus fails, restart the session.

### Session Size

Always use **800x600** — dock elements are large enough for visual verification.

## Known kwin-mcp limitations

These are limitations of the kwin-mcp session (EIS input, its own compositor
view), not of the scenarios: the `tests/appium/` harness handles each as
noted.

| Mechanism | kwin-mcp limitation | kwin-mcp workaround | In tests/appium |
|-----------|---------------------|---------------------|-----------------|
| Screen edge trigger | EIS mouse does not trigger layer-shell edge zones | D-Bus `focus-dock` | Real fake-input pointer reaches the edge strip (`test_vis003_auto_hide_shows_on_screen_edge_approach`) |
| Active window identification | `list_windows` shows no active/focused state | AT-SPI `active` state on frames (unreliable) | `kwin.active_window()` via KWin scripting (`evaluate`) |
| QMenu items | Native QMenu not in AT-SPI tree | Screenshot coordinate-based clicks | KWin popup window + keyboard navigation over `context_menu_entries()` order |
| Close button click (22x22) | Very small target with coordinate conversion | Keyboard Delete key | Clicked directly: screen coordinates from the AT-SPI rect plus the surface's KWin position (`test_prev004_close_button_closes_that_window`) |
| Tooltip AT-SPI | Tooltips are `Accessible.ignored: true` by design | Screenshot-only verification | Screenshot diff: the tooltip is the only pixel change (`test_mouse004_tooltip_shows_app_name_on_hover`) |

See `docs/kwin-mcp-issues.md` for detailed issue descriptions and workaround instructions.

## File-to-Scenario Mapping

Quick reference: which scenarios to re-run when a source file changes.

| Changed File | Re-run Scenarios |
|---|---|
| `src/qml/main.qml` | 01, 02, 03, 04, 05, 06, 07 |
| `src/qml/DockItem.qml` | 01, 02, 05, 06 |
| `src/qml/PreviewPopup.qml` | 01, 03 |
| `src/qml/PreviewThumbnail.qml` | 01, 03 |
| `src/shell/previewcontroller.*` | 01, 03 |
| `src/shell/dockshell.*` | 01, 06 |
| `src/shell/dockvisibilitycontroller.*` | 01, 05, 06, 07 |
| `src/models/dockactions.*` | 02, 03, 04, 05, 06 |
| `src/models/dockcontextmenu.*` | 04 |
| `src/models/notificationtracker.*` | 01, 04 |
| `src/models/launcherentrytracker.*` | 01 |
| `src/qml/settings/*` | 06, 07 |
| `src/config/krema.kcfg` | 02, 03, 05, 06, 07 |
| `src/platform/waylanddockplatform.*` | 01, 07 |
| `src/platform/kwinpointermotionwatcher.*` | 01 |
| `src/app/application.*` | 01, 06, 07 |
| `src/qml/SettingsDialog.qml` | 06 |
| `src/shell/settingswindow.*` | 06, 07 |
| `src/shell/dockview.*` | 02, 06, 07 |
| `src/models/dockmodel.*` | 01, 02, 03, 04, 05, 06 |
| `src/shell/multidockmanager.*` | 02, 03, 06, 07 |
| `src/shell/edgetrigger.*` | 06 |
| `src/shell/outputordermonitor.*` | 02, 03, 06, 07 |
| `src/platform/dockplatform.*` | 01, 07 |

`src/qml/main.qml` maps to all seven scenario files because it owns the dock
input dispatch. Issue 54 changes only the left-click policy branch. Keyboard
activation, context-menu actions, middle/right clicks, wheel cycling, drag
handling, visibility, and settings lifecycle keep their existing contracts;
the affected scenario files record those preservation checks without
duplicating their full procedures.

### Screen reservation and pinned/running sections

| Contract | Scenario | Required observation |
|---|---|---|
| Reservation control/default/persistence | SET-015 | Real Behavior switch, saved value, same maximized KWin window reflows live and after Krema restart |
| Reservation geometry | VIS-001, VIS-009 | Maximized frame/output bounds on all four edges, floating off/on, current icon size, reservation off/on, and no reservation in Auto Hide/Dodge Windows |
| Section control/default/persistence | SET-016 | Real Behavior switch, ordered dock app names, pinned membership, accessible separator, and restart |
| Pin/unpin and app lifecycle | CTX-007 | Running pinned apps retain one pinned slot; unpinned apps follow; live launch/close/group and empty-section transitions |
| Zone drag policy | DND-006 | Real drags reorder within the source section, clamp cross-section moves without pinning, and allow free ordering when separation is off |
| Boundary interactions | KBD-010, MOUSE-017, PREV-010 | Correct task activation/preview indices, non-focusable separator, hover geometry, and unchanged item input targets |

The divider is an overlay, not a task or an extra equal-pitch item. Observe
its accessible role/name and its placement between the current adjacent
item centres during horizontal/vertical layout and hover. Paint and visual
zoom checks need DRM/vgem capture; ordered app names, KWin geometry, real
input, and persistence checks must still run without it.

These rows describe acceptance checks, not recorded successes. The exact
automated nodes and run status are maintained in `tests/appium/README.md`.
