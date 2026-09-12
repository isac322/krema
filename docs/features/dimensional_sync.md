# Feature: Dimensional Sync Protocol

## Feature Overview
**Goal:** Allow users to lock the proportions of the dock so that resizing the base icons automatically scales the panel thickness, preserving the exact visual overflow ratio (Top-Down Reveal) at all sizes.

**Primary Files:**
- `src/qml/settings/PanelPage.qml` (Sync Toggle & Max Envelope Math)
- `src/qml/settings/IconsPage.qml` (Slider Math & Ratio Sync)
- `src/config/krema.kcfg` (Configuration Schema)

---

## Version History

### v1.0 - The Proportional Lock
**Status:** Approved and Working
**Reasoning:** Implemented **Rule 7: The Dimensional Sync Protocol** from `ARCHITECTURE Mandate.md`. This ensures that users who prefer a specific visual look don't have to manually adjust two separate sliders when they just want larger icons.

### v1.1 - Vector Scaling & Proactive Clamping
**Status:** Approved and Working
**Reasoning:** Perfected the synchronization logic by making the **Floor Padding** dynamic (25% of icon size). This turned the dock into a true scalable vector graphic. Furthermore, implemented **Rule 6 (Proactive Clamping)** which automatically pushes subordinate sliders (Panel Thickness, Radius) to their mathematical maximums in real-time, completely eliminating "UI Blindness" and dead zones.
