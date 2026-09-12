# Plan: Dynamic Indicator Sizing and Constraints

## 1. Objective
Make the indicator size (`_dotHeight`) dynamic relative to the `iconSize` using a 10% ratio. Furthermore, ensure that the active "dash" (the stretched indicator) scales proportionally (e.g., 35% of the icon size) so that it never outgrows the icon itself, creating a cohesive visual unit.

## 2. Proposed Changes
- **`src/qml/AppIcon.qml`:**
  - Define `_dotHeight` as `Math.max(2, Math.round(iconSize * 0.10))` to maintain the 10% golden ratio.
  - Define a new property `_activeDotWidth` as `Math.max(_dotHeight * 2, Math.round(iconSize * 0.35))`. This ensures the active indicator is a clear "dash" but never exceeds the icon's width.
  - Update the `Rectangle` inside the indicator `Repeater` to use these new dynamic properties instead of hardcoded `4` and `16` pixel values.

## 3. Execution
- Use `replace` to update `AppIcon.qml`.