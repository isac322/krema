# Plan: Pure Proportional Indicator Gap

## 1. Objective
Refine the `_indicatorGap` calculation in `AppIcon.qml` to be purely proportional to the `iconSize`. This fixes the issue where the hardcoded 4px baseline caused the gap to appear disproportionately large when icons were scaled down to smaller sizes (e.g., 24px).

## 2. Proposed Changes
- **`src/qml/AppIcon.qml`:**
  - Locate the `_indicatorGap` property.
  - Update the calculation from:
    `4 + (iconSize * 0.25 * (1.0 - DockSettings.indicatorOffset))`
    to:
    `Math.max(2, Math.round(iconSize * 0.08) + Math.round(iconSize * 0.15 * (1.0 - DockSettings.indicatorOffset)))`
  - This ensures a tighter baseline gap (8% of icon size, minimum 2px) and a more controlled expansion (up to an additional 15% of icon size when the slider is at 0.5), keeping proportions elegant at all scales.

## 3. Execution
- Exit plan mode.
- Use `replace` to update `src/qml/AppIcon.qml`.
- Ask user to run the app and verify the visual feel.