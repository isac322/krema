# Feature: Dual Reserve Space Modes

## Feature Overview
**Goal:** Allow users to choose how the desktop's exclusive zone (where maximized windows stop) is calculated, ensuring icons never get cut off by windows unless intended.

**Primary Files:**
- `src/shell/dockvisibilitycontroller.cpp` (C++ Backend)
- `src/qml/main.qml` (Frontend Dimension Reporting)
- `src/qml/settings/VisibilityPage.qml` (UI Selection)

---

## Version History

### v1.0 - Panel vs. Icon Extents
**Status:** Approved and Working
**Reasoning:** Addressed the "Ceiling Collision" issue introduced by the Visual Overflow architecture.

#### 1. Real-time Dimension Reporting
The QML layer (`main.qml`) now actively reports the icons' unzoomed envelope (`animatedContentHeight`) to the C++ controller. This allows the C++ backend to know exactly how tall the "Inside World" is, even if the panel background is smaller.

#### 2. The Choice of "Worlds"
The user can toggle between two modes in the Visibility settings:
- **Panel Background (Mode 0):** Reserves space only for the physical dock pill. Maximized windows touch the pill, and icons float over the windows (macOS style).
- **Icon Extents (Mode 1):** Reserves space for the full icon height. Windows stop at the "Ceiling" of the icons, preventing any overlap (Traditional style).

#### 3. C++ Exclusive Zone Logic
The `DockVisibilityController` dynamically calculates the Wayland `exclusiveZone` by switching its thickness input between `m_panelHeight` and `m_contentHeight` based on the user's selected mode.
