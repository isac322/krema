## Krema Dock: Mathematical & Interaction Mandates
This document serves as the absolute source of truth for the dock's layout, math, and interaction logic. All components, QML properties, and system logic MUST strictly inherit from these rules to ensure the dock remains modular, performant, and perfectly debuggable.

### I. Mathematical & Geometric Integrity (The Law of Gravity)

**1. The 3-Tier Hierarchy & Alignment**
The dock is a recursive "Tree of Islands," managed across three alignment layers:
- **Tier 1: Panel (Global Container):** Manages `floating_offset` and `Panel-to-Screen Alignment` (Start, Center, End).
- **Tier 2: Island (Logical Group):** Maintains `_islandPadding` (background pill) and adheres to `Content-to-Panel Alignment` (Start, Center, End, Justify).
- **Tier 3: Item (Atomic Capsule):** Manages `_itemContentSize` and internal indicator/gap units.
- **Justification Logic:** In 'Justify' mode, the `_islandGap` is dynamically calculated to distribute Islands across the full Panel width.

**Gravity Chain (Grounding Law):**
The dock operates in two strictly isolated coordinate systems:
- **The Outside World (Screen Flooring):** The Dock Panel anchors to the screen edge using `floating_offset`. This handles the physical distance from the screen to the exterior of the panel box.
- **The Inside World (Dock Flooring):** The internal layout container (`dockRow`) anchors to the interior edge of the Dock Panel.
- **The Framed Island Mandate (Suspended Gravity):** To create the "Picture Frame" aesthetic (margins on all four sides of the Glass Pill) WITHOUT breaking Top-Down Reveal, `dockRow` MUST NEVER be vertically centered (`y: (height - implicitHeight) / 2` is STRICTLY FORBIDDEN). Instead, it MUST be anchored to the absolute floor using an invisible safety bumper (`_panelInternalMargin: 8`). This guarantees a permanent 8px bottom, left, and right frame. The top frame dynamically emerges when the panel thickness slider is increased.
> `[LEGACY — 5-Unit Stack]` The current implementation uses a flat "5-Unit Stack" model (Floor Padding → Indicator → Gap → Icon → Ceiling Padding) within each Item. This will be migrated to the 3-Tier recursive model. Until migration is complete, the 5-Unit variables (`_unitPanelFloor`, `_unitIndicator`, `_unitIconIndicatorGap`) remain valid within Tier 3 (Item) scope.

**2. The Indicator Modes & Geometry Constitution**
To ensure absolute mathematical consistency across all capsule modes:
- **Indicator Modes:** "Icon-Anchored" (centered under icon), "Full-Span" (stretches across item), or "Leading Edge" (vertical pill shifting the gap horizontally).
- **Dynamic Dash Protocol:** Indicators (dots/dashes) representing window instances MUST remain horizontally centered at the bottom of the icon portion. The total width of the indicator group is strictly bounded by the icon's visual width.
- **Dedicated Unit Variables:** Every geometric limit MUST be bound to explicit mathematical variables.
- **The Ban on Implicit Math (Magic Numbers):** Hardcoded pixel values and implicit math are strictly forbidden. All geometric logic must be derived by summing explicit unit variables.
- **Traceable Variable Registry:** Every variable used for sizing, spacing, or geometry MUST be documented in the central `docs/research/variables/` registry. Documentation must include Name, Owner, Purpose, and consumers, ensuring total transparency of the mathematical state.
- **Constitutional Prefixing:** Variables representing the unit stack must be prefixed with `_unit` and follow the standardized commenting format. Derived variables must explain their mathematical intent.

**3. The Pixel-Perfect Hitbox Law (Stable Virtual Origin)**
Interaction boundaries must strictly respect the visual pixels of the icon, ignoring transparent bounding boxes or invisible containers. To achieve this without introducing deadzones (the "Moving Target" problem), hit-testing MUST use a Stable Virtual Origin.
- **The Ban on Buffers:** "Fuzzy logic" or adding pixel buffers (e.g., +10px margins) to hide jitter is strictly forbidden. 
- **The Unscaled Slot Base:** The Chassis defines a fixed, unscaled "Slot" for each icon (grounded by Rule 1). Hit-testing is calculated by mapping the mouse coordinates back to the center of this Unscaled Slot, *before* any visual scaling transformations are applied. 
- **Virtual Zoom Target:** The mathematical hit-boundary scales geometrically outward from the Stable Virtual Origin.
- **Hover State:** An icon is only "hovered" when the mapped cursor is mathematically inside the 2D boundaries of the dynamically scaled Unscaled Slot.
- **Exit State:** The instant the cursor leaves the exact boundary of the mathematically scaled slot, the hover state for that specific icon is terminated.

