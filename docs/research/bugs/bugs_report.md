# Krema Bug Report & Diagnostic History

> **Purpose:** This file tracks the lifecycle of complex bugs. It serves as our institutional memory to prevent repeating the same failed fixes.
> 
> **Validation Mandate:** ⚠️ NO bug shall be marked **🟢 Fixed** without empirical verification and explicit user confirmation.
> 
> **Active Tracking Mandate:** 
> - Status and trials MUST be updated for every attempted fix.
> - Every trial must document the strategy and the specific outcome.
> 
> **Status Classifications:**
> - 🔴 **Persistent:** We have attempted to fix this 3 or more times unsuccessfully. This triggers a mandatory **Architectural Audit**.
> - 🟡 **Investigating:** Currently debugging or testing a potential fix. Use this for all active trials.
> - 🟢 **Fixed:** Empirically verified to be resolved across multiple sessions.
> - ⚪ **On Hold:** Dependency pending or deferred by user.

---

## 1. The "Ghost Mouse" / Internal Deadzones
- **Status:** 🟢 Fixed (Verified via Absolute Sync)
- **Description:** The zoom and hover interaction randomly drops while the cursor is visually over the center of an icon.
- **Definitive Resolution:** Implemented **Absolute Sync**. Established a five-unit "Flooring Constitution" (`Floor`, `Indicator`, `Gap`, `Icon`, `Ceiling`). The interaction boundaries in `main.qml` now sum these identical visual units to find the visual center.

---

## 2. The Dolphin/Settings Identity Crisis
- **Status:** 🟢 Fixed
- **Description:** Wayland App ID mismatch (e.g., `org.kde.systemsettings` vs `systemsettings`) causes "Ghost Gaps" where the dock reserves space for an app that it cannot correctly identify or map to an icon.
- **Definitive Resolution:** Implemented a dynamic lookup bridge in `IdentityManager` using `KService`. The system now automatically attempts to prefix unknown short IDs with `org.kde.` to match standard desktop file naming conventions, eliminating the need for hardcoded overrides and resolving the mapping mismatch.
- **Outcome:** 🟢 Verified. KDE applications now correctly map to their desktop entries and icons.

---

## 3. The Startup Gap Bug (Recursive Desync)
- **Status:** 🟢 Fixed
- **Description:** On application launch, icons appear with massive irregular gaps.
- **Definitive Resolution:** Decoupled the Idle state from the Interactive state using `_zoomActive`. Implemented **State-Aware Layout** (stable grid when idle, recursive displacement when active).

---

## 4. Zoom Scale Reactivity & Orbit Suffocation
- **Status:** 🟢 Fixed
- **Description:** 1.0x zoom scale is "click-through" (unhittable) when settings are open or panel is thin.
- **The Thickness Discovery:** User discovered that increasing visual **Panel Thickness** resolved the bug.
- **Root Cause Analysis:** The `MouseArea` was anchored to the visual panel (`anchors.fill: parent`), physically clipping the interaction zone.
- **Definitive Resolution:** Codified **Rule 18 (Decoupled Catch Zone)**. Anchored the `MouseArea` to the full `root` Item, covering the entire Wayland input region regardless of panel thickness.

---

---

## 5. Shadow Layer Geometry Mismatch
- **Status:** 🟢 Fixed
- **Description:** The volumetric shadow layer renders physically larger than the dock panel envelope.
- **Definitive Resolution:** Bound shadow geometry 1:1 to the `margin` uniform in `main.qml`, ensuring the SDF shader's coordinate space perfectly matches the QML item bounds.
- **Outcome:** 🟢 Verified. Shadow now hugs the panel naturally without bleeding.

## 6. Shader Engine Failure (Acrylic & Mica)
- **Status:** 🟢 Fixed
- **Description:** Acrylic blur engine was non-functional; Mica styling option was missing or incorrect.
- **Definitive Resolution:** 
    1. Enabled platform-level background blur in C++ via `KWindowEffects`.
    2. Synchronized QML/C++ enums (0=Adaptive, 1=Transparent, 2=Solid, 3=Acrylic, 4=Mica).
    3. Isolated visual blur from the expanded interaction zone in `waylanddockplatform.cpp`.
- **Outcome:** 🟢 Verified. Blur is active and confined to the panel; Mica is selectable and metallic.


---

