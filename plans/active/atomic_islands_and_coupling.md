# Plan: Atomic Islands & Coupling Protocol

## 1. Objective
Transform the dock into a modular "Tree of Islands." Every logical group (Apps, Widgets, Folders) will be an `Island` (`BranchModule`) that owns its own geometry, while individual icons/items act as `Leaves` (`AtomicCapsules`). This replaces hardcoded UI layouts with a dynamic, self-grounding modular system.

## 2. Architectural Mandates
- **Rule 13 (The Island Protocol):** Every logical group shall be encapsulated as an 'Island'. Each Island maintains its own grounded origin and symmetry math.
- **Rule 14 (The Coupling Lock):** To safeguard geometric integrity (Rule 1 & Rule 2), Island coupling and membership changes are protected by an explicit 'Lock' state. Modifications require an intentional unlock action.

## 3. Core Mechanics

### A. The "Atomic Capsule" (BaseModule)
- Every leaf (Icon, Widget) inherits from `BaseModule`.
- Provides the uniform "Pill" background.
- Enforces Rule 1 (Gravity) and Rule 2 (Symmetry).

### B. The Island (BranchModule)
- A container that manages a `Flow` or `RowLayout` of `BaseModule` leaves.
- Handles its own alignment (Start/Center/End).
- Can optionally be "Coupled" to an adjacent Island, merging their Pill backgrounds and drawing a Separator line at the junction.

### C. The Lock Mechanism
- **Locked:** Islands are immutable. No drag-and-drop of items in/out; Islands cannot be merged.
- **Unlocked:** UI enters "Edit Mode." Islands show handles for coupling; leaves show drag handles.

## 4. Execution (Future Milestone)
- Document these rules in `ARCHITECTURE Mandate.md`.
- Implement `BaseModule.qml` to serve as the chassis for all leaves.
- Implement `IslandContainer.qml` to handle the recursive tree logic.
- Add the "Island Lock" toggle to the global Dock Settings.
