# Plan: Fix Indicator Offset

## 1. Objective
Make the "Indicator Offset" slider functional again. It should push the icons away from the indicators, increasing the gap between them. This will dynamically alter the `_indicatorGap` while respecting the 2nd mandate (Illusion of Symmetry), which requires the empty space above the icon to match the empty space below the indicators.

## 2. Proposed Changes
- **`src/qml/AppIcon.qml`**:
  - Make `_indicatorGap` a dynamic property that binds to `DockSettings.indicatorOffset`.
  - The formula will be: `4 + (iconSize * (1.0 - DockSettings.indicatorOffset))`. 
  - As the user moves the slider from 1.0 (100%) down to 0.5 (50%), the gap will increase proportionally to the icon size, pushing the icon upwards.
  - Because `_maxTheoreticalThickness` is calculated using `_totalFloorUnit`, the panel will automatically expand to accommodate this new gap, and the `_dockCeilingPadding` will remain equal to the `_dockFloorPadding` (12px), keeping the symmetry illusion intact.

## 3. Execution
- Use `replace` to update `AppIcon.qml`.