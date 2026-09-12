# Plan: The 3-Tier Architecture & Island Anatomy

## 1. Objective
Establish the definitive structural hierarchy for Krema Dock, transitioning from a monolithic, panel-centric "5-Unit Stack" to a fully modular, recursive "3-Tier Hierarchy". This supports both icons and widgets, enables multi-mode indicators, and ensures mathematically perfect repulsion physics.

## 2. Layout Modes (The Behavioral Toggle)
To prevent feature conflicts, the dock operates in two distinct modes:
- **Icon-Only Mode:** Square capsules. Parabolic Zoom is **enabled**. Folders support "Inline Expansion" (stretching the dock sideways) or "Pop-up Grid."
- **Icon-Label Mode:** Horizontal rectangle capsules. Parabolic Zoom is **locked OFF** to prevent jittery text. Folders use "Pop-up" expansion only.

## 3. The 3-Tier Hierarchy
- **Tier 1: Panel (Global Container)**
  - The invisible Wayland surface managing the `floating_offset` and overall `PanelSpan` (Fit, Fixed, Fill).
- **Tier 2: Island (Logical Group / BranchModule)**
  - A grouped module (e.g., Pinned Apps, System Tray).
  - Maintains its own `_islandPadding` (background pill) and aligns itself according to `PanelAlignment` (Start/Center/End).
  - Islands are separated by `_islandGap`.
  - **Master Zoom Lock:** Islands feature a `zoomEnabled` flag. If false, it acts as a master lock, overriding all child items.
- **Tier 3: Item (Atomic Capsule / BaseModule)**
  - The single unit (Icon, Label, Widget, Folder) inside the Island.
  - Composed of `_itemContentSize`, `_itemIndicatorGap`, and `_itemIndicatorSize`.
  - **Item Zoom Lock:** Items feature their own `zoomEnabled` flag, allowing individual items to opt-out of zoom.
  - **Recursive Folders:** An Item can be a `Folder` type, containing its own internal array of Items. In Icon-Only mode, folders can expand "Inline," pushing neighbors via Direct Recursive Repulsion.

## 4. Multi-Mode Indicators
To support "Icons Only" and "Icon + Label" modes without breaking symmetry:
- **Mode A (Icon-Anchored):** Indicator centers strictly under the icon portion and is bounded by the icon's visual width.
- **Mode B (Full-Span Bottom):** Indicator stretches across the full width of the item (Icon + Label).
- **Mode C (Leading Edge):** Indicator becomes a vertical pill on the left side, shifting the gap from vertical to horizontal.
- **Dynamic Dash:** Multiple window instances are rendered as dots, with the active instance morphing into a dash. The group remains perfectly centered within the item's horizontal bounds.

## 4. Direct Recursive Repulsion
The physics engine avoids QML layout lag by using a Direct Binding Chain within the Island:
`x[i] = (i == 0) ? 0 : x[i-1] + width[i-1] + _proportionalGap`.

## 5. Execution
- Update `ARCHITECTURE Mandate.md` to encode this 3-Tier system.
- Update `ROADMAP.md` Milestone 3 to target the "3-Tier Geometry" instead of the "5-Unit Stack".
- Refactor the C++ Telemetry layer (`Telemetry.hpp`) to track Island and Item variables.
