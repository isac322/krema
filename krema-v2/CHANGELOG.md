# Krema Dock: Changelog

All notable changes to the Krema Dock project will be documented in this file.

## [Unreleased] - 2026-05-19

### Added
- **Hyprland First-Class Support:** Implemented `HyprlandProtocol` and `HyprlandTaskProvider` enabling native operation on Hyprland compositors.
- **Dynamic Environment Detection:** The dock now automatically detects the active compositor (Hyprland vs KWin) and selects the appropriate protocol and task provider at startup.
- **Hyprland Task Integration:** Initial task management for Hyprland using IPC (`hyprctl`) for window listing and activation.
- **Protocol Abstraction:** Refactored shell integration into a modular `IProtocol` and `ITaskProvider` system for better extensibility.

### Internal
- Added `KREMA_FORCE_HYPRLAND` environment variable for debugging.
- Integrated `LayerShellQt` into the Hyprland build path.
- Updated `ITaskProvider::TaskEntry` with `operator==` for efficient model comparison.

## [Unreleased] - 2026-05-18

### Added
- **Recursive Debugging System:** Implemented visual overlays and color-coded terminal logs for all 3 tiers (Panel, Island, Item) using `--debug-geom`.
- **Hit-Test Diagnostic Suite:** Formalized the `--debug-hit` flag with Parabolic Wave intensity logging and "Stable Virtual Origin" visual markers (Rule 3).
- **Discrepancy Alarm:** Added a self-validating "Truth vs. Reality" check that flags 1px+ divergence between C++ intended coordinates and QML visual positions (Rule 14).
- **HOVER_BUFFER:** Introduced a 200px invisible interaction margin to "catch" the mouse early and prevent the "Small Target" bug.
- **Trigger Telemetry:** Added `[PERF-LATENCY]` and `[TRIGGER]` logs to measure interaction activation speed.

### Fixed
- **Velocity Bug:** Resolved issue where interaction only triggered at high mouse speeds by prioritizing `HoverHandler` over `TapHandler` and throttling `setInputRegion` updates to avoid Wayland event saturation.
- **Ghost Boot Delay:** Eliminated the hardcoded 2-second reveal delay by removing unreliable Wayland Y-coordinate checks.
- **Wayland Input Mask Hot-Loop:** Throttled input region updates to a 2px delta threshold to improve protocol performance.

### Internal
- Registered `g_debugHit` and exposed it as `isHitDebug` to the QML engine.
- Documented `HOVER_BUFFER` in the new Variable Research Registry.
