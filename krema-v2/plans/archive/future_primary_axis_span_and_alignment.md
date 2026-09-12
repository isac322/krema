# Future Plan: Customizable Panel Primary-Axis Span & Alignment

## 1. Objective
Enable users to control the span of the dock along its **Primary Axis (Length)**. This allows the dock to transcend its "Fit Content" limitation and support fixed widths, minimum widths, or full-screen expanses, as defined by the user's specific workflow requirements.

## 2. Mathematical Foundation
This feature extends **Rule 8 (Omnidirectional Axis)** by decoupling the "Length" axis from the "Icon Count" while maintaining the dock's internal stability and centering.

### Primary Axis Modes:
1. **Fit Content (Current):** Length = `iconCount * slotSize`.
2. **Fixed Width:** Length = `UserDefinedValue` (e.g., exactly 600px).
3. **Minimum Width:** Length = `max(UserDefinedValue, iconCount * slotSize)`.
4. **Fill Screen:** Length = `screenWidth - (2 * globalMargin)`.

## 3. Proposed Configuration (KConfig)
- `PanelLengthMode`: Enum (0=Fit, 1=Fixed, 2=Min, 3=Fill).
- `CustomPanelLength`: Int (User-defined pixel value).
- `PanelAlignment`: Enum (0=Center, 1=Start, 2=End).

## 4. Implementation Strategy

### A. main.qml (Geometric Refactor)
Update `_actualContentWidth` (and height for vertical docks) to use the new mode logic:
```qml
property real _actualContentLength: {
    switch (DockSettings.panelLengthMode) {
    case 0: return unscaledContentSize + padding;
    case 1: return DockSettings.customPanelLength;
    case 2: return Math.max(DockSettings.customPanelLength, unscaledContentSize + padding);
    case 3: return screenLength - (floatingPadding * 2);
    }
}
```

### B. Alignment Logic
If the panel is wider than the icon row, the `dockRow` container must respect `DockSettings.panelAlignment`:
- **Center:** `(panelWidth - rowWidth) / 2`
- **Start:** `padding`
- **End:** `panelWidth - rowWidth - padding`

## 5. UI Integration (PanelPage.qml)
Add a new section "Length & Alignment" containing:
- A selector for the 4 modes.
- A "Custom Length" slider (only visible in Fixed/Min modes).
- An alignment picker (Left, Center, Right) for horizontal docks.

## 6. Success Criteria
- The dock remains grounded and symmetric regardless of length.
- Maximized windows (Reserve Space) correctly respect the new extended length.
- The corner radius logic remains clamped to half the thickness (Rule 6 preserved).
