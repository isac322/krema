# Plan: Restore Dynamic Corner Radius Clamping

## 1. Objective
Restore the mathematical clamping for the `dockPanel`'s corner radius in `main.qml`. This was lost during the recent codebase reversion. The clamping is necessary to enforce Rule 6 (Dynamic UI Blindness Prevention) and prevent rendering glitches caused by the radius exceeding half the panel's shortest dimension.

## 2. Proposed Changes
- **`src/qml/main.qml`**:
  - Locate the `radius` property of `dockPanel`.
  - Replace `radius: DockSettings.cornerRadius` with `radius: Math.min(DockSettings.cornerRadius, Math.min(width, height) / 2)`.
  - Add explanatory comments linking this logic to Rule 6.

## 3. Execution
- Use `replace` to update `main.qml`.