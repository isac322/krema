# Plan: Unified Geometry Debugging Framework

## 1. Objective
Establish a comprehensive geometry debugging framework using the `--debug-geom` flag across all physical and interactive components of the dock, fulfilling a new foundational mandate.

## 2. Proposed Changes

### A. Core Mandate Update (`GEMINI.md`)
- Add **Rule 12: The Geometry Debugging Mandate**.
- Require all current and future components (Panel, Icons, Indicators, Separators, Mouse Areas, Input Regions) to log their `X`, `Y`, `Width`, and `Height` when `--debug-geom` is active.

### B. AppIcon Component (`src/qml/AppIcon.qml`)
- Ensure the `[GEOM-ICON]` and `[GEOM-INDIC]` logs cover `X, Y, W, H` comprehensively.

### C. Main Panel & Layout (`src/qml/main.qml`)
- Enhance `[GEOM-PANEL]` to include X/Y coordinates.
- Add `[GEOM-MOUSE]` logging to `dockMouseArea` whenever its boundaries change.
- Add `[GEOM-INTER-ICON-GAP]` logging inside the mouse hover logic to explicitly measure and expose the exact size and location of gaps between icons where no icon hover is registered.
- Add `[GEOM-REGION]` logging for the Wayland input region updates.

### D. Separator Component (`src/qml/components/Separator.qml` or inline)
- Locate the Separator logic (either inline in `main.qml` or in a component).
- Add `[GEOM-SEPARATOR]` logging for its dimensions.

## 3. Execution
- Use `replace` to update `GEMINI.md`.
- Use `replace` to update `main.qml` and `AppIcon.qml`.
- Locate and update the Separator component.
