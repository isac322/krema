# Research Log: Decoupled Catch Zone & Kinetic Physics (Rule 18 & 19)

> **Date:** 2026-05-12
> **Discovery:** Interaction Suffocation & Linear Fatigue

## 1. Interaction Suffocation (The 1.0x Click-through)
### The Problem
When the dock panel was set to a thin thickness (e.g., 10-20px), it became impossible to hover or click icons unless they were already zoomed. At 1.0x scale, the icons were "click-through."

### The Root Cause
The QML `MouseArea` was anchored to the `dockPanel` (the visual pill). 
`anchors.fill: dockPanel`
Because the icons overflow the panel vertically (Rule 5: Zoom Independence), the `MouseArea` was physically too short to catch the mouse before it reached the icons. The interaction was literally "suffocated" by the thin panel.

### The Solution (Rule 18: Decoupled Catch Zone)
We decoupled the `MouseArea` from the visual panel and anchored it to the full Wayland surface (`root`).
`anchors.fill: parent // where parent is the interaction surface`
This ensures that mouse events are captured regardless of the panel's visual thickness, providing a 100% reliable "Magnetic Grip" for the dock.

## 2. Linear Fatigue (The "Snap" Problem)
### The Problem
Previously, when the mouse exited the interaction orbit, the icons would instantly snap back to 1.0x. This felt "stiff" and "digital," lacking the premium, organic feel of modern OS transitions.

### The Solution (Rule 19: Kinetic Zoom Physics)
We implemented a non-linear easing behavior using an animated `_zoomIntensity` property.
- **OutBack Easing:** Provides a subtle "bounce" or "liquid" return when the mouse leaves the dock.
- **Physics Bridging:** The intensity property (0.0 to 1.0) is used to scale the parabolic wave, ensuring that the return to rest state is a fluid motion rather than a mathematical snap.

## 3. The "Icon-Gated" Authority (The Sticky Preview fix)
### The Discovery
Window previews (LayerOverlay) have higher Z-authority than the Dock. Even with precision masks, the preview surface's internal `HoverHandler` would "swallow" mouse events, preventing the Dock from knowing when to close the preview.

### The Solution
Moved the interaction "Brain" back to the Dock. The Dock now force-closes the preview the millisecond the mouse exits the icon's precise orbit, overriding the preview's own internal state. This ensures that "Wings" of a wide preview window can never block neighbor icons.
