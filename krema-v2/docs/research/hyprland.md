# Hyprland Support Research

## Overview
Hyprland is a tiling Wayland compositor that supports the `wlr-layer-shell-unstable-v1` protocol. Unlike KWin/Plasma, it does not use `libtaskmanager` for window management information.

## Protocol Support
- **Layer Shell:** Use `LayerShellQt`. It works on Hyprland since Hyprland implements the standard `wlr-layer-shell` protocol.
- **Task Management:**
  - **Socket 1 (.socket.sock):** Used for commands (`hyprctl`).
  - **Socket 2 (.socket2.sock):** Used for real-time events (window opened, closed, active changed).
  - **IPC JSON:** `hyprctl clients -j` provides a comprehensive list of all windows in JSON format.

## Implementation Details (v2.0)
- **HyprlandProtocol:** Implemented using `LayerShellQt`, mirroring the KWin implementation but with Hyprland-specific logging and environment detection.
- **HyprlandTaskProvider:** 
  - [CURRENT] Initial implementation uses `hyprctl clients -j` for polling/initial sync.
  - [TODO] Subscribe to `socket2` for real-time updates to avoid polling and reduce latency.
  - [TODO] Improve icon resolution (currently just using `initialClass`).
  - [TODO] Implement `activateTask` using `hyprctl dispatch focuswindow address:<addr>`.

## Environment Detection
The dock detects Hyprland by checking:
1. `KREMA_FORCE_HYPRLAND` environment variable.
2. `HYPRLAND_INSTANCE_SIGNATURE` environment variable (set automatically by Hyprland).

## Known Issues & Notes
- **Input Regions:** Standard QRegion masks work correctly on Hyprland via Layer Shell.
- **Exclusivity:** `setExclusiveZone` works as expected, pushing tiled windows away.
