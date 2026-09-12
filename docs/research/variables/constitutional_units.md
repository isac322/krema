# Constitutional Unit Variables (5-Unit Stack)

## Overview
Variables representing the "5-Unit Stack" (Rule 17). These are fundamental to the dock's vertical and horizontal stack geometry, defining the "Safe Floor" and "Ceiling" of the dock surface.

## Architectural Role
These units enforce Rule 17 (Interaction Flooring Constitution). They ensure the dock maintains a consistent vertical stack regardless of icon size or zoom state. By anchoring calculations to these defined units, the dock avoids "layout drift" and ensures that indicators, icons, and padding remain perfectly aligned during animations.

## Registry
| Name | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- |
| `_unitPanelFloor` | `DockView` | Internal floor padding | `DockView`, `BackgroundStyle` | Rule 17 |
| `_unitIndicator` | `DockView` | Visual indicator height | `WidgetModel`, `DockView` | Rule 17 |
| `_unitIconIndicatorGap` | `DockView` | Gap between indicator and icon | `DockView`, `Shell` | Rule 17 |
| `_unitIcon` | `DockView` | Dynamic icon size (zoomed) | `DockView`, `AppIcon` | Rule 17 |
| `_unitPanelCeiling` | `DockView` | Internal ceiling padding | `DockView`, `BackgroundStyle` | Rule 17 |
| `_unitIconBaseOffset` | `DockView` | Total distance from panel edge | `DockView` | Rule 17 |

## Mathematical Intent
The stack is designed as an additive chain where each unit provides a specific offset from the previous, ensuring that the dock container can calculate its total thickness dynamically by simply summing these units and applying current zoom factors.