**4. The Illusion of Symmetry (The Empty Gap Rule)**
Visual symmetry is achieved not by centering the icon unit, but by matching the empty gaps on both sides of the unzoomed icon.
- **The Floor Unit:** The total space occupied below the icon (Padding + Indicators + Gap).
- **The Empty Gap Rule:** Symmetry is perfectly realized when the empty space above the icon (`panel_ceiling_padding`) is exactly equal to the empty space below the indicators (`dock_floor_padding`).
- **The Max Height Envelope:** The maximum mathematical thickness of a slot is: `iconSize + Floor Unit + panel_ceiling_padding`.
- **Universal Application:** This rule must be maintained mathematically regardless of panel thickness, icon size, or dynamic indicator scaling. The "air" on both sides must remain identical even during a visual overflow state.
- **Subordination:** Symmetry is a visual illusion subordinate to Gravity. We do not use "Center anchoring" (e.g., `anchors.centerIn`). We use edge grounding and mathematically enforce the symmetric boundary.

**5. The Decoupled Parabolic Zoom Rule**
The zoom wave is mathematically decoupled from the pixel-perfect hover state.
- **Horizontal Continuity:** The zoom effect spans horizontally across the icons as a continuous wave, tracking the cursor's global X-coordinate on the panel, regardless of whether the cursor is actively inside a pixel-perfect icon boundary.
- **Visual Center Imperative (The Hybrid Mapping):** The mathematical distance for the parabolic zoom equation MUST be measured from the cursor to the icon's *actual displaced visual center* on the screen, not its static unzoomed grid position. To prevent circular binding loops (Scale → Layout → Center → Scale), this computation MUST be performed imperatively (top-down) from the centralized interaction handler, evaluating the physical geometry from the previous frame.
- **Vertical Limits:** The vertical scale of the zoom is bound by the icon's mathematical scale factor. The hitbox grows dynamically with the zoom scale but must continually obey the Pixel-Perfect Law.
- **Global Toggle:** Parabolic Zoom is strictly restricted to **Icon-Only Mode**. In **Icon-Label Mode**, the zoom wave is globally locked OFF to preserve text readability and prevent layout jitter.

**6. Island Overflow & Vertical Geometry (Top-Down Reveal Mandate)**
Panel thickness and Item content size are decoupled, utilizing the "Fixed Floor, Moving Ceiling" principle.
- **Island Height:** An Island calculates its own vertical height based on its internal padding and Items.
- **The Clipping Ban (No Vertical Centering):** Because Wayland surfaces and panels anchor to the physical screen edge, `dockRow` MUST NEVER be vertically centered on the cross-axis. Centering forces icons to protrude *downward* as the panel shrinks, pushing them completely off the physical screen (The Clipping Bug).
- **The Moving Ceiling:** The panel's inner (free-facing) edge is the only part of the background that moves when height is adjusted. Resizing the panel is a strict "Top-Down Reveal" mechanism.
- **Uncovering the Icon (Visual Overflow):** When the panel thickness is reduced, the "Ceiling" moves toward the "Floor". Because the icon is locked by Gravity (Rule 1) to the Fixed Floor (plus the safety bumper), the shrinking ceiling simply *uncovers* the icon from the top. The icons protrude safely *upward* toward the center of the screen, completely avoiding edge-clipping.
- **Absolute Zoom Independence:** The Zoom Scale variable (`maxZoomFactor`) and visual projection are strictly decoupled from physical Panel Thickness. Icons zoom freely as visual projections and are permitted to overflow the panel boundary indefinitely without triggering a panel resize.

**7. Proportional Corner Radius & Dimensional Sync**
- **Ratio-Based Radius:** Corner radius is stored as a `RadiusRatio`, not a fixed pixel value.
- **1:1 Scaling:** The Panel's visual radius scales dynamically: `PanelThickness * RadiusRatio`.
- **Island Sync:** Islands use the same `RadiusRatio` against their own height: `ActualIslandRadius = IslandHeight * RadiusRatio`.
- **Alignment Isolation:** Internal padding and alignment must flip origin based on the active screen edge (Omnidirectional Axis).
- **Dimensional Sync Toggle:**
    - **Independent Mode (Absolute Thickness):** When desynchronized, resizing the icons alters the Max Height Envelope (Rule 4) but leaves the absolute pixel height of the dock panel unchanged. The visual overflow size changes dynamically, but the gravity floor remains mathematically fixed.
    - **Synchronized Mode (Proportional Lock):** When synchronized, the current ratio between the panel thickness and the Max Height Envelope is locked. Modifying the base icon size will automatically calculate and apply a new panel thickness to preserve the exact visual overflow ratio.
    - **Permanent Radius Sync:** The corner radius is exempt from the synchronization toggle. It MUST always scale 1:1 proportionally with the base icon size to maintain a consistent visual "roundness" across all dock scales.
    - **Mathematical Subordination:** Sync calculations are strictly subordinate to Rule 6 and the Slider Rule (Rule 16). A synchronized scale operation can never force the panel thickness or radius to exceed the Max Height Envelope.

