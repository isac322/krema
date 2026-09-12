# Plan: Fix Panel Envelope Math Discrepancy

## 1. Objective
Fix a mathematical discrepancy in `src/qml/settings/PanelPage.qml`. During recent iterative updates to the proportional indicator gap, `AppIcon.qml` and `IconsPage.qml` were updated to use a base ratio of `0.125`, but `PanelPage.qml` was left at `0.08`. This causes the UI slider's calculation for the Maximum Envelope to be smaller than the actual dock geometry, violating Rule 6 (UI Blindness Prevention) and Rule 2 (Illusion of Symmetry).

## 2. Proposed Changes
- **`src/qml/settings/PanelPage.qml`**:
  - Locate the `_indicatorGap` property within the `panelLayout`.
  - Update the base multiplier from `0.08` to `0.125` to perfectly match the logic in the other files.
  - Formula will be: `Math.max(2, Math.round(DockSettings.iconSize * 0.125) + Math.round(DockSettings.iconSize * 0.15 * (1.0 - DockSettings.indicatorOffset)))`

## 3. Execution
- Exit plan mode.
- Use `replace` to update `src/qml/settings/PanelPage.qml`.
- Run the application with `--debug-geom` to verify the sync logic behaves perfectly without mathematical divergence.