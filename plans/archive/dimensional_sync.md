# Plan: Dimensional Sync Protocol (Rule 7)

## 1. Objective
Implement the 7th mandate, "The Dimensional Sync Protocol," allowing users to toggle whether the panel thickness scales proportionally when the base icon size is adjusted. This maintains the exact visual overflow ratio (the "Top-Down Reveal") during scaling operations.

## 2. Proposed Changes

### A. Configuration (`src/config/krema.kcfg`)
- Add a new `Bool` setting `SyncPanelThickness` to the `Sizing` or `General` group. Default value: `true`.

### B. Settings UI (`src/qml/settings/PanelPage.qml`)
- Add a `KremaSwitch` for "Proportional Scaling Lock (Dimensional Sync)" underneath the Panel Thickness slider.
- This switch toggles `DockSettings.syncPanelThickness`.

### C. Scaling Logic (`src/qml/settings/IconsPage.qml`)
- Update the `iconSizeSlider`'s `onMoved` event.
- If `DockSettings.syncPanelThickness` is true, calculate the current ratio between `DockSettings.panelHeight` and the `_maxEnv` before applying the new icon size.
- Calculate the new `_maxEnv` based on the newly selected icon size.
- Multiply the old ratio by the new `_maxEnv` to get the new `panelHeight`.
- Ensure the new `panelHeight` never exceeds the new `_maxEnv` (Mathematical Subordination, Rule 6 & 7).

## 3. Execution
- Use `replace` to update `krema.kcfg`.
- Use `replace` to update `PanelPage.qml`.
- Use `replace` to update `IconsPage.qml`.
- Build and test the synchronization lock dynamically.