**8. The Omnidirectional Axis & Floating Offset Mandate**
The dock's mathematical logic and geometric rules are strictly edge-agnostic. Furthermore, the internal geometry of the dock must remain strictly isolated from its global position on the screen.

- **Axis Transposition:**
    - **Primary Axis (Length):** The axis parallel to the screen edge (X-axis for Top/Bottom; Y-axis for Left/Right). The Parabolic Zoom Rule (Rule 5) and layout flow strictly along this axis.
    - **Cross Axis (Thickness):** The axis perpendicular to the screen edge (Y-axis for Top/Bottom; X-axis for Left/Right). Max thickness limits, grounding, and visual overflow (Rules 1 & 6) are calculated against this axis.
- **The Floating Offset (External Geometry):** The Dock Panel anchors to the physical screen edge offset by a dynamic `floating_offset` variable (e.g., `0px` for flush, `10px` for floating). This offset dictates the panel's global position on the Cross Axis but MUST remain entirely mathematically invisible to the internal sizing, scaling, and hitbox calculations of the dock.
- **Isolated Panel-Edge Grounding (Internal Geometry):** The icons must be permanently anchored to the specific *inner boundary of the Dock Panel* that corresponds to the active screen edge. The icons are entirely blind to the `floating_offset` and the physical screen edge.
- **Origin Flipping:** The directional origin of the zoom effect (`transformOrigin`) must automatically flip to originate from the panel's anchored inner boundary.
- **Padding Translation:**
    - `dock_floor_padding` is the internal distance between the indicators and the panel's anchored inner boundary (The Floor).
    - `panel_ceiling_padding` is the internal distance between the unzoomed icon and the panel's free-facing inner boundary (The Ceiling).
    - The Symmetry Illusion (Rule 4) enforces that `panel_ceiling_padding` MUST exactly equal `dock_floor_padding` along the Cross Axis to maintain the visual illusion, completely independent of any `floating_offset`.

**9. The Dynamic Repulsion Protocol**
To prevent visual overlap and maintain individual "territory" during interaction, zoomed icons must physically displace their neighbors.
- **Dynamic Slot Sizing:** The primary axis of an icon's layout slot (Width for horizontal, Height for vertical) must scale 1:1 with its visual zoom factor.
- **Collision Avoidance:** The resulting layout repulsion ensures that no two icons can visually occupy the same coordinate space, preserving the Parabolic Wave's mathematical clarity.
- **Hit-test Stability:** Repulsion-driven movement must be compensated for by 'Ironclad' coordinate mapping (Rule 3) to prevent hover-state flicker during icon displacement.

**10. The Proportional Gap Protocol**
To maintain consistent visual rhythm and prevent 'cramping' at high scales, the empty space (Gap) between icons must scale proportionally with the current zoom level.
- **Linear Scaling:** The gap between Icon A and Icon B must scale based on the average zoom factor of both icons.
- **Rhythmic Preservation:** This ensures that the ratio between 'Ink' and 'Air' remains constant, providing a premium, high-fidelity visual experience regardless of the dock's magnification state.

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
- **Precision Observability:** Must provide visualization of internal hitboxes, inter-icon gaps, and zoom-decoupling state. Enabled via `--debug-geom` and `--debug-hit` runtime flags.
- **Bi-Directional Telemetry:** Every geometric feature MUST implement parallel logging in both C++ (Logic State) and QML (Visual SceneGraph State). C++ logs describe the "Intended" geometry; QML logs (`mapToGlobal`) describe the "Actual" screen reality. Discrepancies between the two are treated as high-priority bugs.
- **High-Signal Compactness:** All diagnostic output MUST be compact (single-line whenever possible) and color-coded using the `KremaConsole` system. Verbose or multi-line logs are forbidden in production debug modes.
- **The "Ghost Sheet" Protocol:** Any invisible metadata region (such as KWin blur regions) must be explicitly flagged at its calculation point. Bounding-box expansion bugs are strictly forbidden; regions must represent the actual visual territory of the components.
- **Layer & Region Traceability:** Every visual layer (QML) and logical interaction region (C++) MUST be explicitly documented with a unified registry and numbered IDs (e.g., `// --- Layer #: [Name] ---`).

