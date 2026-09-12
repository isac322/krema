# Plan: Implement Pixel Hugger Debug Visual

## 1. Objective
Add a visual aid to help the user identify the exact mathematical boundaries of each icon. This will be a 60% transparent magenta rectangle positioned directly behind each icon, visible only when the `--debug-geom` flag is active.

## 2. Proposed Changes
- **`src/qml/AppIcon.qml`**:
  - Inside the `iconImage` Item, add a new `Rectangle` component.
  - Set `z: -1` to ensure it renders behind the icon.
  - Set `anchors.fill: parent` to hug the icon's exact pixels.
  - Set `color: "magenta"` and `opacity: 0.4` (achieving 60% transparency).
  - Bind `visible: _debugGeom` to control its display via the debug flag.
  - Add a small `QQC2.Label` in the center of the rectangle to display the real-time width and height (e.g., "48x48").
  - Set `enabled: false` and `Accessible.ignored: true` to prevent it from interfering with mouse interaction or accessibility.

## 3. Execution
- Exit Plan Mode.
- Use `replace` to update `AppIcon.qml`.
- Ask the user to run the app with `--debug-geom` to verify.
