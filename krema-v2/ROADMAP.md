# Krema Development Roadmap

> A lightweight, modular dock for KDE Plasma 6. 
> **Source of Truth:** [ARCHITECTURE Mandate.md](ARCHITECTURE%20Mandate.md)
>
> Krema is built from the ground up to respect KDE Plasma 6 Wayland protocols. Development follows an **Architectural-First** approach: stability, mathematical rigour, and modularity before features.

---

## Phase 1: Foundational Architecture (v0.1.0)
*Focus: Establishing the core C++ engine, workspace-aware configuration, and geometric mandates.*

- [x] **M1: Configuration Backbone (KConfigXT / Universal JSON):** 
  - Define the schema for dock geometry, behavior, and theme (including per-screen, per-activity/workspace settings).
  - **Theme-Aware Architecture:** Ensure the config system is designed to be easily overridden by external JSON theme files (future-proofing for M16).
- [x] **M1.1: Visual Styles Protocol:** Implement the foundation for Adaptive, Tinted, Acrylic, and Mica backgrounds (compositor-agnostic shaders).
- [x] **M2: Core Dock Shell (C++):** Implement base Wayland surface and Protocol Abstraction (`IProtocol`) supporting both KWin and Hyprland (via Layer-Shell).
- [x] **M2.1: Universal Attention Engine:** Implement the four-level urgency system with cross-platform triggers (MPRIS, LibTaskManager/Hyprland IPC, Unity Entry) and Rule 16 animations.
- [x] **M3: 3-Tier Mathematical Engine:** Implement the recursive Panel -> Island -> Item hierarchy with Direct Recursive Repulsion and Proportional Gaps (Rules 1-10).
- [ ] **M4: Rendering & Animation Pipeline:** Setup QRhi-based rendering pipeline with support for visual overflow (Rule 6) and integrated Easing/Animation primitives (Rule 16).
- [x] **M5: Geometry Debugging:** Implement `--debug-geom` for real-time verification of all layout mandates.
- [x] **M5.1: Task Manager & Pinning Engine:** Implement AppIsland logic using `ITaskProvider` abstraction (KWin/Plasma via LibTaskManager and Hyprland via IPC).
- [x] **M5.2: Icon Size Normalization:** Implement C++ Alpha Bounding Box scanner for visual sizing consistency.
- [ ] **M6: Workspace Awareness Engine:** Implement filtering logic for Virtual Desktops/Workspaces and Activities using platform-specific task provider extensions.
- [ ] **M7: Layout Mode Controller:** Implement the dual-personality toggle (Icon-Only vs. Icon-Label) with global zoom-locks.
- [x] **M8: Parabolic Zoom Engine:** Axis-agnostic zoom wave (Primary/Cross Axis) with kinetic orbits.
- [ ] **M9: Fixed-Width Constraints:** Implement Squish (Dynamic Scaling) and Arrow-Scroll (Pagination) mechanics.
- [x] M10: Decoupled Catch Zone: Implement Rule 18 for full-surface interaction coverage (Buffered via HOVER_BUFFER).

## Phase 3: Visual & Modular System (v0.3.0)
*Focus: Indicators, recursive folders, and the Island Protocol.*

- [ ] **M11: Widget & Tray Ecosystem:** Implement SNI protocol (System Tray) and native shell-specific widget support (KDE Plasma Widgets / Hyprland Bar-Plugins).
- [ ] **M12: Global Tray Protocol:** Logic for overflow, interaction, and hover-previews for tray items.
- [ ] **M13: Tablet-Class Folders:** Implement recursive Item-as-a-Container logic with Inline and Pop-up expansion.
- [ ] **M14: Indicator Engine:** Implement multi-mode, instance-aware active indicators (Icon-Anchored, Full-Span, Leading-Edge).
- [ ] **M15: Advanced Visuals:** Hardware-accelerated Acrylic/Mica backgrounds, volumetric shadows, and particle effects.

---

## Future Backlog
- [ ] Modular Theme Engine (JSON-based)
- [ ] KNS (Get New Stuff) Integration

---

## Tech Stack
| Component | Technology |
|---|---|
| Language | C++23 |
| Desktop | KDE Plasma 6 (Wayland) / Hyprland |
| Framework | Qt 6.8+ / Qt Quick / QRhi |
| KDE Tooling | Frameworks 6.0+, LibTaskManager, LayerShellQt, KPipeWire |
| Hyprland Tooling | hyprctl (IPC) |
| Build System | CMake + ECM + Ninja |
ck / QRhi |
| KDE Tooling | Frameworks 6.0+, LibTaskManager, LayerShellQt, KPipeWire |
| Build System | CMake + ECM + Ninja |
, LibTaskManager, LayerShellQt, KPipeWire |
| Build System | CMake + ECM + Ninja |
