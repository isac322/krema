# Settings Authority Boundary

## Overview
This document defines the clear boundary between **Global (System-Wide)** and **Per-Screen (Local Geometry)** configuration settings. This ensures the dock handles multi-monitor configurations correctly without confusing local overrides with global defaults.

## Authority Matrix

| Feature | Scope | Authority Node | Persistence Key (kremarc) |
| :--- | :--- | :--- | :--- |
| **Icon Size** | Per-Screen | `ScreenSettings` | `Screen-<name>/IconSize` |
| **Zoom Factor** | Per-Screen | `ScreenSettings` | `Screen-<name>/MaxZoomFactor` |
| **Panel Edge/Orientation** | Per-Screen | `ScreenSettings` | `Screen-<name>/Edge` |
| **Panel Height** | Per-Screen | `ScreenSettings` | `Screen-<name>/PanelHeight` |
| **Corner Radius** | Per-Screen | `ScreenSettings` | `Screen-<name>/CornerRadius` |
| **Floating Status** | Per-Screen | `ScreenSettings` | `Screen-<name>/Floating` |
| **Pinned Launchers** | Global | `KremaSettings` | `General/PinnedLaunchers` |
| **DND/Global Shortcuts** | Global | `KremaSettings` | `General/DND` |
| **Indicator Offset** | Global | `KremaSettings` | `General/IndicatorOffset` |
| **Animation Durations**| Global | `KremaSettings` | `General/AnimationDuration` |

## Authority Logic
- **`KremaSettings` (Singleton)**: Acts as the "Master Database". It holds the global system defaults and the state for non-geometric application features.
- **`ScreenSettings` (Instance)**: Acts as the "Per-Screen Override". It stores localized geometric data (`IconSize`, `Edge`, etc.) that must be unique for each monitor setup.

## Architectural Rule
1. **The Mediator Principle**: UI components (`AppIcon`, `Panel`, etc.) must NEVER bind directly to `KremaSettings` (the singleton) for geometry or per-screen configuration.
2. **Mediator Access**: UI components MUST request per-screen variables from their immediate parent (typically `DockView` or `DockShell`), which resolves the priority chain: `ScreenSettings` (Override) -> `KremaSettings` (Global Fallback).
3. **Persistence**: Global settings are stored in top-level config groups, while per-screen overrides MUST be prefixed with `Screen-<name>/` to ensure they do not collide with global defaults.