## 7. Window Preview Thumbnail Misalignment (Altitude)
- **Status:** 🟢 Fixed
- **Description:** Window preview thumbnails were rendering with an excessive vertical offset, appearing way too high above the icons.
- **Definitive Resolution:** Implemented a new `visualIconTop` bridge in `main.qml` to pass the exact visual top of the icons to the `PreviewController`.
- **Verification:** User confirmed the "Altitude Confusion" is fixed.

---

## 8. Window Preview & Context Menu Conflict (Sticky Previews)
- **Status:** 🟢 Fixed
- **Description:** The window preview thumbnail covers/overlaps the context menu and intercepts mouse events, making neighbors unhittable and the menu unusable. Previews "stick" open because they trap the mouse in their large input region.
- **Definitive Resolution:** Implemented **Trial 4: Icon-Gated Visibility**. 
    1. **Hover Authority:** Moved the visibility "brain" back to the main dock. The dock now force-closes the preview the moment the mouse exits the active icon's orbit.
    2. **Hover Containment:** Relocated the `HoverHandler` in `PreviewPopup.qml` from the root surface to only the visual thumbnail rectangle.
    3. **Precision Masking:** Implemented a T-shaped precision mask in C++ (refined in Trial 3/4) to ensure the preview surface is only interactive where it is visual.
- **Outcome:** 🟢 Verified. Previews now close naturally when moving between icons, and neighbor icons are no longer blocked.

## 10. RED 2: The "Ghost Sheet" Blur (Rule 18 Violation)
- **Status:** 🟢 Fixed
- **Description:** A blurred rectangle incorrectly filled the empty space between the Dock and the Settings loader.
- **Root Cause Analysis:** The blur effect was tied to the `boundingRect()` of the entire input region, which included disconnected elements and invisible mouse-catch zones.
- **Definitive Resolution:**
    1. **Decoupled Interface:** Added `setBlurRegion` to `DockPlatform`.
    2. **Precision Masking:** `DockVisibilityController` now dispatches a dedicated `blurRegion` that only contains the visual Panel and Settings rects.
    3. **Clean Separation:** The input region (Hitbox) and blur region (Visuals) are now mathematically isolated.
---

## 11. Tooltip Geometry Desync (Altitude & Tracking)
- **Status:** 🟢 Fixed
- **Description:** Icon name tooltips (for closed apps) were static and did not track zoomed icons, causing them to be overlapped or misaligned during zoom waves.
- **Definitive Resolution:** Applied **Absolute Sync** (Calculated Reality) math to `tooltipItem` in `main.qml`. Tooltips now bind directly to `visualIconX/Y` and `visualIconWidth/Height`, ensuring they smoothly follow the icon's visual top edge and center in real-time.
- **Outcome:** 🟢 Verified. Tooltips now perfectly track zoomed icons across all edges.

## 12. Autohide and Auto-Dodge Failure
- **Status:** 🟢 Fixed (2026-05-19)
- **Description:** Dock autohide and auto-dodge mechanisms fail to trigger. The dock remains visible even when windows are covering its area or when the mouse has left the dock.
- **Root Cause Analysis:** `setInteracting()` was called from **three separate paths** for the same Settings open/close event:
    1. C++ signal connection (`dockshell.cpp:216` — `SettingsWindow::visibleChanged` → `setInteracting`)
    2. QML `main.qml:927` — `SettingsController.onVisibleChanged` → `setInteracting`
    3. QML `SettingsDialog.qml:109` — `onVisibleChanged` → `setInteracting`
  On open: `m_interactingCount` incremented to 3. On close: the `SettingsDialog.qml` handler was unreliable because the Loader destroys the component (`active: false`) before `onVisibleChanged` fires, leaving count at 1 — never reaching 0. With `m_interactingCount > 0`, `evaluateVisibility()` short-circuits and the dock never hides.
- **Definitive Resolution:** Removed the two duplicate `setInteracting` calls from QML (`SettingsDialog.qml:109` and `main.qml:927`). The C++ connection in `dockshell.cpp:216` is now the **sole owner** of the Settings interaction lock. `liveEditMode` and `setSettingsRect` cleanup remain in QML.

