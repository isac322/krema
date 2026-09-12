# Orbit Suffocation (1.0x Dead Zone)

## [2026-05-12] Analysis
- **Problem:** At 1.0x and 1.1x zoom scales, the hover trigger zone was unhittable ("click-through").
- **Cause:** The `enterOrbit` calculation was static (`(iconSize * 0.5) + 5`). At small zoom scales, the interaction orbit fell physically inside the window bounding box and behind the large `liveEditMode` visual radius, meaning the cursor exited the sensor zone before it could even enter the zoom trigger zone.
- **Attempted Resolution:** Dynamic Orbit (`(iconSize * 0.5 * DockSettings.maxZoomFactor) + 5`). The interaction threshold now scales proportionally with the zoom capability.
- **Outcome:** **FAILED**. The dynamic orbit math did not fix the "click-through" at 1.0x or the inaccuracy on the first entry while Settings is open.

## [2026-05-12] Discarded Attempt: Stability Fallbacks
- **Strategy:** Attempted to implement mathematical fallbacks for flooring units in `main.qml` to prevent surface shrinkage.
- **Reasoning:** Hypothesis was that asynchronous icon loading caused the input region to shrink to 0px briefly.
- **Outcome:** **REVERTED.** The fallback variables were deemed unnecessary and did not fix the click-through. The focus shifted back to the Wayland surface expansion logic.
