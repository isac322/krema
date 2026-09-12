# The Signal Desync (Zoom Settings)

## [2026-05-12] Analysis
- **Problem:** Updates to `MaxZoomFactor` from the Settings UI (running in a separate QML engine) were not visually reflected in the Dock.
- **Cause:** The Dock process's `ScreenSettings` layer dropped configuration change signals if it perceived local overrides or if QML's internal change-detection ignored visually identical states.
- **Attempted Resolution:** Implemented a direct `requestSync()` D-Bus/C++ bridge. An "Apply Changes" button manually triggers `KremaSettings::load()` on the Dock shell, forcing a memory refresh from disk.
- **Outcome:** **FAILED**. While memory synced, it did not resolve the core click-through or first-entry defects, leading to the Surface Layer Conflict hypothesis.