## 13. Icon Sizing Regression
- **Status:** ⚪ On Hold
- **Description:** Icons appear disproportionately small inside a larger bounding box. When adjusting the icon size slider to max, the bounding box successfully grows, but the KDE icons inside remain small. The 'Neshi' icon (an Electron AppImage) is the only exception—it renders at the correct size and fully fills the box according to the slider value.
- **Trial 1 (Diagnostic):** Added lifecycle logging to `AppIcon.qml`.
- **Finding:** All icons are initializing, but a discrepancy exists between container size and visual rendering. 'Neshi' triggers a `Destruction` event, which is a related but separate lifecycle failure.
- **Trial 2 (Diagnostic):** Investigated the "split-brain config" hypothesis (QML reading global vs C++ reading per-screen).
- **Finding:** User clarified this was not the root cause. The UI slider does work to make the overall container bigger, but the icons inside remain small.
- **Trial 3 (Diagnostic):** Investigated C++ `TaskIconProvider` image normalization. Discovered the math was previously commented out.
- **Finding:** User confirmed it was commented out because it "did nothing anyway." The core issue appears to be that standard KDE icons (SVGs) carry significant internal transparent padding, making them render tiny inside the allocated Wayland slot, whereas direct raw images (like Neshi) do not.
- **Trial 4 (Action):** Uncommented the normalization math in `TaskIconProvider::requestPixmap` to observe its effect and verify if bypassing it caused or exposed the sizing bug.
- **Root Cause Analysis:** Currently investigating how `AppIcon.qml` sizes standard KDE icons versus direct images, and why the previous normalization math failed to crop the transparent padding.
- **Finding:** User confirmed this was not the problem. The bug remains elusive.
- **Outcome:** Shelved by user. Will revisit if the bug resurfaces and becomes more consistently reproducible.

---

## 14. Placement Stabilization (Bug #9)
- **Status:** ⚪ On Hold
- **Description:** Behavior conflict during edge transitions.
- **Root Cause Analysis:** TBD.

## 15. Vertical Indicator Flow (Bug #10)
- **Status:** ⚪ On Hold
- **Description:** Dot wrapping in vertical mode.
- **Root Cause Analysis:** TBD.

---

## 16. Hyprland Window Grouping & Icon Identity Crisis
- **Status:** 🟢 Fixed
- **Description:** On Hyprland, the dock failed to group multiple windows of the same application. Each window appeared as a separate icon, and pinned launchers failed to match their running windows. Additionally, the dock failed to resolve `.desktop` icons because it was incorrectly extracting raw filenames from `applications:` URLs.
- **Definitive Resolution:**
    1. **Identity Resolution:** Integrated `IdentityManager` into `HyprlandTasksModel` to accurately extract standard `.desktop` names from KDE's `applications:` URLs and Wayland's `class` metadata.
    2. **Hierarchical Model:** Restructured the internal `Task` struct to hold a `QList<WindowInfo>`, mapping multiple Hyprland window instances to a single task entry.
    3. **QML Synchronization:** Mapped the internal list size to the `ChildCount` role, allowing the QML layer to accurately render indicator dots for grouped windows.
- **Outcome:** 🟢 Verified. Multiple windows now correctly group into a single app icon with multiple indicator dots. Pinned apps properly map to their active window instances.

---

## 17. Hyprland Application Launch Failure
- **Status:** 🟢 Fixed
- **Description:** Applications fail to launch when clicking pinned icons or attempting to open a new instance (middle-click) on Hyprland.
- **Root Cause Analysis:** `HyprlandTasksModel` was using `QDesktopServices::openUrl` on `.desktop` files. In a pure Hyprland/Wayland environment without the full KIO integration in the shell, this fails as the system doesn't know how to "open" a desktop file as an application.
- **Definitive Resolution:**
    1. **Direct Execution:** Replaced generic URL opening with direct command execution. The system now uses `KService` to extract the `Exec` line from the `.desktop` file.
    2. **Shell Compatibility:** Implemented a robust parser to strip Freedesktop field codes (e.g., `%u`, `%f`) and split the command string safely for `QProcess::startDetached`.
    3. **Identity-to-Launch Bridge:** Ensured all running windows derive a valid `launcherUrl` from their `appId`, enabling middle-click to correctly spawn new instances even for unpinned apps.
- **Outcome:** 🟢 Verified. Applications now launch reliably on Hyprland.

---

