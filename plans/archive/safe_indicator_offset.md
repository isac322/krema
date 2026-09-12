# Plan: Implement Safe Indicator Offset

## 1. Objective
Make the "Indicator Offset" slider functional without breaking the "Slot Size" symmetry or causing the icons to jitter. This will be achieved using a "Shift-Only" logic that moves the icon within its fixed slot, rather than expanding the slot itself.

## 2. Proposed Changes
- **`src/qml/AppIcon.qml`:**
  - Locate the `iconImage` Item.
  - Define a new local property `_pushOffset`: `(1.0 - DockSettings.indicatorOffset) * 40.0`. This provides a maximum shift of 20px when the slider is at 0.5.
  - Update the declarative `x` and `y` grounding formulas to include `+ _pushOffset` (or `- _pushOffset` depending on the edge). This will effectively push the icon away from the `_totalFloorUnit` (the grounded indicators) without altering the `_maxTheoreticalThickness` of the delegate.

This perfectly fulfills Rule 2 (Illusion of Symmetry) because the total empty gap is preserved within the slot, but the icon itself floats further from the baseline.

## 3. Execution
- Use `replace` to update `AppIcon.qml`.