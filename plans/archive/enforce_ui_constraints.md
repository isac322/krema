# Plan: Enforcing UI Constraints (Rule 6)

## 1. Objective
Completely eliminate "dead zones" in the settings UI. When an overarching dimension shrinks (e.g., Indicator Offset increases, reducing the gap), it reduces the mathematical limits of subordinate dimensions (Panel Thickness, Corner Radius). We must actively clamp the underlying settings to these new limits during interaction.

## 2. Proposed Changes
- **`src/qml/settings/IconsPage.qml`**:
  - Update `indicatorOffsetSlider.onMoved`:
    - Set the new offset.
    - Calculate the new `_maxEnv` using `iconsLayout.calculateMaxEnv(DockSettings.iconSize)`.
    - If `DockSettings.panelHeight > _maxEnv`, clamp it to `_maxEnv`.
    - If `DockSettings.cornerRadius > Math.floor(DockSettings.panelHeight / 2)`, clamp it.
    - Move `save()` to `onPressedChanged` to match the performance optimization.
  - Update `iconSizeSlider.onMoved` to also clamp `cornerRadius` if `panelHeight` shrinks during sync.

- **`src/qml/settings/PanelPage.qml`**:
  - Update `thicknessSlider.onMoved`:
    - Set the new height.
    - If `DockSettings.cornerRadius > Math.floor(value / 2)`, clamp `DockSettings.cornerRadius`.

## 3. Execution
- Exit plan mode.
- Apply replacements to both files.