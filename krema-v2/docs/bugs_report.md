# Krema Bugs Report & Trial Log

> **Protocol:** Every bug must be investigated, reported, and approved before a fix is applied. All trials must be logged here.
> **Status Codes:** 🔴 Open, 🟡 Investigating, 🟢 Fixed, ⚪ On Hold

- [x] 🟢 **Missing Zoom Animation**: 
  - **Root Cause**: Global `MouseArea` was being shadowed by `ItemCapsule` input handlers after a Z-order shift.
  - **Trial 1**: Moved `MouseArea` to the top of the Z-order in `MainDock.qml` and enabled event propagation.
  - **Outcome**: Success. Parabolic zoom is restored.

---

## Active Bugs

- [ ] 🟡 **Startup Jump (156px Offset)**: 
  - **Identified Logic**: Wayland surfaces initialize at (0,0) before LayerShell anchors are applied.
  - **Root Cause**: C++ `QWindow::y()` reports `0` (surface-relative) on KWin/Wayland, making distance-based reveal logic blind.
  - **Trial 1**: Implemented "Ghost Boot" in `Main.cpp` with a 100ms timer and `attempts > 20` fallback.
  - **Outcome**: Mitigated. Dock reveals after 2 seconds once the jump is finished. QML `mapToGlobal` confirmed as the true coordinate source for future work.

---

## Resolved Bugs

- [x] 🟢 **Ghost Bar Input Theft**: 
  - **Root Cause**: Wayland surface was anchored to `Left|Right`, creating a full-width input block.
  - **Trial 1**: Removed horizontal anchors and implemented dynamic `requestSize` and `setInputRegion` in `Main.cpp`. 
  - **Outcome**: Success. Input is now restricted to the visual dock area.
- [x] 🟢 **Parabolic Jitter**: 
  - **Root Cause**: Hover coordinate system was relative to a dynamic dock width, creating a feedback loop.
  - **Trial 1**: Simplified QML coordinate passing and matched window width to dock width.
  - **Outcome**: Success. Coordinate system is now stable.
- [x] 🟢 **Rule 3 Violation**: 
  - **Root Cause**: Hover distance was calculated using zoomed/repulsed positions.
  - **Trial 1**: Implemented a "Ghost Grid" in `LayoutManager` to store and use stable virtual origins.
  - **Outcome**: Success. Parabolic zoom is now mathematically sound and decoupled from visual repulsion.

---

## Resolved Bugs (Session: 2026-05-18)

- [x] 🟢 **Missing Icons and Unclickable Tasks on Wayland**:
  - **Identified Logic**: `KWinTaskProvider::tasks()` returning empty AppIds for certain apps (e.g., Konsole), and `BaseIsland::items` using `QList<BaseItem*>` which QML failed to iterate over.
  - **Root Cause**: 
    1. KDE's TaskManager sometimes populates `LauncherUrl` but leaves `AppId` empty, causing tasks to overwrite each other in the QMap.
    2. Qt 6 QML engine was failing to implicitly convert `QList<BaseItem*>` into a JS array for the `Repeater.model`, resulting in empty UI slots.
    3. `LayoutManager::activateTask` was missing the `Q_INVOKABLE` macro.
  - **The Proposal & Fix**: 
    1. Used `LauncherUrl` as a fallback ID in `KWinTaskProvider`.
    2. Refactored `BaseIsland::items` to expose a `QVariantList` (`itemsVariant()`) specifically for QML binding, resolving the compilation error by including `<QVariant>`.
    3. Added `Q_INVOKABLE` to the activation method.
  - **Trials**:
    - *Trial 1:* Added telemetry logs. Confirmed tasks were retrieved but UI was empty. (Investigating)
    - *Trial 2:* Switched to `QVariantList` but hit a compilation error due to incomplete `QVariant` type. (Failed)
    - *Trial 3:* Included `<QVariant>` and implemented `itemsVariant()`. (Success)

- [x] 🟢 **Velocity Bug (Interaction Only Works with Fast Movement)**:
  - **Identified Logic**: `TapHandler` swallowing hover events and `setInputRegion` saturating the Wayland protocol.
  - **Root Cause**: 
    1. `TapHandler` was aggressively grabbing permissions, preventing `HoverHandler` from seeing slow mouse movements.
    2. The Wayland input region was being updated on every pixel of the zoom animation, causing the compositor to drop "slow" events due to queue saturation.
    3. The interaction target was perfectly clamped to the visual width, making it a very small target for slow cursors.
  - **The Fix**: 
    1. Prioritized `HoverHandler` in `MainDock.qml` and reduced `TapHandler` priority.
    2. Implemented a "Dirty State" check in `Main.cpp` to throttle input region updates (>2px delta only).
    3. Implemented `HOVER_BUFFER` (200px invisible margin) to "catch" the mouse early.
  - **Outcome**: Success. Interaction is now pixel-sensitive regardless of velocity.

- [x] 🟢 **Ghost Boot Delay (2s Reveal Timeout)**:
  - **Root Cause**: The dock was waiting for `window->y() > 500` to confirm its position at the bottom of the screen. Since KWin reports `0` for LayerShell surfaces, the dock hit its 2-second fallback every time.
  - **The Fix**: Removed the coordinate-check timer and reveal the dock immediately upon protocol initialization.
  - **Outcome**: Success. Dock is interactive the instant it appears.

- [x] 🟢 **Architectural Mirror Bug (Tier 1 QML Binding)**:
  - **Root Cause**: `BasePanel` was exposing a raw `QList<BaseIsland*>`, which caused iteration failures in Qt 6 QML engines.
  - **The Fix**: Implemented `islandsVariant()` returning `QVariantList` in `BasePanel`.
  - **Outcome**: Success. All islands are now correctly and reliably rendered.

- [x] 🟢 **Geometry Magic Numbers (Window Height Clipping)**:
  - **Root Cause**: Window height was hardcoded to `2.0 * thickness`, which would clip icons if `maxZoomFactor` changed.
  - **The Fix**: Exposed `maxZoomFactor` from `LayoutManager` and synchronized `MainDock.qml` and `Main.cpp` height calculations to it.
  - **Outcome**: Success. Wayland window height now dynamically adapts to the architectural zoom ceiling.
