# DockShell Architectural Hub

## Overview
`DockShell` is the primary orchestrator for a single dock instance. It acts as the "root" object that wires together the UI, business logic, and configuration. Its pervasive presence in the codebase is due to its ownership of the entire dock lifecycle.

## Architectural Role
- **Instance Owner**: Manages the lifecycle of a single dock (per-screen).
- **Subsystem Hub**: Holds the unique instances of `DockView` (UI), `DockActions` (Commands), `DockContextMenu` (Interaction), `PreviewController` (Popups), and `SettingsWindow` (Configuration).
- **Wiring Logic**: Responsible for the complex signal connections between the settings system and dock components (e.g., re-applying background styles, updating visibility delays, resizing surfaces).
- **Multi-Monitor Bridge**: Designed to support future multi-monitor configurations where one `DockShell` instance is instantiated per screen.

## Key Properties & Responsibilities
- `initialize()`: Registers all QML singletons and connects signal handlers.
- `focusDock()`: Central entry point for keyboard interaction and surfacing the dock panel.
- `m_view`: Direct ownership of the `DockView` surface.
- `m_previewController`: Bridges popup logic and the dock surface.
