# Global Positioning Logic Research

## Overview
This document details how Krema calculates absolute global screen coordinates for dock items, specifically focusing on `m_itemGlobalX` and `m_itemGlobalY`. These variables are critical for anchoring external surfaces (like the preview popup) to specific dock icons.

## Mathematical Principle
The calculation follows an "Additive Coordinate Reconciliation" strategy:
1. **Local Input**: Receives coordinates relative to the dock's own coordinate system.
2. **Dock Offset**: Applies the dock's screen-relative offset (position of the dock window relative to the monitor).
3. **Screen Reconciliation**: Adjusts for the current edge placement (Top, Bottom, Left, Right).

## Logic Breakdown (`PreviewController::showPreview`)

### Coordinate Calculation
The controller calculates global position by combining local coordinates with screen geometry and the dock's current anchor point.

#### Logic per Edge:
- **Bottom**: `GlobalY = LocalY + (ScreenH - DockH - Margin)`; `GlobalX = LocalX + (ScreenW - DockW) / 2.0`
- **Top**: `GlobalY = LocalY + Margin`; `GlobalX = LocalX + (ScreenW - DockW) / 2.0`
- **Right**: `GlobalX = LocalX + (ScreenW - DockW - Margin)`; `GlobalY = LocalY + (ScreenH - DockH) / 2.0`
- **Left**: `GlobalX = LocalX + Margin`; `GlobalY = LocalY + (ScreenH - DockH) / 2.0`

### Usage in Positioning (`recalcContentPosition`)
Once global coordinates are established, they serve as the anchor point for the preview popup:
- **Centering**: The popup is centered on the icon's midpoint: `popupCenter = Global + Size / 2.0`.
- **Offset/Padding**: `preview_margin` is added to place the popup clearly away from the dock item (Standard: 12px).

## Critical Rules
- **Absolute Sync**: Always use `m_dockView->screen()->geometry()` for reference, as the dock may move across virtual monitor setups.
- **Dock Centering**: Horizontal docks are assumed to be centered on the screen (hence the `(ScreenW - DockW) / 2.0` offset).
- **Floating Margin**: Must include `dockMargin` (retrieved from `dockView->floatingPadding()`) to ensure absolute coordinate precision when the dock is floating away from the screen edge.
