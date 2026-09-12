# Feature: Dynamic Indicator Proportions

## Feature Overview
**Goal:** Ensure that status indicators scale naturally with the icons, maintaining a cohesive visual ratio at all sizes (from 24px up to 150px).

**Primary Files:**
- `src/qml/AppIcon.qml`

---

## Version History

### v1.0 - The 10% Golden Ratio
**Status:** Approved and Working
**Reasoning:** Replaced hardcoded 4px dots with a dynamic formula (10% icon size) to fulfill **Rule 6**.

### v1.1 - Pure Proportional Gap
**Status:** Approved and Working
**Reasoning:** Refined the indicator gap to scale linearly with the icon size, ensuring a tight visual unit at all scales.

### v1.2 - Elastic Symmetry Alignment
**Status:** Approved and Working
**Reasoning:** Updated the gap baseline to **12.5%** of icon size (yielding 6px for standard docks). Integrated this proportional gap into the **Elastic Slot** math, ensuring that as the gap scales, the dock panel expands symmetrically to preserve the **Empty Gap Rule (Rule 2)**.
