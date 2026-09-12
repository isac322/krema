# VisibilityController Variables

## Overview
Variables controlling dock visibility, animation delays, and interaction locking.

## Registry
| Name | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- |
| `dockVisible` | `DockVisibilityController` | Current visibility state | `Controller`, `QML` | Lifecycle |
| `mode` | `DockVisibilityController` | Visibility behavior (Auto, Dodge, etc.) | `Controller`, `Settings` | Interaction |
| `interacting` | `DockVisibilityController` | Prevents auto-hide while active | `Controller`, `Shell` | Interaction |
| `liveEditMode` | `DockVisibilityController` | Editing mode for dock config | `Controller`, `QML` | UI/UX |
| `panelRect` | `DockVisibilityController` | Current screen geometry | `Controller`, `Blur` | Geometry |
