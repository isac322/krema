# Plan: Test and Document Working Features

## 1. Objective
1. Run the application to verify that all recent surgical edits (KConfig fixes, bracket fixes, revert operations) have not broken the application.
2. Once the test passes and the dock is stable, populate the `docs/features/` directory with versioned documentation for all recently approved and stabilized features to prevent future regressions.

## 2. Proposed Documentation
Create three new markdown files in `docs/features/`:

### A. `two_worlds_grounding.md`
- Document how `main.qml` handles "Outside World" (Screen Flooring) via `_screenFlooring`.
- Document how `AppIcon.qml` handles "Inside World" (Dock Flooring) via flush `dockRow` anchoring and `_dockFloorPadding`.
- Detail the "Top-Down Reveal" mechanism (Rule 1 & Rule 5).

### B. `illusion_of_symmetry.md`
- Document the "Empty Gap" math: ensuring `_dockCeilingPadding` strictly equals `_dockFloorPadding` (12px).
- Detail the `_totalFloorUnit` calculation (Padding + Dot + Gap) and how the maximum panel envelope is sized to perfectly mirror the top and bottom empty spaces (Rule 2).

### C. `dual_reserve_space.md`
- Document the integration between the QML frontend (`animatedContentHeight`) and the C++ Wayland backend (`DockVisibilityController::evaluateVisibility()`).
- Detail the two modes: Panel Background (0) vs. Icon Extents (1).

## 3. Execution
1. Exit plan mode.
2. Run `just build && ./build/dev/bin/krema --debug-geom`.
3. Verify logs and visual stability.
4. Use `run_shell_command` with `cat << 'EOF' > ...` to create the markdown files in `docs/features/` directory.