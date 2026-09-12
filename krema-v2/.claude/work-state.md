# Work State (The Master Mind)

> **Role:** This file is the central hub for project coordination. It bridges sessions by tracking priorities, summarizing technical memory, and linking to deep-dive documentation.

## Knowledge Index
- **Architectural Truth:** [ARCHITECTURE Mandate.md](../ARCHITECTURE Mandate.md) (The mathematical constitution)
- **Active Plans:** [plans/active/](../plans/active/) (3-Tier, Vertical/Horizontal Geometry, Placement)
- **Project State Update:** [ROADMAP.md](../ROADMAP.md) (Strategy), [Work State](../.claude/work-state.md), [Research Station](../docs/research/) (Truth)

## Current Milestone

**Milestone 1: Foundation (Architecture & Build System)** ⬅️
*Focus: Project skeleton, 3-Tier Hierarchy planning, and C++ Telemetry layer.*

## Completed Items
- [x] Initialized Architecture Mandate & Research Station.
- [x] Established the "Tree of Islands" (Panel -> Island -> Item) recursive hierarchy.
- [x] Finalized Horizontal Geometry (Span, Alignment, Justify, Arrow-Scroll).
- [x] Finalized Vertical Geometry (Overflow, Sync, Proportional Radius).
- [x] M1: Project Skeleton & CMake Modular structure.
- [x] M1: Refactored Telemetry layer to support recursive 3-Tier data structures.

## Active Tasks
- [x] M2: Core Dock Shell (C++ & Protocol Abstraction / IProtocol.hpp).
- [x] M2.1: Universal Attention Engine (C++ Foundation: AttentionManager, D-Bus Providers).
- [x] M2.1: Universal Attention Engine (QML Visuals: Badges & Rule 16 Animations).
- [x] M5.1: Task Manager & Pinning Engine (AppId/LauncherUrl fallback, QVariantList QML binding, and activation logic). ⬅️

## Known Issues
- **Wayland Coordinate Blindness:** C++ `QWindow::y()` reports `0` on KWin; absolute position is only reliable via QML `mapToGlobal`. Mitigated by immediate reveal and HOVER_BUFFER targets.

## Session History
- **2026-05-19 (Session J):** Completed M1 (Configuration Backbone) and M1.1 (Visual Styles Foundation). Integrated KConfigXT to manage settings, providing a type-safe and QML-bindable configuration layer. Generalized roadmap milestones to ensure full support for both KWin and Hyprland. Completed M4 (Rendering & Animation Pipeline) by implementing a centralized `DockAnimator` primitive and migrating UI components. Implemented M9 (Squish Mechanics) in `LayoutManager.cpp`. Finalized M6 (Workspace Awareness Engine) by implementing `WorkspaceController` and `WorkspaceSelector.qml`, enabling per-workspace task filtering.
- **2026-05-19 (Session I):** Implemented first-class Hyprland support. Created `HyprlandProtocol` (Layer-Shell) and `HyprlandTaskProvider` (IPC/hyprctl). Refactored `Main.cpp` to include dynamic environment detection (Hyprland vs KWin). Added `operator==` to `TaskEntry` for model stability. Verified operation on Hyprland with real-time geometry logging. Updated `ROADMAP.md` and `CHANGELOG.md` to reflect multi-protocol maturity.
- **2026-05-18 (Session H):** Shifted strategy to analyze and adapt proven mechanics from the `krema-reference` project. Successfully ported the Gaussian/Parabolic distribution curve for kinetic zoom physics into `LayoutManager.cpp`, replacing the linear cosine curve. Synchronized `TaskManager::TasksModel` configuration flags (`setHideActivatedLaunchers`, `setLaunchInPlace`) in `KWinTaskProvider` to prevent duplicate icons and correctly bind to the Wayland window source.
- **2026-05-18 (Session G):** Completed the Recursive Debugging suite. Formalized `--debug-geom` and registered `--debug-hit`. Implemented visual overlays (Magenta/Cyan/Yellow) and color-coded logs for all 3 tiers. Added Rule 14 "Discrepancy Alarm" to flag C++/QML math divergence. Resolved the "Velocity Bug" by prioritizing `HoverHandler`, implementing `HOVER_BUFFER` (200px), and throttling `setInputRegion` updates. Removed the 2-second "Ghost Boot" delay.
- **2026-05-18 (Session F):** Resolved M5.1 Task Manager UI sync issues. Discovered and documented that Qt 6 QML engines require `QVariantList` instead of `QList<QObject*>` for `Repeater.model` mapping to prevent empty arrays. Fixed `activateTask` by providing `LauncherUrl` fallback for apps missing `AppId` (e.g. Konsole) and mapped `Q_INVOKABLE` methods. Enforced Rule 17 (Token Efficiency & Bracket Awareness).
- **2026-05-18 (Session E):** Emergency stabilization of core engine. Fixed "Ghost Bar" problem by removing full-width anchors and implementing dynamic input regions. Resolved Parabolic Zoom jitter by implementing the "Ghost Grid" (Stable Virtual Origin) in `LayoutManager`, adhering to Mandate Rule 3. Restored zoom interaction by fixing Z-order shadowing in `MainDock.qml`. Simplified QML-C++ interaction layer.
- **2026-05-17 (Session D):** Elevated Hyprland Support to a first-class priority. Refactored the roadmap to include `ITaskProvider` and `HyprlandProtocol` milestones alongside KWin. Future-proofed M1 for a Modular Theme Engine. Expanded the architecture to include Task Management, Visual Styles (Adaptive, Mica, etc.), the System Tray (SNI), and Plasma Widget support. Integrated M5.2: Advanced Icon Resolution for Steam/EXE icon extraction.
- **2026-05-17 (Session C):** Finalized the Universal Attention Engine plan. Designed a four-level urgency system (Idle, Low, Normal, Critical) that applies to apps (Discord), system widgets (Battery), and media (MPRIS). Synced GEMINI.md to the v2 architecture and documentation standards. Created technical specification in `docs/research/notifications.md`.
- **2026-05-17 (Session B):** Completed M2: Core Dock Shell. Established `IProtocol` abstraction and implemented `KWinProtocol` using `LayerShellQt`. Restructured `src/shell` build system. Resolved CMake detection issues for Qt6 and KF6 in the current environment.
- **2026-05-17 (Session A):** Architectural "Brainstorming" phase completed. Fully mapped out the mathematical engine including recursive folders, axis-agnostic placement, and layout mode separation. Updated all telemetry structs and mandates to reflect the final blueprint. Project is ready for C++ implementation of the Wayland shell.
