# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- Fixed an interaction bug where normal mouse twitches would cancel the dock's un-hide timer in AutoHide mode, making the dock incredibly difficult to trigger. The QML hover region now dynamically expands alongside the Wayland input region to preserve hover hysteresis.

### Added
- **Dock Alignment Control:** Added a new alignment engine that allows positioning the dock at the Start (Left/Top), Center, or End (Right/Bottom) of the selected screen edge. The alignment strictly respects floating padding and corner radii to prevent edge bleeding.
- **Flatpak Icon Extractor:** Implemented direct resolution of Flatpak application icons from `~/.local/share/flatpak` and `/var/lib/flatpak`. This allows native icon rendering on standalone window managers like Hyprland, bridging the gap when `XDG_DATA_DIRS` does not include flatpak export paths.
- **Hyprland Support Foundation:** Introduced `HyprlandDockPlatform` to enable Krema to run on Hyprland sessions. This implementation uses standard layer-shell protocols for positioning while bypassing KWin-specific effects, ensuring architectural stability on non-KDE compositors.
- **Dev-Autostart & Environment Injection:**
 Updated `justfile` to generate a development autostart entry (`~/.config/autostart/`) that synchronizes with the `just run` environment. Both the development launcher and autostart now inject `XDG_DATA_DIRS` and `QT_PLUGIN_PATH` into their `Exec` lines, ensuring local plugins and themes load correctly upon login.
- **Island Internal Padding:** Introduced 8px internal padding to the IslandModule, completely decoupling the Island's glass pill border from the main Panel's external boundary to achieve a more premium spatial layout without breaking zoom logic.
- **Span Screen Aesthetics:** Upgraded the "Span Screen" layout engine. The dock mathematically tracks screen width/height up to 100% when flush, but enforces a strict 99% cap when Floating Mode is active to preserve edge separation.
- **Ultra-Thin Panel Configurations:** Lowered the absolute minimum configuration floor for `IconSize` from `24px` down to `12px`. Combined with fluid 0px margin clamps, users can now create ultra-minimalist docks that perfectly "hug" the inner elements.
- Multi-monitor support with three modes: Primary Only, All Screens, and Follow Active Screen
- Per-screen settings override: each monitor can have independent icon size, edge, visibility mode, background, and pinned launchers
- Follow Active Screen mode with three trigger types: mouse position, active window focus, and composite
- Virtual desktop filtering: show windows from current desktop only, or all desktops with dimmed icons for other desktops
- Fedora (COPR), openSUSE (OBS), Debian, and Ubuntu packaging support
- Compile-time LayerShellQt API detection for cross-distribution compatibility
- OBS build targets for Fedora 44 and openSUSE Leap 16.0
- Docker GUI runtime smoke images for installing externally built distro packages on isolated KWin virtual displays
- Docker GUI smoke screenshots and Krema process readiness checks for package runtime validation

### Fixed
- **Tooltip Wayland Clipping:** Expanded the Wayland Layer Shell invisible surface boundary by 230px. This completely resolves the issue where wide tooltips (like "System Settings") popping out from vertical docks were abruptly sliced off by the compositor canvas.
- **Vertical Dock Length Mismatch:** Removed an inverted math check in `main.qml` that forced vertical docks to calculate their maximum 100% length limit against the screen's *width* instead of its height.
- **Overflow State Persistence:** Elevated the `AllowOverflow` layout toggle into a permanent `krema.kcfg` setting. This stops the dock from defaulting to strict bounds on launch and silently deleting the user's custom tight-panel configurations.

