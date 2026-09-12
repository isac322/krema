# Plan: Omnidirectional Panel Placement (Rule 8)

## 1. Objective
Ensure that the dock behaves identically regardless of its placement on the screen (Top, Bottom, Left, Right). To avoid writing duplicate layout logic for horizontal vs. vertical modes, the C++ Math Engine must use an abstracted "Primary/Cross Axis" coordinate system.

## 2. Axis Abstraction
The C++ Math Engine will NEVER hardcode `X` or `Y` for layout math. Instead, it uses abstract axes:
- **Primary Axis:** The axis parallel to the screen edge. (e.g., The axis where Parabolic Zoom and Arrow Scrolling occur).
  - *Bottom/Top Placement:* Primary = X-Axis
  - *Left/Right Placement:* Primary = Y-Axis
- **Cross Axis:** The axis perpendicular to the screen edge. (e.g., The axis where Thickness, Visual Overflow, and the 5-Unit Stack are calculated).
  - *Bottom/Top Placement:* Cross = Y-Axis
  - *Left/Right Placement:* Cross = X-Axis

## 3. Implementation Strategy (The Engine)
- **`LayoutTelemetry` update:** The C++ engine tracks a single enum: `ScreenEdge edge;`.
- **The Transposer Function:** When the engine runs the Direct Recursive Repulsion or Parabolic Zoom math, it calculates everything using the abstracted `Primary` and `Cross` variables.
- At the very end of the mathematical loop, a `transpose()` function converts the `Primary/Cross` coordinates back into raw `X/Y` coordinates for QML to render.
- This guarantees that a dock on the Left edge is mathematically identical to a dock on the Bottom edge.

## 4. Origin Flipping (The UI)
- The QML layer must bind the `transformOrigin` of zooming items to the anchored edge. 
  - *Bottom:* Origin = Bottom
  - *Top:* Origin = Top
  - *Left:* Origin = Left
  - *Right:* Origin = Right
- This ensures visual overflow always points toward the center of the screen, preserving the "Fixed Floor, Moving Ceiling" logic.

## 5. Execution
- Implement the `ScreenEdge` enum in `Telemetry.hpp`.
- Ensure the C++ `GeometrySolver` class (when built in M3) is structured to use the Primary/Cross axis abstraction.
