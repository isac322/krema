# Zoom Effect Research & Diagnostic Log

## 1. Executive Summary
This document catalogs the mathematical and architectural challenges encountered during the implementation of the Parabolic Zoom effect for Krema. The primary objective is to achieve **Instant Repulsion** (Rule 15), where icons push neighbors away at the exact same frequency and speed as their visual growth.

## 2. The Diagnostic Journey

### Phase 1: The "Fuzzy" Era
- **Hypothesis:** Buffers are needed to handle animation lag.
- **Trial:** Added +/- 10px hit-test buffers.
- **Result:** Interaction felt "mushy" and unpolished. Violated Rule 3.

### Phase 2: The "Ghost Center" Failure
- **Hypothesis:** Hit-testing against the mathematical "Slot Center" is the most stable method.
- **Trial:** Calculated 1D distance from unscaled centers.
- **Result:** Massive internal deadzones. The visual icon image was grounded to the floor (Rule 1), while the math looked at the slot center. They disagreed by ~11px.

### Phase 3: The "Target-Push" Jitter
- **Hypothesis:** Repulsion jitter is caused by the layout engine recalculating every frame.
- **Trial:** Used `zoomFactor` (instant target) for layout width and `currentScale` (smooth) for visuals.
- **Result:** Severe jitter. The icons "teleported" instantly, causing the cursor to "fall off" the icon before the visual could catch up.

### Phase 4: The "Interaction Mask" Breakthrough
- **Hypothesis:** Deadzones are caused by transparent SVG padding and Wayland clipping.
- **Trial:** Implemented a strict pixel-based mask and expanded the interaction buffer to 200px.
- **Result:** **Success on hit-test precision.** The icons are now visually honest and rock-solid.

## 3. The "Repulsion Lag" Problem
- **Status:** **🟢 Resolved (via Symmetrical Displacement)**
- **Symptom:** Icons zoom smoothly, but the neighbors only move AFTER the cursor stops. 
- **Cause:** QML's high-level layout engines (Flow/Row) batch updates to save CPU, creating 1-2 frames of visual overlap.
- **Solution:** Switched to a manual binding chain where each icon's position is calculated instantly based on its predecessor's extent and a dynamic gap.

## 4. The "Feedback Loop" Discovery
- **The Loop:** If icon repulsion moves an icon's center, and the zoom math depends on that center's distance to the mouse, a recursive oscillation occurs (Jitter).
- **The Fix:** **The Shadow Grid.** Decoupled the interaction grid (static unscaled centers) from the visual display (displaced icons).

## 5. The "Heisenbug" (Debug-Induced Jitter)
- **The Issue:** Internal deadzones reappeared specifically when running with `--debug-all`.
- **The Cause:** High-volume terminal logging introduced millisecond-level latency in the JS interaction handlers, causing frame-perfect hit-tests to fail for single frames.
- **The Fix:** **Persistence Hysteresis.** Separated "Initial Detection" (strict pixel check) from "Hover Survival" (generous 40px interaction orbit).

## 6. The Property Shadowing Trap
- **The Discovery:** A hardcoded `property int iconSize: 48` existed in `AppIcon.qml`.
- **The Result:** Even if the user set icon size to 96px, the hit-test logic was still checking for a 48px range. This created massive deadzones in the center/right of large icons.
- **The Fix:** Unified the `iconSize` property source to ensure the interaction engine matches the visual scale 1:1.

## 7. Conclusion of Stabilization Phase
The Krema core engine is now mathematically ironclad. We have decoupled **Interaction** (Shadow Grid), **Physics** (Binding Chain), and **Visuals** (Smooth Transforms). This architecture provides the premium performance required for a Latte-grade dock while remaining resilient to framework jitters and configuration changes.
