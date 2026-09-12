# Plan: Dynamic Corner Radius (Rule 6)

## 1. Objective
Implement Rule 6 (Dynamic UI Blindness Prevention) for the dock's corner radius, while also preventing the "eating itself" rendering glitch caused by QML when the radius exceeds half of the rectangle's shortest dimension.

## 2. Proposed Changes
- **`src/qml/main.qml`:**
  - Update the `dockPanel`'s `radius` property. Instead of blindly using `DockSettings.cornerRadius`, it will use `Math.min(DockSettings.cornerRadius, Math.min(width, height) / 2)`. This mathematically clamps the visual shape to a perfect pill at maximum and prevents the radius from creating sharp points or overlapping artifacts.
- **`src/qml/settings/PanelPage.qml`:**
  - Update the `radiusSlider`'s `to` limit. It currently has a hardcoded limit of `48`.
  - It will be dynamically bound to `Math.floor(thicknessSlider.value / 2)`. This ensures the UI slider physically cannot enter the mathematical "dead zone" (where moving the slider no longer increases the visual roundness because it's already a pill), directly fulfilling Rule 6.

## 3. Execution
- Use `replace` to update `main.qml`.
- Use `replace` to update `PanelPage.qml`.