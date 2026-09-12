# Variable Documentation Protocol

## Objective
To maintain the mathematical integrity of the dock's geometry. Every variable used in the calculation of spacing, sizing, zoom, or interaction flooring must be explicitly defined and tracked.

## 1. The Variable Documentation Registry (Mandatory)
ALL new variables for sizing, geometry, animation, or state logic MUST be documented in the registry at `docs/research/variables/`.
- Refer to `docs/research/variables/README.md` for the documentation protocol.
- Documentation for new variables must be included in the same turn the code change is applied.
- The registry MUST include: Name, Owner, Purpose, Consumers, and Architectural Links (Rule IDs).

## 2. Constitutional Unit Variables
Variables representing the "5-Unit Stack" (Rule 17) must be prefixed with `_unit` and include a comment explaining their role in the stack.

### Required Comment Format:
`// --- Unit [1-5]: [Name] ([Description]) ---`

## 2. Derived Geometric Variables
Variables calculated by summing or transforming units must include a comment explaining the mathematical intent.

### Required Comment Format:
`// --- Derived: [Name] ([Mathematical Intent]) ---`

## 3. Kinetic & Interaction Variables
Variables controlling animations or hit-test logic must be documented with their unit of measure or range.

## 4. Current Registry (The Baseline)
- `_unitPanelFloor`: Unit 1 - Internal floor padding.
- `_unitIndicator`: Unit 2 - Visual indicator height.
- `_unitIconIndicatorGap`: Unit 3 - Gap between indicator and icon.
- `_unitIcon`: Unit 4 - Dynamic icon size (zoomed).
- `_unitPanelCeiling`: Unit 5 - Internal ceiling padding (mirrors Unit 1).
- `_maxTheoreticalThickness`: The absolute mathematical envelope of a slot.
- `_currentVisualThickness`: The real-time height of a zoomed slot.
- `_unitIconBaseOffset`: Total distance from panel edge to icon base.
- `_zoomIntensity`: Kinetic bridge for transition smoothing (0.0 - 1.0).

### Interaction Stack (Mouse & Hit-Testing)
- `hoveredIndex`: The ID of the icon currently "under" the cursor.
- `virtualCenter`: The mathematical center of a slot (Rule 3).
- `mouseX` / `mouseY`: The raw coordinates on the interaction surface.
- `zoomSigma`: The "width" or influence of the parabolic wave.
- `m_zoomOverflowHeight`: Extra interaction "catch zone" above icons.

### Animation & Zoom Stack (The Wave)
- `currentScale`: Animated value between 1.0 and max zoom factor.
- `zoomFactor`: Real-time magnification multiplier.
- `itemCenterX`: The specific point the icon grows from.
- `_bounceTarget`: Pixel distance for launch animation.
- `_attentionBounceTarget`: Distance for "demands attention" jump.

### Placement & Preview Stack (Layout Origin)
- `_panelEdgePos`: Final calculated X/Y coordinate of the dock panel.
- `visualIconTop`: Absolute boundary used to align window previews.
- `m_itemGlobalPos`: Global screen coordinate used as an anchor for popups.
- `m_itemExtent`: Width/height of the icon that triggered the popup.
- `preview_margin`: The physical "air" between icons and previews (Standard: 12px).
- `m_contentX` / `m_contentY`: Absolute screen coordinates of the preview popup surface.
- `m_contentWidth` / `m_contentHeight`: Dimensions of the popup container.
- `m_hideTimer`: 200ms delay timer bridging the dock-to-preview mouse transition.

### Shadow Stack (Visual Depth)
- `margin`: Buffer calculated to ensure shadows aren't clipped.
- `elevation`: The "height" of the dock above the wallpaper.
- `shadowA`: Final opacity of the drop shadow.
