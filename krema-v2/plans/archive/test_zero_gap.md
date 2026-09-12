# Plan: Test Zero Gap Baseline

## 1. Objective
Test the visual appearance of the dock when the mathematical gap between the indicators and the icon is set to exactly 0. This will empirically determine if the perceived "large gap" is due to invisible padding within the icon image files or an error in the layout logic.

## 2. Proposed Changes
- **`src/qml/AppIcon.qml`**:
  - Temporarily hardcode `_indicatorGap` to `0`.

## 3. Execution
- Exit plan mode.
- Use `replace` to update `AppIcon.qml`.
- Ask user to run the app and observe the visual result.