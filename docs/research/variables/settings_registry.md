# Krema Settings Registry

## Overview
This registry tracks the two-tier configuration system in Krema: Global (`KremaSettings`) and Per-Screen (`ScreenSettings`).

## Authority Structure
- **KremaSettings (Singleton)**: Stores global system-wide defaults, shortcuts, and pinned application states. These values are persisted to `kremarc`.
- **ScreenSettings (Instance)**: Stores per-monitor geometric overrides (Icon Size, Edge, Zoom). These are persisted as `[Screen-<name>]` groups within the same `kremarc` config file.

## Registry
| Setting | Scope | Authority | Persistence Key |
| :--- | :--- | :--- | :--- |
| `IconSize` | Per-Screen | `ScreenSettings` | `Screen-<name>/IconSize` |
| `MaxZoomFactor` | Per-Screen | `ScreenSettings` | `Screen-<name>/MaxZoomFactor` |
| `Edge` | Per-Screen | `ScreenSettings` | `Screen-<name>/Edge` |
| `Floating` | Per-Screen | `ScreenSettings` | `Screen-<name>/Floating` |
| `PinnedLaunchers`| Global | `KremaSettings` | `General/PinnedLaunchers` |
| `IndicatorOffset` | Global | `KremaSettings` | `General/IndicatorOffset` |

## Architectural Note
The `DockView` acts as the mediator between these two tiers. When an icon or component requests a geometry-related setting (like `iconSize`), the `DockView` MUST resolve it by checking the per-screen `ScreenSettings` override first, falling back to `KremaSettings` only if no per-screen override exists.
