# Plan: Implement Proportional Floor Padding

## 1. Objective
Refine the Dimensional Sync Protocol by making the `_dockFloorPadding` proportional to the icon size (25%). This replaces the hardcoded 12px value, ensuring that the entire dock envelope scales like a vector graphic and preventing the visual "y-intercept" distortion when icons are resized with the proportional lock enabled.

## 2. Proposed Changes
- **`src/qml/AppIcon.qml`**:
  - Replace `readonly property real _dockFloorPadding: 12` with `readonly property real _dockFloorPadding: Math.max(4, Math.round(iconSize * 0.25))`.
- **`src/qml/settings/PanelPage.qml`**:
  - Replace `readonly property real _floorPadding: 12` with `readonly property real _floorPadding: Math.max(4, Math.round(DockSettings.iconSize * 0.25))`.
- **`src/qml/settings/IconsPage.qml`**:
  - Update `calculateMaxEnv(size)` to replace the hardcoded `12` with `let floorPad = Math.max(4, Math.round(size * 0.25))` and adjust the return formula accordingly.

## 3. Execution
- Exit plan mode.
- Use `replace` to update `AppIcon.qml`, `PanelPage.qml`, and `IconsPage.qml`.
- Ask user to run and verify the elastic scaling.