### Fixed
- **Invisible Click-Blocking Wall:** Resolved a major Wayland Input Region bug where the dock's shadow bounds and a static hover override permanently expanded the physical click-interception area, preventing users from clicking windows behind the dock. The Input Region now dynamically shrinks to exactly trace the dock, expanding only when actively hovered.
- **Hit-Testing Off-Center Deadzone:** Fixed a bug where parabolic zoom physically shifted icons, but QML hit-testing remained rigidly anchored to the unzoomed center. The hitbox now mathematically maps to the real-time physical visual coordinate shift, ensuring flawless 2.0x zoom tracking on both edges.
- **Hyprland Blur Sharp Box:** Replaced perspective projection in the shadow shader (`outer_shadow.frag`) with orthographic projection. This prevents the shadow from growing proportionally on wide panels and being chopped off by the Qt bounding box, eliminating the sharp blurred rectangle artifact on Hyprland.
- **Vertical Indicator Wrapping:** Resolved a major layout engine bug in vertical mode where active indicator dashes would artificially wrap horizontally into the icon's visual space due to an aggressive 3px constraint. The flow layout now accurately stacks indicators TopToBottom alongside the icon.
- **Floating Dock on Wake:** Diagnosed and eliminated a Wayland stacking conflict where the dock's exclusive zone would snap on top of the Plasma taskbar instead of the screen edge after DPMS sleep or screen unlock. The application now safely performs a full `scheduleTopologyUpdate()` to enforce exact absolute anchors.
- **Development Sandbox Restriction (Ghost Bug):** Patched `justfile` to inject `kstart` when spawning Krema in IDE terminal emulators, permanently bypassing the Plasma 6 security sandbox that was silently starving the dock of Wayland IPC window metrics during development.
- **Preview Geometry Sync:** Resolved "Altitude Confusion" via Absolute Sync, ensuring window preview thumbnails and vertical offsets are perfectly synchronized with zoomed icons.
- **Flatpak Vector Icon Rendering:** Removed strict available sizes checks that incorrectly discarded valid SVGs, enabling perfect rendering of Flatpak vector graphics.
- **Fuzzy Flatpak Identity Resolver:** Updated the suffix matching algorithm to aggressively strip spaces and dashes, resolving edge cases where Wayland window classes (e.g., "aim train") mismatched their Flatpak identifiers.
- **Lua IPC Interception Bypass:** Resolved Hyprland context menu "Close" failure by implementing a dynamic fallback that rewrites raw socket dispatches to `hl.dsp.window.close()` when the `hyprland-lua-plugins` extension is active.
- **Active Indicator Desync:** Fixed an issue where the active window indicator failed to update when windows were focused externally (e.g. via Alt+Tab or window manager). The data model now guarantees a forced evaluation whenever a child's state changes.
- **Full-Surface Blur on Launch:** Fixed a startup blur bug where an empty `QRegion` passed to `KWindowEffects::enableBlurBehind` caused KWin to apply blur to the entire Wayland surface (500+ pixels tall), creating an oversized blurry block around the dock. Blur-using styles now defer until the panel geometry is reported by QML.


## [0.8.0] - 2026-05-12

### Added
- **Interaction Flooring Constitution (Rule 17):** Implemented a rigorous 5-unit vertical stack (Floor, Indicator, Gap, Icon, Ceiling) to ensure absolute mathematical consistency between visual rendering and interaction logic.
- **State-Aware Geometry Engine (Rule 15):** New layout system that dynamically toggles between a stable grid (idle state) and recursive displacement (interactive state), resolving architectural race conditions and startup gaps.
- **Absolute Sync Hit-Testing:** Mathematically precise interaction engine that aligns mouse orbits 1:1 with visual zoomed pixels, eliminating interaction drift and deadzones.
- **Absolute Zoom Independence (Rule 5):** Strict decoupling of dock panel thickness from icon magnification, ensuring a stable geometric foundation during the parabolic zoom wave.
- **Multi-Monitor System:** Full support for Primary Only, All Screens, and Follow Active Screen modes with integrated per-screen settings overrides for size, edge, and style.
- **Modular Settings Interface:** Completely redesigned multi-page UI using Kirigami FormCards with a nested visual hierarchy for streamlined configuration.
- **Advanced Visual Materials:** Hardware-accelerated Acrylic and Mica-style background themes with real-time blur and noise control.
- **Split Mode Layout:** Native task management separating pinned launchers from active windows with a reactive etched-glass separator pill.
- **Ghost Input Region:** System-level mouse tracking that decouples Wayland interaction from visual panel geometry, ensuring icons remain interactive when visually overflowing thin panels.
- **Architectural Blueprint Mode:** Specialized diagnostic view with a synchronized high-contrast drafting grid for live geometry verification.
- **Keyboard Navigation Core:** Integrated support for Meta+F5 activation and full arrow-key dock navigation.
- **Workspace Awareness:** Native task filtering based on Virtual Desktops and Plasma Activities.
- **Distribution Packaging:** Integrated configuration support for Arch, Fedora (COPR), openSUSE (OBS), Debian, and Ubuntu.

