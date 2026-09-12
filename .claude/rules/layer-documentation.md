# Layer & Region Traceability Protocol

## Objective
To ensure absolute visibility and maintainability of the dock's multi-layered architecture. Every component that occupies screen space—whether visual (QML) or logical (C++)—must be explicitly identified and documented.

## 1. Standardized Markers
All definitions and major calculations of layers and regions MUST use the following comment headers:

### Layers (Visual QML Objects)
Format: `// --- Layer #: [Name] ([Purpose]) ---`
Example: `// --- Layer 0: Main Dock Panel (Visual container) ---`

### Regions (Logical C++ Stencils)
Format: `// --- Region #: [Name] ([Purpose]) ---`
Example: `// --- Region 1: THE INPUT REGION (Click mask) ---`

## 2. Mandatory Documentation
Every identified layer/region must include:
- **Purpose:** Clear explanation of its role (e.g., "Handles window thumbnails").
- **Geometry:** How its dimensions are calculated (e.g., "Full screen width" or "boundingRect of X").
- **Interaction:** Whether it accepts input, is click-through, or provides visual-only metadata (like Blur).
- **Warnings:** Flag known issues or "Ghost" behaviors (e.g., "Expanding bounding box bug").

## 3. The "Ghost" Law
Invisible layers or metadata regions (like KWin blur regions) are the most dangerous. They MUST be documented at their point of calculation in C++ or QML to prevent "Ghost Sheet" bugs where empty space unintentionally becomes blurry or non-interactive.

## 4. Current Registry (The Baseline)
Maintainers must reference this list when adding new components:
- **Layer 0:** Main Dock Panel (`main.qml`)
- **Layer 1:** Window Previews (`PreviewPopup.qml`)
- **Layer 2:** Icon Hover Glow (`AppIcon.qml`)
- **Layer 4:** Blueprint Ghost Grid (`main.qml`)
- **Layer 5:** Drag & Drop Ghost (`main.qml`)
- **Layer 8:** Settings Dialog Container (`main.qml`)
- **Region 1:** THE INPUT REGION (`DockVisibilityController.cpp`)
- **Region 2:** THE BLUR REGION (`WaylandDockPlatform.cpp`)
- **Region 3:** THE PREVIEW REGION (`PreviewController.cpp`)
