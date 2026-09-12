# Plan: Reorganizing the Architecture Mandate

The user clarified that the "Symmetry" rule is an illusion and subordinate to the "Gravity" rule (Rule 1). Icons are fundamentally grounded to the edge padding, and symmetry is maintained by sizing the panel appropriately, rather than centering the icon within a variable-sized panel.

## 1. Objective
Rewrite and reorganize `ARCHITECTURE Mandate.md` to explicitly reflect the hierarchy of Gravity over Symmetry, addressing the "Fixed Icon, Moving Box" concept.

## 2. Proposed Changes
- **Rule 1: The Supreme Law of Gravity (Grounding)**
  - Icons and their associated indicators form a single, solid unit.
  - This unit is permanently pulled by "gravity" to rest on the active screen edge of the panel.
  - The icon does not move or shift to accommodate panel resizing; the panel resizes around the grounded icon.
- **Rule 2: The Illusion of Symmetry (Padding Calculation)**
  - The dock achieves a "centered" visual aesthetic mathematically matching the empty space (padding) on the opposite side.
  - Symmetry is a visual illusion subordinate to Gravity. We do not use "Center anchoring".
- **Rule 5: The Independent Panel Height & Visual Overflow**
  - Clarify the "Fixed Icon, Moving Box" principle.
  - When the panel thickness is reduced, the panel's "Ceiling" and "Floor" shrink inward, uncovering the grounded icon to create visual overflow.
- **Rule 8: The Modular Black Box Contract**
  - Update to mention the Gravity Protocol.

## 3. Execution
- Use the `replace` tool (after exiting Plan Mode) to update the contents of `ARCHITECTURE Mandate.md` with the new structure.

## 4. Verification
- Present the updated text to the user for review.