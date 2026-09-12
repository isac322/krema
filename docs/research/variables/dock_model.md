# DockModel Variables

## Overview
Variables managing the dock's task manager, pinned launchers, and virtual desktop context.

## Registry
| Name | Singleton? | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `tasksModel` | Yes | `DockModel` | Underlying task management | `DockModel` | TaskManager API |
| `pinnedLaunchers` | Yes | `DockModel` | List of pinned application IDs | `DockModel`, `Settings` | Pinned Apps |
| `virtualDesktopMode` | Yes | `DockModel` | Desktop filtering mode | `DockModel`, `QML` | Workspace |
| `currentDesktop` | Yes | `DockModel` | Current desktop ID | `DockModel`, `QML` | Workspace |