### Fixed
- **Sticky Preview Conflict:** Resolved interaction deadlock where window previews would trap the mouse hover state, blocking neighboring icons (fixed via Trial 4: Icon-Gated Visibility).
- **1.0x Interaction Orbit:** Resolved "Click-through" bug where thin panels would physically clip the interaction zone (fixed via Rule 18: Decoupled Catch Zone).
- **Blur Expansion Bug:** Isolated KWindowEffects background blur to the visual panel area, preventing frosted "ghost halos" in interaction deadzones.
- **Preview Misalignment:** Synchronized vertical thumbnail offsets using the `visualIconTop` bridge, ensuring previews float at a consistent altitude regardless of zoom state.
- **Settings UI Collision:** Resolved text overlaps in the configuration pages by implementing the **Safe Spacing Mandate (Rule 20)** and removing negative margins.
- **Shadow Clipping:** Restored natural volumetric shadow fade by correctly binding geometry to SDF padding.
- Build compatibility with LayerShellQt < 6.4 (Ubuntu 25.04, Debian 13)
- Build compatibility with strict `QT_NO_CAST_FROM_ASCII` flag on non-Arch distributions
- Fedora Rawhide OBS resolver preferences for current ICU and systemd packages
- openSUSE Slowroll OBS support corrected to x86_64, matching upstream Slowroll architecture availability
- openSUSE Leap 16.0 OBS compiler dependency aligned with Krema's GCC 13 minimum
- openSUSE Docker GUI runtime images now include the `dbus-run-session` provider required by package smoke tests
- Debian/Ubuntu and openSUSE runtime package dependency names in OBS packaging metadata so the package installs cleanly against current distribution repositories (release bumped to 0.7.0-2)

## [0.7.0] - 2026-03-28

### Added

- Progress bar on dock icons for apps reporting task progress (e.g. file copy in Dolphin)
- Badge display mode setting: Number, Dot, or Off
- Attention animation duration setting with auto-stop (default 5 seconds, 0 for infinite)
- "Clear Notifications" context menu action for manually dismissing notification badges
- Do Not Disturb integration — attention animations are suppressed when system DND is active

### Fixed

- Notification badges now always clear on focus, even when SmartLauncher count was previously active
- Attention animation no longer runs infinitely for persistent notification badges
- Dodge mode now correctly resumes after closing the settings window

## [0.6.0] - 2026-03-02

### Added

- Notification badge count on dock icons (via D-Bus RegisterWatcher and SmartLauncher Unity API)
- SNI (StatusNotifierItem) NeedsAttention monitoring for tray-based attention requests
- Six attention animation styles: Bounce, Wiggle, Pulse, Glow, Dot color, and Blink
- Attention animation setting in Appearance settings page
- Single-instance enforcement — launching Krema again while it's already running is now silently ignored
- Automatic startup on KDE Plasma login
- Desktop launcher entry visible in application menu

## [0.5.1] - 2026-03-01

### Fixed

- Fixed icon rendering on HiDPI displays when icon normalization or icon scale is active
- Fixed dock edge trigger unreachable in AutoHide/DodgeWindows mode when KDE panel occupies the screen edge

## [0.5.0] - 2026-02-22

Initial public release.

### Added

- Pinned app launcher with drag-and-drop reordering
- Running app tracking via KDE Task Manager
- Parabolic zoom animation on hover
- Live window preview on hover via PipeWire
- Middle-click to close windows from preview
- Context menu with pin/unpin, new instance, and quit actions
- AutoHide, DodgeWindows, and AlwaysVisible visibility modes
- Floating dock style with acrylic blur background
- Icon size normalization for visually consistent icons
- Icon scale setting for uniform icon padding
- Notification badge indicators
- KDE Plasma 6 native integration (Layer Shell, KConfig, Kirigami)
- Full keyboard accessibility (Meta+F5, arrow navigation, focus ring)
- Screen reader support via AT-SPI accessible properties
- Settings UI with Kirigami FormCard delegates
