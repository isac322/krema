# Feature: Dynamic Corner Radius

## Feature Overview
**Goal:** Prevent visual rendering glitches and UI "dead zones" by mathematically clamping the corner radius relative to the panel's thickness and length.

**Primary Files:**
- `src/qml/main.qml` (Visual Clamping & Decoupling)
- `src/qml/settings/PanelPage.qml` (UI Synchronization)

---

## Version History

### v1.0 - The Initial Clamp
**Status:** Approved and Working
**Reasoning:** Implemented **Rule 6: The Dynamic UI Blindness Prevention (Slider Rule)** to ensure the visual output and the UI slider always remain within mathematically valid limits.

#### 1. Visual Clamping Logic (`main.qml`)
The `radius` of the `dockPanel` is dynamically clamped to half of its shortest side:
```qml
radius: Math.min(DockSettings.cornerRadius, Math.min(width, height) / 2)
```

#### 2. UI Synchronization Logic (`PanelPage.qml`)
The corner radius slider dynamically binds its maximum limit to the panel's current thickness:
```qml
from: 0; to: Math.floor(thicknessSlider.value / 2); stepSize: 1;
```

### v1.1 - The Geometric Decoupling
**Status:** Approved and Working
**Reasoning:** Fixed a bug where the dock's horizontal width became "stuck" during icon resizing. This was caused by the width being mathematically dependent on the `cornerRadius` setting, creating a logical circularity with the visual clamping.

#### 1. Decoupling Logic (`main.qml`)
Replaced radius-based padding with a fixed **32px "Corner Breathing Room."** This ensures the dock's physical footprint is strictly determined by its content (Icons) and remains entirely independent of its visual roundness (Radius).
```qml
// Rule 1 & 8: Use a fixed 'Corner Breathing Room' (32px)
property real _actualContentWidth: Math.max(dockRow.animatedContentWidth + 32, Kirigami.Units.gridUnit * 6)
```
