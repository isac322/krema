# Plan: Implement Icon Mode (Icon vs Icon+Text)

## 1. Objective
Enable users to choose how task manager icons are displayed: "Icons Only" (current behavior) or "Icons + Names" (label display).

## 2. Proposed Changes
- **`src/config/krema.kcfg`**:
  - Add `IconDisplayMode` integer setting (0 = Icon Only, 1 = Icon + Text). Default: 0.
- **`src/qml/AppIcon.qml`**:
  - Add a `QQC2.Label` for the task name, conditionally rendered based on `DockSettings.iconDisplayMode`.
  - Ensure the label text is truncated if it exceeds a reasonable width, to prevent dock bloat.
  - Apply the mandated "Inside World" symmetry math to the label position so it doesn't break the grounded icon grounding.
- **`src/qml/settings/IconsPage.qml`**:
  - Add a "Icon Mode" section with a radio button or toggle to switch between the two modes.

## 3. Execution
- Exit Plan Mode.
- Apply changes.
- Build and verify.
