# E2E test scenarios

Accessibility-first end-to-end test scenarios for the Krema dock. The scenarios
are the human-readable oracle for the automated tests: each TC lists them on its
`**Automated:**` lines. The Tier 2 `tests/appium/` harness runs them in a real
KWin session; MOUSE-008 and VIS-007 also have Tier 2 KWin ctests. Tier 3 reuses
the Appium scenarios against installed distro packages. See
`tests/appium/README.md` for the coverage matrix and how to run the suite.

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
**Verification:** AT-SPI state / screenshot / window list / kremarc
```

### Feature IDs

Human-readable identifiers for functional areas. Used to:
- Find related scenarios when a feature changes
- Cross-reference between scenario files

### Affected Files

Source file paths that, when modified, may require re-running the scenario.
This enables the "changed file → affected scenario" lookup (see the mapping below).

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

## Notes for Running Scenarios

The steps describe user actions and observable results, so they can be
followed by hand in a Plasma 6 Wayland session or read as the spec for an
automated test.

### AT-SPI State Reference

Key states used in assertions:
- `focused` — element has keyboard focus (string `"focused"` in states list)
- `focusable` — element can receive focus (always present on interactive items)
- `visible` — element is rendered on screen
- `showing` — element is visible and not obscured
- `sensitive` — element accepts user interaction
- `active` — frame/window is the active window

### AT-SPI Structure for Krema

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

1. **Focus Dock shortcut**: Meta+Alt+D triggers the `focus-dock` global action.
   Where synthetic key presses do not reach KGlobalAccel, invoke the action
   over D-Bus instead:
   ```
   busctl --user call org.kde.kglobalaccel /component/krema \
     org.kde.kglobalaccel.Component invokeShortcut s focus-dock
   ```

2. **Preview popup hidden state**: The popup menu element stays in the tree with
   0x0 size when hidden. Check for absence of `showing`/`visible` states, NOT for
   element absence.

3. **Tool bar focused residue**: After keyboard navigation ends, `[tool bar]` may
   retain `focused` state. Always assert on **button-level** `focused` state.

4. **Parabolic zoom in AT-SPI**: The focused item has a larger bounding box than
   neighbors (e.g., 83x88 vs 64x68). Can be used to verify zoom is active.

5. **AT-SPI coordinates are surface-local**: AT-SPI bounding boxes are relative
   to the Wayland surface, not the screen. Add the surface's screen position
   (from KWin) to get screen coordinates. For a bottom-anchored surface:
   ```
   screen_y = (screen_height - surface_height) + surface_y
   ```

6. **Showing a hidden dock for mouse tests**: In AutoHide or Dodge windows mode,
   move the pointer to the dock's screen edge to reveal the dock, then onto the
   target item. Alternatively, trigger Focus Dock (shows the dock in keyboard
   mode), wait ~500 ms, then move the pointer onto the dock: pointer motion
   cancels keyboard mode while the hover keeps the dock visible.

7. **QMenu context menu not in AT-SPI**: The native QMenu (right-click) is a
   `Qt::Popup` window, which Qt does not expose in the AT-SPI tree. Navigate it
   with the keyboard (Down/Return) or locate entries on a screenshot.
   Menu items are in fixed order: AppName, Pin/Unpin, New Instance, Close,
   separator, Settings..., About Krema, Quit.

8. **Dock-to-preview mouse transition**: Moving the pointer from the dock to the
   preview popup must be gradual (step by step, ~20px increments). A direct jump
   from the dock to the preview area lets the preview close (hidePreviewDelayed
   triggers).

## File-to-Scenario Mapping

Quick reference: which scenarios to re-run when a source file changes.

| Changed File | Re-run Scenarios |
|---|---|
| `src/qml/main.qml` | 01, 02, 03, 04, 05, 06, 07 |
| `src/qml/DockItem.qml` | 01, 02, 05, 06 |
| `src/utils/zoomcalculator.h` | 01, 02, 05, 06, 07 |
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
| Boundary interactions | KBD-010, MOUSE-018, PREV-010 | Correct task activation/preview indices, non-focusable separator, hover geometry, and unchanged item input targets |

The divider reserves a dedicated primary-axis stroke slot outside the task
model, not an extra equal-pitch item or keyboard stop. Observe its accessible
role/name, one normal icon spacing on each side at rest, and its placement
between the current adjacent visual edges during horizontal/vertical layout
and hover. Paint and visual zoom checks need DRM/vgem capture; ordered app
names, KWin geometry, real input, and persistence checks must still run
without it. Under edge-bounded Parabolic zoom or InPlace overlap, disclose
that visual clearance can be smaller than the nominal spacing.

These rows describe acceptance checks, not recorded successes. The exact
automated nodes and run status are maintained in `tests/appium/README.md`.
