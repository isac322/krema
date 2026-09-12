# The Ghost Grid Protocol (virtualCenter)

## [2026-05-12] Introduction: The Stable Anchor

### Definition
The `virtualCenter` is a mathematical "Ghost Grid" that defines the ideal, unzoomed center of an icon slot within the dock. Unlike the visual `x` or `y` coordinates, which change dynamically during zoom animations, the `virtualCenter` remains constant regardless of the icon's current scale or state.

### The Problem: Recursive Desync
Previously, icons were positioned recursively (e.g., `Icon[n].x = Icon[n-1].x + Icon[n-1].width`). This created a "Chain Dependency" where:
1.  **Startup Race:** If the first icon hadn't calculated its width yet, every subsequent icon would be positioned incorrectly, leading to "Huge Gaps" on launch.
2.  **Hover Fix:** Hovering forced a zoom update, which "kicked" the binding chain and resolved the gaps, but this was a symptom-level fix, not a root-cause fix.
3.  **Binding Loops:** The recursive chain often fought with the panel's implicit size, causing performance-degrading binding loops.

### The Solution: The Ghost Grid (virtualCenter)
By calculating the center of every icon slot independently of its neighbors, we create a "Stable Anchor."

**Calculation Logic:**
- `slotSize = iconSize + spacing`
- `totalLength = (count * slotSize) - spacing`
- `startPoint = (DockLength / 2) - (totalLength / 2)`
- `virtualCenter = startPoint + (index * slotSize) + (iconSize / 2)`

### Current Roles
1.  **Gaussian Zoom (Gaussian Wave):** Used as the source of truth for distance-to-mouse calculations. By using a stable center, we avoid the "Jitter Loop" where zooming an icon changes its position, which then changes its distance to the mouse.
2.  **Absolute Sync:** Used to align the interaction boundaries 1:1 with the visual icon pixels.

### Proposed Evolution: Non-Recursive Layout
We will transition the dock's layout engine to use `virtualCenter` as the primary positioning authority. Instead of pinning icons to each other, each icon will "snap" to its `virtualCenter` offset by its own half-width:
`x = virtualCenter - (width / 2)`
