# Feature: Dynamic Indicator Offset

## Feature Overview
**Goal:** Allow users to adjust the distance between the indicators and the icon unit while strictly preserving visual symmetry and grounding stability.

**Primary Files:**
- `src/qml/AppIcon.qml` (Elastic Slot Math)
- `src/qml/settings/IconsPage.qml` (UI Slider)

---

## Version History

### v1.0 - The Elastic Symmetry Implementation
**Status:** Approved and Working
**Reasoning:** Implemented a functional "Indicator Offset" slider that upholds **Rule 2: The Illusion of Symmetry**. It uses an "Elastic Slot" that expands proportionally to icon size.

#### 1. The Proportional Shift
The expansion is tied to the icon size (25% max expansion), ensuring visual consistency across all dock scales.

#### 2. Proactive Synchronization
When the user adjusts the offset, the UI now proactively clamps the Panel Thickness and Corner Radius to their new mathematical maximums, fulfilling **Rule 6 (UI Blindness Prevention)**.
