# Feature: Two Worlds Grounding

## Feature Overview
**Goal:** Establish an unbreakable physical anchor chain connecting the icons to the physical screen edge, ensuring the dock is perfectly stable regardless of its size, overflow state, or floating properties.

**Primary Files:**
- `src/qml/main.qml` (The Outside World)
- `src/qml/AppIcon.qml` (The Inside World)

---

## Version History

### v1.0 - The Geometric Gravity Implementation
**Status:** Approved and Working
**Reasoning:** Implemented **Rule 1: The Supreme Law of Gravity** and **Rule 5: The Top-Down Reveal** from `ARCHITECTURE Mandate.md`. This replaced verbose QML state machines with pure, high-efficiency declarative math.

#### 1. The Outside World (Screen Flooring)
Defined in `main.qml`, this layer handles the global position of the entire dock panel relative to the physical screen edge. It is completely isolated from the icon sizes.
- **The Gap:** The distance from the screen edge is exclusively managed by `_screenFlooring` (derived from the `floating_offset` setting).
- **Flush Anchoring:** The `dockRow` (which holds all icons) is geometrically anchored *flush* (0px offset) to the screen-facing edge of the panel.

#### 2. The Inside World (Dock Flooring)
Defined in `AppIcon.qml`, this layer handles the internal layout of the icon and its indicators.
- **The Fixed Floor:** The base of the indicators is permanently anchored 12px (`_dockFloorPadding`) away from the panel's interior edge.
- **The Chain:** Screen Edge → Floating Padding → Panel Edge (Flush) → 12px Padding → Indicators → Icon.

#### 3. The Top-Down Reveal
Because the `dockRow` is flush to the panel edge, shrinking the panel height only moves its inner "Ceiling" downwards. This creates a "Fixed Floor, Moving Ceiling" effect, allowing the panel to smoothly uncover the icons from the top down, maintaining perfect grounding during visual overflow.
