# Variable Registry

## Overview
This directory serves as the centralized registry for tracking variables in Krema.

## Registry Index

### System Hubs (Singletons)
- [DockModel Variables](dock_model.md)
- [DockSettings Variables](dock_settings.md)
- [KremaDebug Variables](krema_debug.md)
- [NotificationTracker Variables](notification_tracker.md)

### Instance Variables
- [DockView Variables](dock_view.md)
- [DockShell Variables](dock_shell.md)
- [VisibilityController Variables](visibility_controller.md)
- [ScreenSettings Variables](screen_settings.md)
- [Preview Variables](preview_popups_and_labels.md)
- [Global Positioning Variables](global_positioning_logic.md)
- [Constitutional Unit Variables](constitutional_units.md)
- [Kinetic & Interaction Variables](kinetic_interaction.md)
- [Shadow Stack Variables](shadow_stack.md)
- [TaskIconProvider Variables](task_icon_provider.md)
- [Settings Authority Boundary](settings_authority.md)
- [Settings Registry](settings_registry.md)

## How to document a new variable:
1. Create a new markdown file in this directory or add it to an existing category file.
2. For each variable, specify:
    - **Name**
    - **Owner** (e.g., C++ Class or QML Component)
    - **Purpose** (e.g., animation timing, geometry calculation)
    - **Consumers** (e.g., PreviewController, DockView)
    - **Architectural Link** (e.g., Rule 17, Rule 20)
