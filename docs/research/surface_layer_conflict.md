# The Surface Layer Conflict (The Stubborn Reality)

## [2026-05-12] Analysis
- **Status:** **UNRESOLVED.** The dynamic orbit and signal desync hotfixes failed to fix the "click-through" at 1.0x and the inaccuracy on first entry while Settings is open.
- **Specific Anomalies Observed:**
  1. **1.0x Save Delay:** The 1.0x zoom scale updates and actually saves only when the settings window is closed.
  2. **First-Entry Defect:** While settings are open, the very first `enterOrbit` for all scales (1.1x to 2.0x) is physically lower than the icons. The `enterOrbit` only updates to match the accurate icon size *after* the user performs their first `exitOrbit`. This happens across all zoom scales except 1.0x (which remains completely unhittable/click-through).
- **Hypothesis:** The conflict is likely caused by the **expanded Wayland surface** used during `liveEditMode`. 
  - When Settings are open, the dock expands its surface to display the blueprint grid.
  - The Wayland `inputRegion` might be falling out of sync with the visual QML state during the initial expansion, only forcing a true geometry recalculation after a complete hover cycle (enter + exit).
  - This explains why the bug **vanishes** as soon as Settings is closed (surface shrinks back to normal).

## [2026-05-12] Rule 17 & Overflow Expansion Attempt
- **Trial:** Refactored `currentVisualOverflow` in `main.qml` to explicitly cover the **Exit Orbit** (Visual Radius + 10px).
- **Result:** Discarded. Expanding the Wayland input region in QML did not resolve the 1.0x click-through issue. 
- **Revised Hypothesis:** The conflict might be deeper in how `WaylandDockPlatform` or `LayerShellQt` handles the surface geometry when the window is physically larger than its visual content. The "click-through" at 1.0x might be a protocol-level rejection of events when they occur in the "expanded" area of a surface that hasn't yet completed its visual transition.
