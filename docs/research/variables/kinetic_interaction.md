# Kinetic & Interaction Variables

## Overview
Variables controlling animations, hit-test logic, and kinetic responses.

## Registry
| Name | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- |
| `_zoomIntensity` | `DockView` | Kinetic transition smoothing | `DockView` | Rule 19 |
| `hoveredIndex` | `DockView` | ID of current icon under cursor | `DockView`, `Shell` | Rule 3 |
| `virtualCenter` | `DockView` | Slot center mathematical point | `DockView` | Rule 3 |
| `zoomSigma` | `DockView` | Parabolic wave influence width | `DockView` | Rule 19 |
| `m_zoomOverflowHeight` | `DockView` | Extra catch zone for input | `DockView` | Rule 12 |
| `currentScale` | `DockView` | Animation scale factor (1.0-max) | `DockView` | Rule 19 |
| `zoomFactor` | `DockView` | Magnification multiplier | `DockView` | Rule 19 |
| `itemCenterX` | `DockView` | Icon growth origin | `DockView` | Rule 19 |
| `_bounceTarget` | `DockView` | Launch animation pixel distance | `DockView` | Rule 19 |
| `_attentionBounceTarget` | `DockView` | Attention jump pixel distance | `DockView` | Rule 19 |
