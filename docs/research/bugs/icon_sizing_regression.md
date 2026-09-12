# Icon Sizing Regression Research

## Overview
An anomaly was reported where a single icon appears significantly smaller than others in the dock after a restart. The debug geometry logs (`--debug-geom`) currently show all icons initializing at `Scaled:52x52`, suggesting the discrepancy might arise post-initialization or during a specific state transition.

## Analysis of Geometry Logic
- **DockView::updateSize()**: Calculates the dock surface dimension based on `iconSize`, `baseIconSize` (fixed at 48), and `maxZoomFactor`.
- **AppIcon.qml**: 
    - Uses `iconSize` (from `DockSettings`) as the base.
    - Applies `currentScale` (animated zoom).
    - Contains logic for `_smartLauncherItem` (progress bar icons), which might be interfering with local coordinate scaling.

## Potential Root Causes
1. **Normalization Race**: The icon provider clear/cache-bump logic might be failing to trigger a re-render for specific types of icons (like electron-based launchers) on restart.
2. **Missing Normalization**: Electron apps (e.g., `neshi-desktop-electron`) are explicitly flagged in logs with missing images, suggesting their icon provider response is inconsistent.
3. **Stale Zoom Factor**: If an icon is initialized before the `zoomFactor` binding from `DockSettings` is fully settled, it might stay at base `currentScale = 1.0` instead of animating to the target zoom.

## Current Registry
| Variable | Owner | Purpose | Consumers |
| :--- | :--- | :--- | :--- |
| `iconSize` | `ScreenSettings` | Base sizing for icons | `DockView`, `AppIcon` |
| `currentScale` | `AppIcon` | Zoom-animated multiplier | `AppIcon` |
| `baseIconSize` | `DockView` | Internal reference (fixed 48) | `DockView` |

## Next Steps
- Verify if the "small" icon is specifically an Electron-based launcher or a standard desktop entry.
- Audit `AppIcon.qml` `onZoomFactorChanged` and `onComponentCompleted` bindings to ensure `currentScale` initializes correctly even if settings are delayed.
