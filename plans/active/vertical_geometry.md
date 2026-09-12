# Plan: Vertical Geometry & Proportional Constraints

## 1. Objective
Define the mathematical rules for the vertical (cross-axis) layout of the dock. This formalizes how the Panel and Islands handle thickness, visual overflow, dimensional synchronization, and proportional corner radius scaling.

## 2. The Panel vs. Island Relationship
- **The Panel (Tier 1):** Defines the base `Thickness` (the physical Wayland boundary).
- **The Island (Tier 2):** Calculates its own `IslandHeight` based on its internal items + padding.
- **Island Overflow:** If `IslandHeight > PanelThickness`, the Island (and its background pill) visually overflows the Panel's upper boundary. Because the internal math is relative to the Island's floor, symmetry is never broken during overflow.

## 3. Dimensional Sync (Panel Feature)
- **Toggleable:** Users can enable `SyncPanelThickness`.
- **Logic:** When the base `targetContentSize` changes (e.g., from 48px to 64px), the C++ engine recalculates the `PanelThickness` to maintain the exact ratio of visual overflow, keeping the "Top-Down Reveal" consistent.

## 4. Proportional Corner Radius (Universal Rule)
- **Ratio-Based Math:** Corner radius is not stored as a fixed pixel value, but as a `RadiusRatio` (e.g., 0.5 for a perfect pill, 0.2 for slightly rounded).
- **1:1 Scaling:** When the Panel resizes, the corner radius scales 1:1 mathematically: `ActualPanelRadius = PanelThickness * RadiusRatio`.
- **Island Synchronization:** The Islands use the exact same `RadiusRatio` against their own height: `ActualIslandRadius = IslandHeight * RadiusRatio`. This guarantees visual cohesion between the Panel and overflowing Islands at any scale.

## 5. Execution
- Update `ARCHITECTURE Mandate.md` to encode these vertical constraints (Overflow, Dimensional Sync, Proportional Radius).
