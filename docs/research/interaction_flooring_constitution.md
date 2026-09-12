# Research: Interaction Flooring Constitution

## [OUTDATED] Initial Static Orbit Logic
Initially, the Zoom Orbit was calculated using a static formula centered on the panel's geometric center:
- `maxReach = (iconSize * maxZoomFactor) / 2 + 40`
- **Problem:** This created "Interaction Drift." As the panel thickness increased, the interaction zone shifted away from the icons because the icons remained anchored to the floor (Edge) while the orbit remained in the center of the growing panel. It also created "Air Gaps" at high scales where the zoom triggered before the mouse reached the icon.

### [CURRENT] The Interaction Flooring Constitution (Rule 17)
To ensure absolute mathematical consistency between visual rendering and interaction logic, the dock's vertical (or cross-axis) stack is governed by a strict variable-based constitution codified as **Rule 17**.

#### The 5-Unit Stack (The Unit Mandate)
1.  **`Floor Padding`**: Internal space between the panel's anchored edge and the indicators (default 25% of iconSize).
2.  **`Indicator`**: Visual dot/dash height (default 10% of iconSize).
3.  **`Gap`**: Space between indicator and icon image (scales with `indicatorOffset`).
4.  **`Icon`**: The visual icon pixels, synchronized with the zoom factor (`iconSize * currentScale`).
5.  **`Panel Ceiling Padding`**: Internal space above the icon, strictly mirroring Unit #1 per Rule 2 (Illusion of Symmetry).

#### Mandatory Unit Variables
Every gap, padding, and physical element MUST be defined as its own explicit, mathematically calculated unit variable (e.g., `_unitPanelFloor`). Hardcoded pixel values and implicit math are strictly forbidden in layout and hit-testing calculations.

### Absolute Interaction Sync Logic

#### 1. Pixel-Perfect Zoom Orbit
The orbit center is no longer the panel center. It is calculated by summing the visual units to find the exact center of the current tallest icon:
- `VisualRadius = (MaxIconThickness / 2) + InterGap + Indicator + PanelFloor`
- `ExitOrbit = VisualRadius + 10px (User Grip)`
- **Benefit:** The interaction zone now expands and contracts in perfect 1:1 sync with the visual "Milk & Roast" wave.

#### 2. Distance-Based Hover Zone
The hover state (clickability) is decoupled from the zoom orbit and uses a "Distance from Visual Center" check:
- `dist = Math.sqrt(dx^2 + dy^2)`
- `isHit = dist <= (iconSize / 2)`
- **Benefit:** Resolves the scale-origin mismatch. The hover state now perfectly matches the visual icon pixels at any zoom level, preventing the "Early Exit" mismatch between zoom and hover.

#### 3. Absolute Indicator Grounding
Indicators are anchored using absolute QML anchors pinned to the panel edge with the `_unitPanelFloor` margin.
- **Benefit:** Indicators remain mathematically locked to the panel edge and do not move vertically during the zoom wave.
