## Krema Dock: Mathematical & Interaction Mandates
This document serves as the absolute source of truth for the dock's layout, math, and interaction logic. All components, QML properties, and system logic MUST strictly inherit from these rules to ensure the dock remains modular, performant, and perfectly debuggable.

### I. Mathematical & Geometric Integrity (The Law of Gravity)
**1. The 3-Tier Hierarchy & Alignment**
The dock is a recursive "Tree of Islands," managed across three alignment layers:
- **Tier 1: Panel (Global Container):** Manages `floating_offset` and `Panel-to-Screen Alignment` (Start, Center, End).
- **Tier 2: Island (Logical Group):** Maintains `_islandPadding` and adheres to `Content-to-Panel Alignment` (Start, Center, End, Justify).
- **Tier 3: Item (Atomic Capsule):** Manages `_itemContentSize` and internal indicator/gap units.
- **Justification Logic:** In 'Justify' mode, the `_islandGap` is dynamically calculated to distribute Islands across the full Panel width.

**2. The Indicator Modes & Geometry Constitution**
To ensure absolute mathematical consistency across all capsule modes:
- **Indicator Modes:** "Icon-Anchored" (centered under icon), "Full-Span" (stretches across item), or "Leading Edge" (vertical pill shifting the gap horizontally).
- **Dynamic Dash Protocol:** Indicators (dots/dashes) representing window instances MUST remain horizontally centered at the bottom of the icon portion. The total width of the indicator group is strictly bounded by the icon's visual width.
- **Dedicated Unit Variables:** Every geometric limit MUST be bound to explicit mathematical variables.
- **The Ban on Implicit Math (Magic Numbers):** Hardcoded pixel values and implicit math are strictly forbidden. All geometric logic must be derived by summing explicit unit variables.
- **Traceable Variable Registry:** Every variable used for sizing, spacing, or geometry MUST be documented in the central `docs/research/variables/` registry. Documentation must include Name, Owner, Purpose, and consumers, ensuring total transparency of the mathematical state.

**3. The Pixel-Perfect Hitbox Law (Stable Virtual Origin)**
Interaction boundaries must respect visual pixels. Hit-testing MUST use a Stable Virtual Origin mapped to an unscaled slot base (grounded by Rule 1).

**4. The Illusion of Symmetry (The Empty Gap Rule)**
Visual symmetry is achieved by matching empty gaps: the space above the icon (`panel_ceiling_padding`) MUST exactly equal the space below the indicators (`dock_floor_padding`).

**5. The Decoupled Parabolic Zoom Rule**
The zoom wave is mathematically decoupled from the hover state. Horizontal continuity is maintained across the entire panel, regardless of individual icon boundaries.
- **Global Toggle:** Parabolic Zoom is strictly restricted to **Icon-Only Mode**. In **Icon-Label Mode**, the zoom wave is globally locked OFF to preserve text readability and prevent layout jitter.

**6. Island Overflow & Vertical Geometry**
Panel thickness and Item content size are decoupled. 
- **Island Height:** An Island calculates its own vertical height based on its internal padding and Items.
- **Island Overflow:** If the Island's height exceeds the Panel's thickness, the Island visually overflows the Panel boundary. Internal math remains relative to the Island's floor, preserving symmetry.

**7. Proportional Corner Radius & Sync**
- **Ratio-Based Radius:** Corner radius is stored as a `RadiusRatio`, not a fixed pixel value.
- **1:1 Scaling:** The Panel's visual radius scales dynamically: `PanelThickness * RadiusRatio`.
- **Island Sync:** Islands use the same `RadiusRatio` against their own height.
- **Alignment Isolation:** Internal padding and alignment must flip origin based on the active screen edge (Omnidirectional Axis).
- **Dimensional Sync:** If enabled, changing the base `_itemContentSize` recalculates the `PanelThickness` to maintain the visual overflow ratio.

**8. The Omnidirectional Axis & Floating Offset Mandate**
Internal geometry must remain isolated from global screen positioning (floating offset). Internal padding and alignment must flip origin based on the active screen edge.

**9. The Dynamic Repulsion Protocol**
To prevent visual overlap, zoomed icons must physically displace neighbors using dynamic slot scaling that maintains mathematical clarity.

**10. The Proportional Gap Protocol**
Empty space (Gap) between icons must scale proportionally with the average zoom factor of the adjacent icons to maintain consistent visual rhythm.

### II. System Architecture & Performance
**11. GPU-Accelerated Rendering Architecture**
- **C++ Logic Backend (The Brain):** Responsible for protocols, math, and state. Independent of visual implementation.
- **QML/QtQuick Visual Frontend (The Body):** Responsible for SceneGraph, shaders, and animations.
- **Shader Mandate:** All visual effects (blur, shadows) MUST use GPU shaders (`.frag` / `.vert`) via QRhi. CPU-based pixel manipulation is forbidden.

