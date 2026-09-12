# Feature: Illusion of Symmetry

## Feature Overview
**Goal:** Create a perfectly balanced visual aesthetic where icons appear centered within the dock panel, while maintaining a rock-solid mathematical grounding to the screen edge.

**Primary Files:**
- `src/qml/AppIcon.qml`
- `src/qml/main.qml`

---

## Version History

### v1.0 - The Initial Empty Gap Rule
**Status:** Superseded by v2.0
**Reasoning:** Established the "Empty Gap Rule" to replace unstable icon-centering with matched air gaps.

### v2.0 - Universal Symmetry (The Absolute Standard)
**Status:** Approved and Working
**Reasoning:** Refined the symmetry math to be truly universal. Symmetry is now defined strictly as: **`Ceiling Air Gap == Floor Air Gap`**. 
- **The Math:** The `_dockCeilingPadding` (Top Air) now perfectly mirrors the `_dockFloorPadding` (Bottom Air, 25% of iconSize).
- **Independence:** This rule is now mathematically immune to changes in indicator size or the adjustable indicator gap. No matter how much you "push" the icon away from the dots, the top and bottom empty spaces remain 100% identical.
- **Rule 2 Mandate:** Formally updated in `ARCHITECTURE Mandate.md` as the supreme standard for visual balance.
