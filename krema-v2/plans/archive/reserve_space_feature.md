# Plan: Implement Dual Reserve Space Modes

## 1. Background & Motivation
With the introduction of the "Two Worlds" (Visual Overflow) architecture, the dock panel can shrink smaller than the icons. The current Wayland `exclusiveZone` uses the panel thickness. This causes maximized windows to clip through overflowing icons. We need to allow the user to choose whether to reserve space based on the Panel (modern/macOS style) or the Content Envelope (traditional/no overlap).

## 2. Proposed Changes

### A. Configuration (`src/config/krema.kcfg`)
- Add a new integer setting: `reserveMode`.
- Values: `0` (Panel Size), `1` (Icon/Content Envelope Size).
- Default: `1` (Content Size, to prevent overlap by default).

### B. C++ Backend (`src/shell/dockvisibilitycontroller.h` & `.cpp`)
- Add a `Q_PROPERTY` for `contentHeight` (to track the unzoomed icon envelope).
- Add a `Q_PROPERTY` for `contentWidth` (for vertical docks).
- Add `m_reserveMode` state and a setter.
- Update `evaluateVisibility()`:
  - If `m_reserveMode == 0`, use `m_panelHeight` (or width).
  - If `m_reserveMode == 1`, use `m_contentHeight` (or width).

### C. C++ Wiring (`src/shell/dockshell.cpp`)
- Bind the new `KremaSettings::reserveMode()` to `DockVisibilityController`.
- Ensure it updates when the setting changes.

### D. QML Frontend (`src/qml/main.qml`)
- Bind the new `contentHeight` and `contentWidth` properties in `DockVisibilityController` to `dockRow.animatedContentHeight` and `dockRow.animatedContentWidth`.

### E. Settings UI (`src/qml/settings/VisibilityPage.qml`)
- Add a row of selectable buttons (similar to the Visibility Mode selection) under the existing "Reserve Space" switch to select the mode ("Panel Background" vs "Icon Extents").
- Only enable this selection if "Reserve Space" is enabled.

## 3. Execution Steps
1. Modify `krema.kcfg`.
2. Update `dockvisibilitycontroller.h` and `.cpp`.
3. Update `dockshell.cpp`.
4. Update `main.qml` to feed the content dimensions.
5. Update `VisibilityPage.qml`.
6. Build and verify.