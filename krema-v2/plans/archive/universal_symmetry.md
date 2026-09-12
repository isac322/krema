# Plan: Universal Symmetry Implementation

## 1. Objective
Fix the mathematical calculation of the `_dockCeilingPadding` in `AppIcon.qml` to strictly mirror the empty `_dockFloorPadding` (12px) instead of the entire `_totalFloorUnit`. This enforces the "Empty Gap Rule" for perfect visual symmetry regardless of indicator sizes or gaps. 

Update the `ARCHITECTURE Mandate.md` to mathematically enshrine this "Empty Gap Rule."

Update `PanelPage.qml` to dynamically bind its max thickness limit to `_maxTheoreticalThickness` so it always stays in sync with the elastic gap sizing.

## 2. Proposed Changes
- **`src/qml/AppIcon.qml`**:
  - `_dockCeilingPadding: _dockFloorPadding`
  - `_maxTheoreticalThickness: iconSize + _totalFloorUnit + _dockCeilingPadding`
- **`ARCHITECTURE Mandate.md`**:
  - Update Rule 2 to explicitly state the "Empty Gap Rule".
- **`src/qml/settings/PanelPage.qml`**:
  - Update `thicknessSlider.to` to dynamically calculate the exact max thickness using the same formulas, so it doesn't break when icon size scales.

## 3. Execution
- Exit Plan Mode.
- Use `replace` to update `AppIcon.qml`, `PanelPage.qml`, and `ARCHITECTURE Mandate.md`.
