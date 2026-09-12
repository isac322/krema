# Feature: Unified Geometry Debugging Framework

## Feature Overview
**Goal:** Provide absolute mathematical visibility into every physical and interactive layer of the dock through a single runtime flag.

**Primary Files:**
- `GEMINI.md` (Formal Mandate)
- `src/qml/main.qml` (Panel, Mouse, Gap, Region Logs)
- `src/qml/AppIcon.qml` (Icon, Indicator Logs)

---

## Version History

### v1.0 - The Geometry Debugging Mandate
**Status:** Approved and Working
**Reasoning:** Implemented **Rule 12: The Geometry Debugging Mandate** to ensure all current and future components have a standardized diagnostic output. This eliminates "guessing" during visual polish phases.

#### 1. The Unified Flag
Activated via the `--debug-geom` command-line argument. This flag toggles high-fidelity terminal logs across all components.

#### 2. Log Categories
- **`[GEOM-PANEL]`**: Tracks background position, size, and orientation.
- **`[GEOM-ICON]`**: Logs unscaled slot dimensions vs. scaled icon image pixels.
- **`[GEOM-INDIC]`**: Monitors indicator grounding and proportional scaling.
- **`[GEOM-MOUSE]`**: Exposes the physical interactive bounds of the dock.
- **`[GEOM-REGION]`**: Logs the precise Wayland Input Region passed to the OS.
- **`[GEOM-INTER-ICON-GAP]`**: Measures the empty space between icons where no hover is registered.

#### 3. Inter-Icon Gap Measurement
A specialized tracker inside the hit-test loop that explicitly measures and exposes the "Dead Zones" between icons. This was essential for identifying the 10px "Fuzzy Buffer" dependencies.