**12. High-Performance Project Structure**
- **Module Separation:** The project MUST be structured into distinct CMake modules: `libkrema-core` (Math Engine, State, Config), `krema-shell` (Wayland/KWin Protocols), and `krema-ui` (QML/Shaders).
- **Build Efficiency:** Utilize precompiled headers and explicit export macros (hidden visibility).

**13. Threading & Event Loop Integrity**
- **Main Thread Sovereignty:** Reserved for UI rendering.
- **Off-Thread Math:** High-complexity layouts MUST use asynchronous task queues or signals. Never block the main loop.

### III. Interaction, Debugging & Maintenance
**14. The Observability Manifesto (Deep-State Telemetry)**
Every module added to the system MUST support this high-performance diagnostic suite:
- **Universal Instrumentation:** Every functional module MUST expose internal state transitions via the telemetry feed.
- **Compile-Time Stripping:** All diagnostic code must be wrapped in `KREMA_DEBUG` preprocessor directives.
- **Zero-Copy & Lazy Evaluation:** Telemetry data must be published via efficient pointer-based structs and only broadcast if a listener is active.
- **Frame-Budget Safety:** Diagnostic operations are subordinate to the 60fps render-loop.
- **Precision Observability:** Must provide visualization of internal hitboxes, inter-icon gaps, and zoom-decoupling state.
- **Bi-Directional Telemetry:** Every geometric feature MUST implement parallel logging in both C++ (Logic State) and QML (Visual SceneGraph State). C++ logs describe the "Intended" geometry; QML logs (`mapToGlobal`) describe the "Actual" screen reality. Discrepancies between the two are treated as high-priority bugs.
- **High-Signal Compactness:** All diagnostic output MUST be compact (single-line whenever possible) and color-coded using the `KremaConsole` system. Verbose or multi-line logs are forbidden in production debug modes.
- **KremaConsole:** All output must use the standardized, color-coded KremaConsole system for categorization.

**15. The Island Protocol (Universal Encapsulation)**
Every functional component (Widget, Tray, App) shall be encapsulated as an 'Island'.
- **Standardized Foundation:** Each Island maintains isolated origin, geometry, and state, inheriting Rule 1 and Rule 2 constraints.
- **Inter-Island Contract:** Interaction between Islands MUST be handled through strictly defined interfaces.

**16. Standardized Maintenance Mandates**
- **Geometry Debugging:** All components must support `--debug-geom` and `--debug-hit` flags.
- **Kinetic Physics:** All transitions must utilize liquid, smoothed easing curves.
- **Safe Spacing:** Negative margins and direct overlaps are forbidden.
- **Surgical Edit Mandate:** Refactors exceeding 50 lines must be broken into isolated, verifiable steps.
- **Proactive Reporting:** Anomalies must be reproduced before a fix is applied.
- **Traceability:** Every visual/logic layer MUST be documented with a unified registry.
- **Dynamic UI:** Configuration controls must dynamically bind to mathematical limits (Slider Rule).

### IV. Project State & Synchronization
**17. The Project State Update**
...
- **Continuous Sync:** Development is not considered complete until all layers of the Project State Update are reconciled.

**18. The Visual Style Constitution**
The dock background MUST strictly adhere to legibility mandates across all four styles:
- **Adaptive:** Dynamic color sampling from the wallpaper/theme must maintain a minimum contrast ratio of 4.5:1 against icons and text.
- **Tinted:** User-defined accents must respect a global opacity floor (e.g., 0.6) to prevent total transparency jitter.
- **Acrylic & Mica:** Blur radius and noise-texture intensity must be mathematically linked to the `PanelThickness` to maintain visual density.

**19. The Task Manager Invariant**
Application ordering within an `AppIsland` follows a strict deterministic logic:
- **Pins First:** Pinned applications are always anchored to the start of the Island.
- **Launcher Order:** Running applications follow the launch order or user-defined manual arrangement.
- **Grouping:** Multiple instances of the same AppID MUST be grouped into a single Item capsule unless configured otherwise.

**20. The Island Encapsulation Rule**
Complex components (System Tray, Plasma Widgets) MUST inherit the geometry of their parent Island.
- **Constraint Isolation:** Internal widget layout changes must NOT propagate size fluctuations to the Panel without explicit re-validation of Rule 9 (Dynamic Repulsion).
- **Interactive Consistency:** Tray icons and Widgets must adhere to Rule 3 (Hitbox Law) and Rule 4 (Symmetry).
