# Plan: Fix Dimensional Sync Bug

## 1. Objective
Investigate and fix the issue where the `iconSizeSlider` becomes unresponsive when the Dimensional Sync lock (`SyncPanelThickness`) is active. The bug is likely caused by a mismatch in the mathematical envelope formulas across different QML files, or a binding feedback loop when updating the settings sequentially.

## 2. Proposed Changes
- **`src/qml/settings/IconsPage.qml`**:
  - Update `calculateMaxEnv` to strictly match the "Pure Proportional Gap" math currently active in `AppIcon.qml` (`0.08` base ratio instead of `0.125`).
  - Add a `[SYNC-DEBUG]` terminal log to the `onMoved` event to print the exact ratio calculations.
  - Apply the new `iconSize` and `panelHeight` settings simultaneously and ensure `DockSettings.save()` commits the correct values.

- **`src/qml/settings/PanelPage.qml`**:
  - Update the `_indicatorGap` mathematical helper to strictly match the `0.08` base ratio so the UI slider limits match the physical dock limits.

## 3. Execution
- Exit plan mode.
- Use `replace` to update `IconsPage.qml` and `PanelPage.qml`.
- Run the application with `--debug-geom` to verify the slider moves smoothly.