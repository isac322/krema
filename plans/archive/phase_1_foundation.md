# Plan: Architectural Alignment Phase 1 - Foundation (Symmetry & Grounding)

Align the core dock geometry in `AppIcon.qml` and `main.qml` with **Rule 1 (Symmetry Axiom)** and **Rule 4 (Independent Panel Height)** of the `ARCHITECTURE Mandate.md`.

## 1. Research & Analysis
- **AppIcon.qml:** Current `_indicatorSpace` and `width/height` calculations use hardcoded values (Lines 300-315).
- **main.qml:** Current `dockRow` centering logic (`y` calculation on Lines 530-540) uses hardcoded offsets (e.g., `10`).
- **Goal:** Replace all hardcoded "guessing" with a unified padding variable that ensures `top_padding == bottom_padding`.

## 2. Strategy
- Define a `readonly property real _crossAxisPadding` in `AppIcon.qml` that calculates the required padding to center the icon within the panel's max theoretical height.
- Update `AppIcon.qml` dimensions to use this padding for grounding.
- Update `main.qml`'s `dockRow` positioning to use mathematical grounding instead of hardcoded `10px` offsets.

## 3. Execution (Surgical Steps)
1. **Step 1:** Modify `AppIcon.qml` to define the mathematical padding properties.
2. **Step 2:** Update `AppIcon.qml`'s `width` and `height` to be strictly driven by Rule 1 and Rule 4.
3. **Step 4:** Update `main.qml`'s `dockRow` centering/grounding logic to use the new padding instead of hardcoded offsets.

## 4. Validation
- Verify visual symmetry using the `--debug-geom` flag.
- Ensure changing the "Panel Thickness" slider in settings doesn't cause icons to "jump" or lose centering.
- Check all 4 edges (Top, Bottom, Left, Right) for consistency.
