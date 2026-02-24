# E2E Test Scenarios

Accessibility-first end-to-end test scenarios for Krema dock.
Scenarios are verified using `kwin-mcp` (KWin virtual compositor + AT-SPI).

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

1. **Global shortcuts**: `keyboard_key "super+F5"` does NOT trigger KGlobalAccel in
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

## Known Limitations (Not Testable via kwin-mcp)

| Mechanism | Limitation | Alternative |
|-----------|-----------|-------------|
| Screen edge trigger | EIS mouse does not trigger layer-shell edge zones | D-Bus `focus-dock` |
| Active window identification | `list_windows` shows no active/focused state | AT-SPI `active` state on frames (unreliable) |
| QMenu items | Native QMenu not in AT-SPI tree | Screenshot coordinate-based clicks |
| Close button click (22x22) | Very small target with coordinate conversion | Keyboard Delete key |
| Tooltip AT-SPI | Tooltips are `Accessible.ignored: true` by design | Screenshot-only verification |

See `docs/kwin-mcp-issues.md` for detailed issue descriptions and workaround instructions.

## File-to-Scenario Mapping

Quick reference: which scenarios to re-run when a source file changes.

| Changed File | Re-run Scenarios |
|---|---|
| `src/qml/main.qml` | 01, 02, 03, 05, 07 |
| `src/qml/DockItem.qml` | 01, 02, 05 |
| `src/qml/PreviewPopup.qml` | 01, 03 |
| `src/qml/PreviewThumbnail.qml` | 01, 03 |
| `src/shell/previewcontroller.*` | 01, 03 |
| `src/shell/dockshell.cpp` | 01 |
| `src/shell/dockvisibilitycontroller.*` | 01, 07 |
| `src/models/dockactions.*` | 02, 04, 05 |
| `src/models/dockcontextmenu.*` | 04 |
| `src/qml/settings/*` | 06 |
| `src/config/krema.kcfg` | 06 |
| `src/platform/waylanddockplatform.*` | 01, 07 |
| `src/app/application.cpp` | 01 |
| `src/qml/SettingsDialog.qml` | 06 |
| `src/shell/settingswindow.*` | 06 |
| `src/shell/dockview.*` | 02, 07 |
| `src/models/dockmodel.*` | 02, 03 |
