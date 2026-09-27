# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed

- Build compatibility with LayerShellQt < 6.4 (Ubuntu 25.04, Debian 13)
- Build compatibility with strict `QT_NO_CAST_FROM_ASCII` flag on non-Arch distributions
- Fedora Rawhide OBS resolver preferences for current ICU and systemd packages
- openSUSE Slowroll OBS support corrected to x86_64, matching upstream Slowroll architecture availability
- openSUSE Leap 16.0 OBS compiler dependency aligned with Krema's GCC 13 minimum
- openSUSE Docker GUI runtime images now include the `dbus-run-session` provider required by package smoke tests
- Debian/Ubuntu and openSUSE runtime package dependency names in OBS packaging metadata so the package installs cleanly against current distribution repositories (release bumped to 0.7.0-2)
- Launching Krema from Kickoff/KRunner/Application menu no longer fails with "The name com.bhyoo.krema was not provided by any .service files"; the desktop entry no longer declares `DBusActivatable` without a matching D-Bus service file (#18)
- The dock and window-preview surfaces now follow the Plasma primary output (kde_output_order_v1) instead of the first-announced Wayland output, and they migrate when the primary output is changed in System Settings (#18)
- Debian/Ubuntu package now declares all runtime QML module dependencies (Kirigami Addons settings/formcard, QtQuick.Effects) so the Settings dialog and dock UI load on minimal installs without recommended packages (packaging release bumped to 0.7.0-3)
- Krema no longer crashes when you change "Monitor mode" in Settings; the new mode applies and the Settings window stays open (#16)
- Docks recreated by a monitor mode change now stay visible while the Settings window is open, and docks on different screens share one Settings window instead of opening one each
- "All monitors" and "Follow active screen" modes now place each dock on its own screen instead of stacking every dock on the primary screen, and switching back no longer crashes Krema the next time a window (such as Settings) opens
- Switching monitor mode no longer leaves an invisible window preview surface behind for each dock it replaced
- "Toggle Dock", "Focus Dock" and the Meta+number shortcuts now act on the visible dock in "Follow active screen" mode instead of the hidden primary-screen dock
- On Debian 13 and Ubuntu 25.04 (kirigami-addons 1.7), choosing "Settings..." again now brings the open Settings window forward instead of opening another one, and the dock stays visible while Settings is open (#24)
- Krema builds again against LayerShellQt < 6.6 (Debian 13, Ubuntu 25.04), which lacks `Window::setScreen`, and on those versions the dock and its window previews now appear on their intended output (the Plasma primary output, or each screen in "All monitors" mode) instead of all landing on the first output
- Krema no longer crashes on Debian 13 and Ubuntu 25.04 when you quit it while the Settings window is still opening or open (#27)
- Wayland dock context menus now map as transient popups of the layer-shell dock surface and open at the actual pointer or keyboard-focused item position

### Added

- Multi-monitor support with three modes: Primary Only, All Screens, and Follow Active Screen
- Per-screen settings override: each monitor can have independent icon size, edge, visibility mode, background, and pinned launchers
- Follow Active Screen mode with three trigger types: mouse position, active window focus, and composite
- Virtual desktop filtering: show windows from current desktop only, or all desktops with dimmed icons for other desktops
- Fedora (COPR), openSUSE (OBS), Debian, and Ubuntu packaging support
- Compile-time LayerShellQt API detection for cross-distribution compatibility
- OBS build targets for Fedora 44 and openSUSE Leap 16.0
- Docker GUI runtime smoke images for installing externally built distro packages on isolated KWin virtual displays
- Docker GUI smoke screenshots and Krema process readiness checks for package runtime validation
- Deterministic unprivileged KWin/Wayland UI frame regression tests with toleranced endpoint checks, bounded transition divergence, and inline review previews

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
