# NotificationTracker Variables

## Overview
Tracks application notification status, enabling features like badge counters and attention animations.

## Registry
| Name | Singleton? | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `revision` | Yes | `NotificationTracker` | Change counter for state | `DockContextMenu` | Notifications |
| `dndActive` | Yes | `NotificationTracker` | Do Not Disturb status | `DockContextMenu` | Interaction |
