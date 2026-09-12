# Product Quality Lessons Learned

This file contains the "burned-in" anti-patterns to prevent recurring mistakes.

## 2026-05-19: QML Binding Race Conditions
- **Anti-Pattern:** Assuming that global singletons (DockSettings, etc.) and model data are available synchronously during the `onCompleted` phase or initial property binding of instantiated components (AppIcon).
- **Observation:** Persistent `Unable to assign [undefined] to bool` warnings in logs during startup.
- **Root Cause:** AppIcon properties bind to model/settings values before those values are populated by the backend, leading to temporary `undefined` states.
- **Prevention Rule:** In non-critical visual components, expect and tolerate initial binding warnings if the property eventually stabilizes. Do not over-engineer complex null-guarding logic (e.g., unnecessary property resets) unless the UI exhibits flickering or functional failure.

## 2026-05-23: Vector Graphics Size Checks
- **Anti-Pattern:** Using `icon.availableSizes().isEmpty()` as a safety check before returning a `QIcon` to QML.
- **Observation:** Flatpak vector graphics (SVGs) were successfully found by the extractor but discarded right before rendering, leaving an empty fallback in the UI.
- **Root Cause:** SVGs are vector-based and lack predefined pixel dimensions. They will inherently report an empty list for `availableSizes()` until they are explicitly rendered.
- **Prevention Rule:** Never use `availableSizes().isEmpty()` as a validation step for user-provided icons or themes. If the file exists and forms a valid `QIcon`, trust the QML image provider to resolve it.

## 2026-05-23: Raw IPC Socket Interception
- **Anti-Pattern:** Assuming that standard `hyprctl dispatch <command>` syntaxes will work universally over the Hyprland UNIX socket without error handling.
- **Observation:** Context menu "Close" buttons failed silently with no effect.
- **Root Cause:** Users running the `hyprland-lua-plugins` extension have their raw IPC socket dispatches intercepted and overridden by the Lua interpreter, breaking commands like `closewindow address:` with errors like `expected a dispatcher (e.g. hl.dsp.window.close())`.
- **Prevention Rule:** All hardcoded dispatch commands sent to the Hyprland IPC socket MUST include a fallback handler that dynamically catches Lua execution errors and rewrites the command using the `hl.dsp.*` syntax.

## 2026-06-01: Wayland Layer Surface Lifecycle (The hide/show Anti-Pattern)
- **Anti-Pattern:** Attempting to force KWin (or any Wayland compositor) to redraw or recover a `wlr_layer_surface` by calling `QWindow::hide()` and `QWindow::show()`.
- **Observation:** The dock appears to "float" or "move up" from the edge of the screen after unlocking, even though logs show the correct `AnchorBottom` flag and `exclusive_zone` being requested.
- **Root Cause:** When `hide()` and `show()` are executed while the compositor is waking up from DPMS sleep, the dock re-enters KWin's layer stacking order mid-evaluation. KWin evaluates the dock's exclusive zone *after* system panels, stacking the dock's reserved space *on top* of the other panels instead of flushing it against the true screen edge.
- **Prevention Rule:** NEVER use `hide()/show()` to recover broken Wayland surfaces. If a layer surface is destroyed or desynced by the compositor, trigger a full application-level topology rebuild (e.g., `scheduleTopologyUpdate()`) to cleanly tear down and reconstruct the shell hierarchy from scratch. This guarantees the compositor calculates placement from a pristine, conflict-free state.

## 2026-06-01: The "Phantom Bug" Revert (Blind Geometry Hacking)
- **Anti-Pattern:** Rewriting geometric logic (especially working logic) to fix a bug without first explicitly diagnosing if the bug is actually caused by that component, or if a previous fix has already solved it.
- **Observation:** Ripping out `anchors.fill: parent` from `IslandModule` and replacing it with a hardcoded `DockSettings.panelHeight` binding.
- **Root Cause:** A screenshot showed the Glass Pill floating below the dark panel (the "Floating Frankenstein" bug). I assumed the Glass Pill's `anchors.fill` was flawed. I completely forgot that I had *already* fixed the root cause in a previous session by anchoring the parent `dockRow` to the floor! My "fix" forced the Glass Pill to match the shrinking dark panel height, permanently breaking its core purpose (wrapping the protruding icons).
- **Prevention Rule:** **Enforce Rule 8 & 11.** Before attempting to rewrite *any* geometric logic to fix a bug, you MUST pause and write out a formal Diagnostic Report. Explicitly prove the mathematical root cause. If the component is mathematically sound (like a pure `anchors.fill`), the bug is guaranteed to be in the parent layout or offset calculation. NEVER replace working logic with hardcoded pixel limits.

## 2026-06-02: Duplicated Math Formulas (The Missing +16)
- **Anti-Pattern:** Duplicating a mathematical formula in multiple places with subtle differences, then using the wrong version as a clamp.
- **Observation:** The `calculateMaxEnv()` function (used for slider clamping) was missing the `+ 16` padding that the `_maxEnv` readonly property had. This allowed the panel to be 16px shorter than the actual glass pill, causing visual overflow.
- **Root Cause:** The function was originally in `islandMarginLayout` (for a different purpose) and didn't include the `+16`. When it was promoted to a global clamp, the formula mismatch went unnoticed.
- **Prevention Rule:** NEVER duplicate a sizing formula. If two properties or functions compute the same geometric value, extract it into a single shared function and reference it from both places. When moving a formula to a new scope, always diff it against the canonical version.

## 2026-06-02: Proportional Slider Dead Zones
- **Anti-Pattern:** Capping one value (panelHeight) when a proportionally-linked value (iconSize) hits its limit, creating a dead zone where the slider does nothing.
- **Observation:** When iconSize hit 96, `panelHeight` was capped to `round(96 / ratio)`, making the slider unresponsive from that point to its maximum (200px).
- **Root Cause:** The unified clamping logic `newPanelHeight = Math.min(newPanelHeight, Math.round(96 / parent.capturedRatio))` froze panelHeight at ~144px for a ratio of 0.667, even though the slider went to 200.
## 2026-06-07: The "Invisible Click-Blocking Wall" (Input Region Margin Bug)
- **Anti-Pattern:** Applying rendering margins (intended for soft features like drop shadows) to physical interception boundaries like the Wayland `InputRegion`.
- **Observation:** Users were unable to click windows or interact with the desktop directly above or beside the dock, even though visually there was nothing there but empty space.
- **Root Cause:** A 64px `margin` was mathematically appended to the bounding box of the `InputRegion` to ensure the Wayland compositor didn't clip the outer glow shader. While extending the compositor's rendering surface by 64px is necessary for shaders, applying that same 64px extension to the *input interception region* turned the shadow into a solid wall of invisible click-blocking glass. Furthermore, a static `m_visible` override was artificially expanding the vertical bounds to its absolute maximum `zoomOverflowHeight` even when the dock was idle.
- **Prevention Rule:** NEVER reuse rendering geometry variables (like `margin` or `shadowBounds`) for interaction or hit-testing logic (`InputRegion`). The Wayland `InputRegion` MUST exclusively trace the tangible UI elements. If an interactive feature requires expanded space (like catching massive zoomed icons), the `InputRegion` MUST be dynamically tied to the QML `hovered` state so it instantly shrinks back to free the desktop when the interaction ends.
