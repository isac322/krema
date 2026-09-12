# Product Quality Lessons

> This file MUST be referenced by the `product-quality` agent when writing test scenarios.
> User-reported bugs = scenarios this agent missed.

## Missed Bug Records

### [2026-02-22] Preview Input Region Full Screen Width (Bug 2+3)
- **User Symptom**: Clicking outside the preview popup is blocked, moving left/right doesn't close it.
- **Missed Scenario**: "Click outside preview area → event passed to other apps", "Move left/right out of preview → preview closes".
- **Root Cause**: `updateInputRegion()` used the full screen width. The "Hitbox = UI Size" principle was not applied.
- **Added Regression Scenario**: Test clicking outside the preview popup, test closing on left/right exit.

### [2026-02-23] Icon Fallback When Launching App from Launcher (0 Instances)
- **User Symptom**: Clicking to launch an app with 0 instances → preview only shows icon fallback.
- **Missed Scenario**: "Launch app from launcher → PipeWire live thumbnail".
- **Root Cause**: The `retryScreencast()` loop was only located in the group window path, making it unreachable after the early return for single windows.
- **Added Regression Scenario**: Verify PipeWire live thumbnail when launching an app from 0 instances (not icon fallback).

### [2026-02-22] New Instance PipeWire Fallback Loop (Bug 1)
- **User Symptom**: The 2nd instance of the same app shows an icon fallback.
- **Missed Scenario**: "2nd instance opened while preview is active → PipeWire live thumbnail".
- **Root Cause**: `ScreencastingRequest` is one-shot, but there was no retry logic on failure.
- **Added Regression Scenario**: Verify PipeWire live thumbnail for new instances (not icon fallback).

## Mandatory Regression Scenarios

(Cumulative — MUST be included in every test plan)

1. Dock icon hover zoom operates smoothly at 60fps.
2. Dock auto-hide works correctly when windows overlap.
3. Existing dock functions (zoom, click, drag) operate normally after adding new features.
4. Clicking outside (left/right) of the preview popup → event passed to other apps.
5. Moving mouse left/right out of the preview → popup closes when leaving the area.
6. Adding a new instance → shows PipeWire live thumbnail (not icon fallback).
7. Launching app from launcher (0 instances) → shows PipeWire live thumbnail (not icon fallback).
8. **[Safe Floor Check]**: The dock's baseline visibility/geometry variables (width, height, overflow) never drop to 0 or cause UI collapse during idle states.
9. **[Heterogeneous Logic Check]**: Non-standard items (Separators, Indicators) render and function correctly without crashing loops that assume all items are identical icons.
10. **[Messy Path Preservation]**: Unique/hardcoded app icon overrides (e.g., Steam, Electron apps, Neshi) retain their custom behaviors and do not revert to generic fallbacks.
