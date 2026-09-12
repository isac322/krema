# Plan: Perfecting Dimensional Sync & Padding

## 1. Objective
Document the previously working Dimensional Sync feature. Furthermore, enhance the "Inside World" geometry by making the `_dockFloorPadding` dynamically scale with `iconSize`. This transforms the entire dock into a scalable vector graphic where the padding, indicators, and gaps all resize harmoniously with the icon.

## 2. Proposed Changes
- **`docs/features/dimensional_sync.md`**:
  - Document the goals, version history, and execution logic of the Dimensional Sync Protocol (Rule 7).
- **`src/qml/AppIcon.qml`**:
  - Replace `_dockFloorPadding: 12` with a proportional calculation: `Math.max(4, Math.round(iconSize * 0.25))`.
- **`src/qml/settings/PanelPage.qml`**:
  - Update the local `_floorPadding` variable to match the new proportional math based on `DockSettings.iconSize`.
- **`src/qml/settings/IconsPage.qml`**:
  - Update the `calculateMaxEnv` helper function to dynamically calculate the floor padding instead of using a hardcoded `12`.

## 3. Execution
- Exit plan mode.
- Create the documentation file in `docs/features/`.
- Apply the 3 string replacements across the QML files.
- Run `just build && ./build/dev/bin/krema --debug-geom` to verify.