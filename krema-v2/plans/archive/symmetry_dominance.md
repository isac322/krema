# Plan: Symmetry Dominance & Rule 6 Enforcement

## 1. Objective
Elevate **Symmetry** to the supreme law of the dock, ensuring icons remain perfectly balanced on the panel's cross-axis even during visual overflow. This replaces the "Flush Grounding" model with a "Symmetric Sandwich" model. Simultaneously, strictly enforce **Rule 6** by clamping setting values to their mathematical limits in real-time, eliminating all UI "dead zones."

## 2. Proposed Mandate Changes (`ARCHITECTURE Mandate.md`)
- **Rule 1: The Supreme Law of Symmetry (The Axiom)**
  - Symmetry is the foundational law. The empty gaps above and below the icon unit (icon + indicators + gap) must remain mathematically identical at all scales and in all overflow states.
- **Rule 2: Geometric Grounding (Grounded to Center)**
  - Icons are grounded to the *Center Line* of the panel's cross-axis.
  - The "Gravity Chain" remains: Screen -> Floating Gap -> Panel Center -> Proportional Gaps -> Icon.
- **Rule 5: Symmetric Visual Overflow**
  - When panel thickness is reduced, the panel background shrinks symmetrically toward its center line.
  - The icons remain fixed in space; the panel background "uncovers" the icons from both top and bottom edges equally.

## 3. Proposed Code Changes
- **`src/qml/main.qml`**:
  - Update `dockRow` positioning: Always use `(dockPanel.width - animatedContentWidth) / 2` and `(dockPanel.height - animatedContentHeight) / 2`. This ensures symmetric overflow in all 4 edge placements.
- **`src/qml/settings/IconsPage.qml`**:
  - In `onMoved` of `iconSizeSlider` and `indicatorOffsetSlider`:
    - Calculate the new `_maxEnv`.
    - If `DockSettings.panelHeight > _maxEnv`, set it to `_maxEnv` (Forced Rule 6 Clamp).
- **`src/qml/settings/PanelPage.qml`**:
  - In `onMoved` of `thicknessSlider`:
    - Set the new height.
    - If `DockSettings.cornerRadius > Math.floor(value / 2)`, set it to `Math.floor(value / 2)` (Forced Rule 6 Clamp).

## 4. Execution
- Exit Plan Mode.
- Apply `replace` to the mandate and QML files.
- Build and verify the "Symmetric Sandwich" behavior.