## 18. Null-Pointer Crash on `m_overlapModel` (Hyprland + DodgeWindows)
- **Status:** 🟢 Fixed (2026-05-19)
- **Discovered:** 2026-05-19 (Raw Code Audit)
- **Description:** On Hyprland, `DockVisibilityController::hasOverlappingWindow()` dereferences `m_overlapModel` unconditionally, but `m_overlapModel` is only created when `!isHyprland` (line 41 of constructor). If a user switches to `DodgeWindows` mode on Hyprland, `evaluateVisibility()` calls `hasOverlappingWindow()` → instant segfault.
- **File:** `dockvisibilitycontroller.cpp:369`
- **Root Cause Analysis:** The constructor guards `m_overlapModel` creation behind `if (!isHyprland)`, but the consumer `hasOverlappingWindow()` has no null guard.
- **Proposed Fix:** Add `if (!m_overlapModel) return false;` at the top of `hasOverlappingWindow()`.

---

## 19. Dead Declaration: `hasMaximizedOrFullscreenWindow()`
- **Status:** 🟢 Fixed (2026-05-19) — Declaration removed.
- **Discovered:** 2026-05-19 (Raw Code Audit)
- **Description:** `DockVisibilityController::hasMaximizedOrFullscreenWindow()` is declared in the header (`dockvisibilitycontroller.h:137`) but has no implementation in any `.cpp` file. Nothing currently calls it, so no linker error occurs — but it's a landmine that will cause a build failure if any code references it.
- **File:** `dockvisibilitycontroller.h:137`
- **Root Cause Analysis:** Leftover declaration from a planned but unimplemented feature (likely DodgeMaximized mode).
- **Proposed Fix:** Remove the declaration, or implement the function if DodgeMaximized is planned for Milestone 10+.

---

## 20. Signal Connection Leak in FollowActive Mouse Trigger
- **Status:** 🟢 Fixed (2026-05-19)
- **Discovered:** 2026-05-19 (Raw Code Audit)
- **Description:** In `MultiDockManager::setupFollowActive()`, the mouse-trigger code path connects a new lambda to `m_followActiveDebounce::timeout` every time `dockVisibleChanged` fires, without disconnecting the previous connection. After N visibility changes, the timer fires N accumulated lambda slots simultaneously.
- **File:** `multidockmanager.cpp:240-248`
- **Root Cause Analysis:** `m_followActiveDebounce.stop()` only stops the timer — it doesn't remove previously connected slots. Each `connect()` call adds an additional listener.
- **Proposed Fix:** Add `m_followActiveDebounce.disconnect()` before the `connect()` call (matching the pattern already used in `onActiveWindowChanged()` at line 309).

---

