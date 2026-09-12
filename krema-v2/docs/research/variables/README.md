# Krema Dock: Variable Documentation Registry

> **Mandate (Rule 2):** Every variable used for sizing, geometry, animation, or state logic MUST be documented here. This ensures absolute mathematical transparency and prevents "Ghost" magic numbers.

## Interaction & Boundary Units

### 1. HOVER_BUFFER
- **Owner:** `src/Main.cpp` / `LayoutManager.cpp`
- **Unit:** Pixels (Integer)
- **Default:** 200
- **Purpose:** Expands the physical window width beyond the visual dock width. This "catches" the cursor early, ensuring that the Wayland compositor hands off input events to the dock before the mouse hits the icons. 
- **Consumer:**
  - `Main.cpp`: Used to calculate `windowW`.
  - `MainDock.qml`: Subtracted from `point.position.x` to normalize coordinates for `processHover`.
- **Architectural Link:** Rule 5 (Decoupled Parabolic Zoom) & Rule 12 (Geometry Debugging).

---
## Formatting Protocol
When adding a new variable, use the following structure:
```markdown
### [VARIABLE_NAME]
- **Owner:** [File/Class]
- **Unit:** [Pixels/Ratio/Seconds]
- **Default:** [Value]
- **Purpose:** [Detailed explanation]
- **Consumer:** [List of dependent components]
- **Architectural Link:** [Rule ID]
```
