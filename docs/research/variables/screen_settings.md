# ScreenSettings Variables

## Overview
Variables for per-screen configuration overrides, including sizing and aesthetic preferences.

## Registry
| Name | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- |
| `hasOverrides` | `ScreenSettings` | Detects per-screen custom config | `Settings` | Config |
| `iconSize` | `ScreenSettings` | Per-screen icon size override | `DockView`, `Settings` | Rule 17 |
| `edge` | `ScreenSettings` | Edge position per screen | `DockPlatform`, `Settings` | Layout |
| `visibilityMode` | `ScreenSettings` | Visibility behavior override | `VisibilityController` | Interaction |
| `backgroundStyle` | `ScreenSettings` | Visual style identifier | `DockView`, `QML` | Style System |
| `backgroundOpacity` | `ScreenSettings` | Opacity of dock surface | `DockView`, `QML` | Style System |
| `maxZoomFactor` | `ScreenSettings` | Zoom magnification level | `DockView`, `Settings` | Rule 19 |
| `floating` | `ScreenSettings` | Enables/disables dock floating | `DockView`, `Visibility` | UI/UX |
| `cornerRadius` | `ScreenSettings` | Corner radius override | `DockView`, `Blur` | Rule 6 |
| `panelHeight` | `ScreenSettings` | Height of the panel base | `DockView`, `Shell` | Rule 17 |
| `pinnedLaunchers` | `ScreenSettings` | Pinned app list storage | `DockModel` | Config |
| `separatorStyle` | `ScreenSettings` | Visual style of separators | `SeparatorDelegate` | UI/UX |
| `separatorOpacity` | `ScreenSettings` | Separator transparency | `SeparatorDelegate` | UI/UX |
| `separatorWidth` | `ScreenSettings` | Separator thickness | `SeparatorDelegate` | UI/UX |