**15. The Island Protocol (Universal Encapsulation)**
Every functional component (Widget, Tray, App) shall be encapsulated as an 'Island'.
- **Standardized Foundation:** Each Island maintains isolated origin, geometry, and state, inheriting Rule 1 and Rule 4 constraints.
- **Recursive Containers:** Islands can contain other modules, inheriting the same Gravity and Symmetry constraints as leaf modules.
- **Inter-Island Contract:** Interaction between Islands MUST be handled through strictly defined interfaces.
- **The Coupling Lock Protocol:** To safeguard geometric integrity (Rule 1 & Rule 4), Island coupling and membership changes are protected by an explicit 'Lock' state.
    - **Immutable State:** When locked, the dock's logical layout is immutable.
    - **Intentionality:** Modifications to the Island's structure or links between islands require an intentional unlock action (Edit Mode).

**16. Standardized Maintenance Mandates**
- **Geometry Debugging:** All components must support `--debug-geom` and `--debug-hit` flags with standardized logging formats.
- **Kinetic Physics (The "Kremy" Transition):** All zoom transitions must exhibit a weighted, liquid motion. When the mouse exits the interaction orbit, icons must follow a smoothed easing curve (e.g., `Easing.OutBack` or `CubicBezier`) driven by the animated `_zoomIntensity` property.
- **Safe Spacing (Non-Overlapping UI):** Negative margins and direct overlaps are forbidden. All labels must use `wrapMode: Text.WordWrap` and `Layout.fillWidth: true` for adaptive wrapping. Width constraints must prevent sibling elements from "crushing" each other in horizontal layouts.
- **Dynamic UI (Slider Rule):** User-facing configuration controls (sliders, spinboxes) must dynamically bind to the mathematical limits of the dock's current state. If a mathematical rule caps a value, the UI slider must instantly adopt this cap. Sliders must never move into ranges that produce no visual changes.
- **The "Panel Size" Terminology Mandate (Proportional Scaling):** The term "Panel Size" strictly refers to the **Master Proportional Scale** of the dock. Any logic, slider, or discussion referencing "Panel Size" MUST ensure that *everything inside the panel* (icons, gaps, indicators) scales symmetrically and proportionally with the panel's physical thickness. If independent panel thickness is intended without scaling content, the term "Panel Thickness" must be used instead.
- **Surgical Edit Mandate:** Refactors exceeding 50 lines must be broken into isolated, verifiable steps.
- **Proactive Reporting:** Anomalies must be reproduced before a fix is applied.
- **Traceability:** Every visual/logic layer MUST be documented with a unified registry.

### IV. Project State & Synchronization
**17. The Project State Update**
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

**21. The Glass Pill Binding Mandate (Tier 2 Wrapper)**
The `IslandModule` (Tier 2 Glass Pill) serves as the visual background for groups of icons. Its geometry MUST be flawlessly synchronized with the icons it wraps.
- **The Flatline Bug Ban:** An `IslandModule` MUST NEVER bind its `implicitHeight` or `height` to its `parent` (`dockRow`) if that parent relies on unbound or implicit dimensions. Doing so will cause the height to evaluate to `0px`, crushing the Glass Pill into a 2px flat horizontal line.
- **The Pure Anchors Law:** The Glass Pill `Rectangle` MUST exclusively use `anchors.fill: parent` (where parent is the `IslandModule` container). Because the parent `dockRow` is mathematically anchored to the floor (Rule 6), the Glass Pill will naturally wrap the protruding icons while permanently maintaining a flawless 8px bottom margin from the dark panel. Hardcoding its height to `DockSettings.panelHeight` is strictly forbidden, as it destroys the Pill's ability to wrap vertically protruding icons when the dark panel shrinks.

**22. Panel Length Mode (The Native Span Rule)**
The width of the physical dark panel is strictly dictated by the `PanelLengthMode`, completely independently of KWayland clipping constraints or scrolling boundaries.
- **Adaptive Mode (0):** The panel physically wraps the icons, snapping to `dockRow.implicitWidth + 32`.
- **Span Screen Mode (1):** The panel acts as a classic taskbar, stretching to a fixed percentage of the screen width (e.g., `root.width * (MaxLength / 100)`). The icons must be naturally centered within this vast space using pure coordinate math (`(dockPanel.width - implicitWidth) / 2`).
- **The Anti-Scroll Ban:** Attempting to implement "Span Screen" by wrapping the dock in a `Flickable` and artificially capping the KWin boundary to 10% is mathematically disastrous and strictly forbidden. Span Screen must remain a pure, native coordinate centering layout.