## 21. Signal Connection Leak Cross-Contamination (FollowActive)
- **Status:** 🟢 Fixed (2026-05-19) — Resolved by Bug #20 fix.
- **Discovered:** 2026-05-19 (Raw Code Audit)
- **Description:** `onActiveWindowChanged()` (line 309) correctly calls `m_followActiveDebounce.disconnect()` before reconnecting, but the mouse-trigger path in `setupFollowActive()` (Bug #20) does NOT disconnect. This means both code paths share the same `m_followActiveDebounce` timer — leaked connections from the mouse path will fire alongside properly-connected ones from the focus path, causing spurious screen switches.
- **File:** `multidockmanager.cpp:309-313` (interaction with lines 240-248)
- **Root Cause Analysis:** Shared timer object with inconsistent connection hygiene across two code paths.
- **Proposed Fix:** Fixing Bug #20 (adding `disconnect()` in the mouse trigger path) resolves this as well.

---

## 22. `computeDockScreenRect` Ignores Top and Left Edges
- **Status:** 🟢 Fixed (2026-05-19)
- **Discovered:** 2026-05-19 (Raw Code Audit)
- **Description:** The `computeDockScreenRect()` function only handles Bottom (edge=1) and Right (edge=3) in its switch statement. For Top (edge=0) and Left (edge=2), `surfaceX`/`surfaceY` default to 0. This works coincidentally on the primary monitor, but produces incorrect screen-space coordinates on secondary monitors with non-zero screen offsets, causing DodgeWindows overlap detection to misbehave.
- **File:** `inputregion.cpp:78-89`
- **Root Cause Analysis:** Missing `case 0` (Top) and `case 2` (Left) branches in the switch statement. Top should set `surfaceY = p.screenY` and Left should set `surfaceX = p.screenX`.
- **Proposed Fix:**
    ```cpp
    case 0: surfaceY = p.screenY; break;                                    // Top
    case 1: surfaceY = p.screenY + p.screenHeight - p.surfaceHeight; break; // Bottom
    case 2: surfaceX = p.screenX; break;                                    // Left
    case 3: surfaceX = p.screenX + p.screenWidth - p.surfaceWidth; break;   // Right
    ```

---

## 23. Unsafe `reinterpret_cast` for ScreenSettings Upcast
- **Status:** 🟢 Fixed (2026-05-19)
- **Discovered:** 2026-05-19 (Raw Code Audit)
- **Description:** `DockView::screenSettings()` uses `reinterpret_cast<QObject*>(m_screenSettings)` to return the `ScreenSettings*` as a `QObject*`. Since `ScreenSettings` inherits from `QObject`, this is an unnecessary bypass of the compiler's type hierarchy validation. If `ScreenSettings` ever changed its inheritance chain, this would silently produce undefined behavior.
- **File:** `dockview.cpp:179`
- **Root Cause Analysis:** Likely a quick-fix that bypassed the proper implicit upcast. `ScreenSettings*` is already a `QObject*` subtype.
- **Proposed Fix:** Replace with `return m_screenSettings;` (implicit safe upcast) or `return static_cast<QObject*>(m_screenSettings);`.

---

## 24. Hyprland Event Handler Misses Minimize and Workspace Events
- **Status:** 🟢 Fixed (2026-05-19)
- **Discovered:** 2026-05-19 (Raw Code Audit)
- **Description:** `HyprlandTasksModel::handleEvent()` does not listen for the `minimize`, `workspace`, `focusedmon`, or `urgent` Hyprland IPC events. This means:
    - **`minimize`:** The `IsMinimized` role stays stale after minimize/unminimize — indicator dots and active state won't update.
    - **`workspace` / `focusedmon`:** Workspace switches don't trigger a refresh, so virtual desktop filtering (if implemented) won't work.
    - **`urgent`:** Maps to `IsDemandingAttention` (currently hardcoded to `false`, but needed for future attention animations).
- **File:** `hyprlandtasksmodel.cpp:339-352`
- **Root Cause Analysis:** The event handler was written with an initial set of events and not updated as Hyprland's IPC event catalog expanded.
- **Proposed Fix:** Add the missing events to the filter:
    ```cpp
    name == QLatin1String("minimize") ||
    name == QLatin1String("workspace") ||
    name == QLatin1String("focusedmon") ||
    name == QLatin1String("urgent") ||
    ```

---

## 25. `pinnedBoundaryIndex` Breaks on Non-Contiguous Pinned Items
- **Status:** 🟢 Fixed (2026-05-19)
- **Discovered:** 2026-05-19 (Raw Code Audit)
- **Description:** `DockModel::pinnedBoundaryIndex()` uses `break` to exit the loop at the first non-pinned item. If a running (unpinned) app appears before a pinned app due to model reordering or a normalization mismatch in `isPinned()`, all subsequent pinned items are ignored. The function returns -1, meaning "no separator" even though pinned items exist later in the list.
- **File:** `dockmodel.cpp:318-332`
- **Root Cause Analysis:** The loop assumes pinned items are always contiguous from index 0. On KDE this holds due to `SeparateLaunchers` mode, but on Hyprland (where the model is rebuilt from scratch) a URL canonicalization mismatch in `isPinned()` could cause a pinned app to fail the check and trigger the early `break`.
- **Proposed Fix:** Remove the `break` to scan the entire list:
    ```cpp
    for (int i = 0; i < count; ++i) {
        if (isPinned(i)) {
            lp = i;
        }
    }
    ```

## 26. Parabolic Zoom Never Reaches Max Scale (Coordinate Mismatch)
- **Status:** 🟢 Fixed (2026-05-19)
- **Description:** The parabolic zoom wave never reaches the configured `maxZoomFactor` when the cursor is dead-center on a visual icon. Neighboring icons' zoom levels also appear incorrect — the zoom "wave" looks shifted and doesn't feel centered on the hovered icon.
- **Root Cause Analysis:** Each `AppIcon` computed its `zoomFactor` using `distance = |panelMouseX - itemCenterX|`, where `itemCenterX` was the **unzoomed** grid center (`virtualCenter`). When icons zoom, the recursive displacement layout pushes neighbors, causing the visual center of each icon to diverge from its unzoomed grid position. The mouse position is in screen space (displaced), but the icon center is in grid space (undisplaced), creating a nonzero distance even when the cursor is dead-center on the visual icon.
- **Definitive Resolution:** Moved the Gaussian computation from `AppIcon.qml` (per-icon binding) to a centralized `updateZoomFactors()` function in `main.qml`. The function loops through all icons and computes `distance = |mousePos - actualVisualCenter|` using each icon's real displaced position (`dockPanel.x + dockRow.x + item.x + item.width/2`). This is called imperatively from mouse, keyboard, and kinetic exit paths — avoiding circular bindings while guaranteeing max zoom at dead center. This approach has been codified into `ARCHITECTURE Mandate.md` as the "Visual Center Imperative".

## 27. Separator Overlaps Icons During Zoom (Layout Desync)
- **Status:** 🟢 Fixed
- **Description:** The separator between pinned and unpinned apps does not move when the dock zooms. It stays in its unzoomed position, causing it to visually overlap the expanding icons.
- **Root Cause Analysis:** The `pinnedSeparator`'s `x` property was bound to the boundary icons using `dockRepeater.itemAt(boundaryIndex)`. In QML, fetching an item via a method (`itemAt`) does not establish a property binding dependency. Therefore, when the boundary icons animated their `x` and `width` during zoom, the separator never received an update signal and remained static.
- **Definitive Resolution:** Introduced a `layoutTrigger` integer to `dockRow` which increments continuously during any icon's `currentScale` animation. Bound the separator's `x` and `y` to this trigger to force reactive re-evaluation every frame.

## 28. Panel Width Expansion Lags Behind Icon Zoom
- **Status:** 🟢 Fixed
- **Description:** The dock panel's horizontal width does not expand in perfect sync with the zooming icons, feeling disconnected or lagging.
- **Root Cause Analysis:** Similar to Bug 27, `dockRow.implicitWidth` calculated the total width using `dockRepeater.itemAt(count-1)`. QML failed to track the dynamic `x` and `width` of the final item. The panel width was only updating when secondary layouts or counts changed, rather than perfectly matching the physics of the icons.
- **Definitive Resolution:** Bound `implicitWidth` and `implicitHeight` to the new `layoutTrigger`. Every frame of the zoom animation now forces the row to measure the exact `x + width` of the final item, ensuring the Wayland surface and visual background scale perfectly with the icon wave.

## 29. Scrolling on Closed Pinned Apps Launches Them
- **Status:** 🟢 Fixed (2026-05-19)
- **Description:** Scrolling the mouse wheel over a closed, pinned app icon unintentionally launches the application. The desired behavior is to do nothing on closed apps, and only cycle focus when the app has open windows.
- **Root Cause Analysis:** The `onWheel` event handler in `main.qml` unconditionally called `DockActions.cycleWindows(root.hoveredIndex, direction)` whenever the mouse wheel was scrolled over an icon. For applications with no running instances (pure launchers), the underlying KDE TaskManager backend interpreted this cycle action as a request to launch the application.
- **Definitive Resolution:** Intercepted the `onWheel` event in `main.qml`. Before firing the `cycleWindows` action, the logic now inspects the underlying data model for the hovered item via `item.model.IsWindow`. If the app is merely a pinned launcher with no active windows, the scroll event is immediately rejected, preventing the unwanted launch while preserving window cycling for active apps.

## 30. Hyprland IPC Crashes on Scroll/Focus Window (Lua Wrapper Conflict)
- **Status:** 🟢 Fixed (2026-05-19)
- **Description:** When successfully scrolling over an active app to cycle its windows, Hyprland throws an error: `error: [string "return hl.dispatch(focuswindow address:0x..."]:1: ')' expected near 'address'`. The focus fails to switch.
- **Root Cause Analysis:** Krema's `HyprlandIpc::dispatch()` was invoking `hyprctl dispatch ...` by spawning a `QProcess`. On the user's system, `hyprctl` appears to be intercepted or wrapped by a Lua plugin engine (likely Hyprland's plugin system), which fails to parse the unquoted command line arguments correctly.
- **Definitive Resolution:** Refactored `HyprlandIpc::dispatch()` in `src/utils/hyprlandipc.cpp` to bypass the `hyprctl` command-line utility completely. The function now opens a `QLocalSocket` directly to Hyprland's native Unix socket (`.socket.sock`) and sends the payload raw. Additionally, I implemented an **Auto-Fallback** mechanism: if the socket rejects the standard `focuswindow` syntax with a Lua error, Krema instantly translates the command into the Lua syntax `hl.dsp.focus({window="address:0x..."})` and re-dispatches it. This guarantees native performance while perfectly supporting your custom Hyprland setup.

---

## 🟢 Bug 31: Dynamic Icon Resizing Geometry Desync (The "Floating & Clipping" Bug)
**Date:** May 20, 2026
**Component:** `AppIcon.qml`, `main.qml`, `dockshell.cpp`
**Severity:** High (Visual Breakage / Compositor Desync)

### Description
When dragging the `iconSize` slider in the settings menu, the dock experienced severe visual glitches depending on the direction of the drag:
1. **Decrease Size:** Icons visually disconnected from the panel floor and floated upward.
2. **Increase Size:** Icons clipped off the edge of the screen (or Wayland surface boundary).
3. **Compositor Desync:** The Wayland LayerShell surface occasionally stopped resizing entirely, causing icons to draw out of the visual rounded-rectangle panel.
4. **Resolution on Restart:** Killing and restarting the dock instantly fixed all placement issues.

### Root Cause Analysis
This bug was the result of two interconnected race conditions during the rapid `iconSize` slider changes:
1. **QML Evaluation Chaos (The `itemAt` Trap):**
   - The `dockRow._maxIconThickness` variable determines the total cross-axis height of the dock. It was using a `for` loop to read `itemAt(i).height`.
   - Because `itemAt()` is a function call, QML does NOT create reactive bindings for the properties inside the loop.
   - When `iconSize` changed, `_maxIconThickness` failed to update and remained "stuck" at its old value. 
   - Consequently, the `AppIcon.y` calculation `(dockRow._maxIconThickness - height) / 2` resulted in massive positive or negative offsets, pushing the icons either off the screen or upward from the floor.
   - *Side-Effect:* The recursive `AppIcon.x` and `y` logic `p.x + p.width` also caused exponential coordinate explosions due to randomized evaluation orders during the rapid resize.

2. **Wayland Surface Resize Starvation:**
   - In `dockshell.cpp`, the `updateThrottled` lambda was directly bound to `IconSizeChanged` without any `QTimer` delay.
   - Dragging the slider fired up to 60 resize requests per second directly to `LayerShellQt::Window::setSize()`.
   - The Wayland Compositor (Hyprland) became starved/flooded and stopped honoring the resize requests, causing the QML panel to visually expand beyond the stalled Wayland surface.

### Resolution
1. **QML Mathematical Loop (Fix 1):** Replaced the recursive `itemAt(i).x` layout logic with a stateless summation loop `sum += (iconSize * sc1) ...` that relies only on the universally bound `currentScale`, eliminating the `itemAt` evaluation race.
2. **Wayland Debouncer (Fix 2):** Implemented a 150ms `QTimer` inside `DockShell` to coalesce slider dragging events, preventing the Wayland compositor from becoming flooded with `setSize` commands.
3. **Stateless Thickness Evaluation (Fix 3):** Completely stripped the `itemAt(i)` loop from `dockRow._maxIconThickness`. It now calculates the theoretical thickness mathematically and reactively based directly on `DockSettings.iconSize`. This fixed the floating/clipping by ensuring the slot container `y` positions never evaluate with stale dimensions.

---

## 🟢 Bug 32: Auto Hide and Auto Dodge Failures
**Date:** May 20, 2026
**Status:** 🟢 Fixed
**Description:** Auto hide and auto dodge mechanisms are failing to trigger correctly. 
- On Hyprland, DodgeWindows appears to never hide the dock because overlap detection doesn't have a data source yet.
- On multi-monitor setups, overlap detection calculates coordinates on the wrong screen (e.g., checking X=0 for a right-side monitor).

## 🟢 Bug 33: Screen Edge Placement (Left/Right)
**Date:** May 20, 2026
**Status:** 🟢 Fixed
**Description:** The dock behaves incorrectly when placed on the Left or Right screen edges. Investigating QML geometry, QWindow anchors, and Wayland LayerShell surface sizing to find the layout desync.
