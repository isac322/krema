# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Added local Flatpak packaging files and prepared KDE Store and AlternativeTo submission kits
- Published a Launchpad project page with the app icon, logo, and branding image
- Added zoom animation presets in Appearance (Natural, Quick, Relaxed, and Instant) and a Custom tab with separate zoom-in and zoom-out duration and easing
- `krema --version`, `--help`, `--author` and `--license` now print their information and exit, also without a display (handy for package smoke checks) and while the dock is already running; unknown options stop with an error instead of being silently ignored
- Added separate opacity settings for icons of active windows, minimized windows, and inactive windows or launchers

### Changed

- Hover zoom now eased in and out by default (180 ms in, 240 ms out at normal animation speed) for a smoother, macOS-like magnification
- In place zoom now followed the pointer directly and only animated when the pointer entered or left the dock, like Parabolic
- Zoom animation presets replaced the single zoom duration setting; a previously customized duration was kept as a Custom setting with the old ease-out curve
- Icon spacing can now be set up to 64 pixels instead of 16
- Dock right-click menu entries gained their standard KDE icons
- The shadow settings were relabeled "Light height" and "Panel elevation" (formerly "Light Z (height)" and "Elevation"), the light height slider moved in steps of 50, and the light radius value stopped claiming to be in pixels

### Fixed

- Pinned app-store screenshots to immutable image sources and corrected the settings screenshot caption
- KWin's Slide Back effect never fired while Krema was running, because Krema's always-mapped window preview surface counted as an ordinary window stacked above all others; raising a window, including from the dock, slid covering windows aside again
- Apps pinned through a `.desktop` file path (as stored by hand-edited or imported configurations) are now recognized as pinned: the context menu offers "Unpin from Dock" instead of "Pin to Dock" and unpinning works, and "Separate pinned and running apps" keeps them on the pinned side

## [0.10.0] - 2026-10-05

### Added

- Krema now ships its own app icon, shown in the application launcher, task switcher, Discover, and window title bars
- Added a "Zoom animation duration" setting in Appearance for Parabolic and In place hover zoom, with a range of 0 to 1000 ms (default 100 ms, an unscaled baseline that preserves the normal-speed zoom feel); Plasma animation scaling still applies, and 0 ms makes the zoom snap instantly
- Added independent left-click choices for single and grouped windows: optional active-window minimization or grouped previews, with activation and grouped cycling kept as defaults
- Added "Selected monitors" in Behavior settings so users could keep docks on chosen outputs, retain disconnected selections, and use a temporary primary-display dock until a selected monitor returned
- Added an enabled-by-default "Reserve screen space" setting for Always visible mode, so maximized windows avoid the dock while Auto hide and Dodge windows retain their existing behavior.
- Added an optional "Separate pinned and running apps" setting, keeping pinned apps together before unpinned running apps, reserving a themed separator slot with normal icon spacing on each side, and keeping cross-boundary drags in their section without pinning or unpinning.

### Changed

- The default Focus Dock shortcut moved from Meta+F5 to Meta+Alt+D: Meta+F5 belongs to KWin's "Move Mouse to Focus", so it never reached the dock and was left unbound on Plasma older than 6.7. Setups still on the old default switched automatically

### Fixed

- Corrected the Debian/Ubuntu package copyright notice to include the full MIT-CMU terms for the bundled KDE protocol.
- Application icons supplied by KDE/Wayland task metadata stayed visible when they lacked a theme name and refreshed when delayed metadata arrived.
- Minimized live previews now retained their application icon when PipeWire was ready instead of becoming blank.
- Changing visibility mode while the Settings window was open now released or applied the dock's screen-space reservation immediately.
- The README now lists the four available background styles and no longer advertises unsupported Semi-Transparent or Mica styles
- Windows minimized with KWin's Magic Lamp or Squash effects now animate toward their Krema dock icon instead of an unrelated fallback position
- The software-center description (Discover, GNOME Software) now lists the four available background styles and no longer advertises unsupported Mica or adaptive opacity
- "Follow active screen" with the "Mouse position" trigger moved the dock: pushing the pointer against the dock edge of another monitor brought the dock there
- Changing the icon size or screen edge resized and moved the dock immediately on distributions with LayerShellQt older than 6.4 (e.g. Debian 13, Ubuntu 25.04) instead of only after a restart
- Preserved the current positive dock window size before changing layer-shell anchors, preventing KWin from rejecting an intermediate zero-axis configure during edge transitions
- Clicking a dock icon worked when the icon had just appeared or moved under a resting pointer; the click was previously ignored or went to the wrong icon
- Clicking a pinned app bounced its icon until the app's window appeared, including on sessions without startup notifications
- Launching a new instance of a running app (middle click or New Instance) kept the launch bounce going until the new window appeared, instead of stopping after half a second
- Pressing Escape while dragging a dock icon cancelled the drag and kept the original order
- Releasing a dock-item drag after leaving and re-entering the dock no longer activated or minimized a window or opened a grouped preview; the next click still followed its configured action
- Leaving dock keyboard navigation with Escape returned keyboard focus to the previously active window, so typing worked right away and "Dodge active window" hid the dock again
- Moving the mouse anywhere on screen ended dock keyboard navigation, not only when the pointer moved over the dock
- The window preview no longer captured the mouse in the invisible area around it, which had ended thumbnail keyboard navigation (e.g. after closing a window with Delete) and blocked clicks beneath the preview
- A window's hover preview no longer reopened or stayed open after the pointer left the dock through the preview while the dock re-centred for a newly opened window
- Grouped previews on left and right docks now grow their native surface to contain the laid-out thumbnail row, so the last thumbnail remains clickable instead of passing input to the window underneath
- Window previews stayed open after fast pointer entry, instead of closing under a resting pointer while their Wayland input region was still pending
- The website's settings demo no longer stuttered while scrolling in Firefox on phones

## [0.9.0] - 2026-09-28

### Added

- A "Zoom style" option in Appearance settings: Parabolic (default, neighbours move aside) or In place (the previous behaviour, where magnified icons overlapped)

### Changed

- Hover zoom now makes room macOS-style: magnified icons push their neighbours aside and the dock background grows, keeping the icon under the pointer; in the middle of the dock the background edges and far icons stay still, and near an end the dock grows smoothly toward it without shaking

### Fixed

- Hovering and clicking dock icons worked for every icon on left and right docks (previously only the first icon responded), and zoomed icons on a top dock stayed hovered across their whole enlarged area
- Fixed the whole dock surface being blurred at startup; blur now covers only the visible panel from launch
- Setting the background opacity to 0% made the dock background fully transparent and removed the blur, instead of silently saving 10%
- Icon scale, attention animation duration, badge display mode, "Use system color", "Use accent color" and "Only dodge active window" now persist across restarts instead of reverting to their defaults
- Fixed tooltips on left and right docks being cut off after a few characters; they now show the full app name, and very long names end with an ellipsis
- Fixed the window preview popping up when a window opened while a dock icon was hovered (for example right after clicking a launcher), even with "Enable window preview" turned off
- App badge counts and progress bars sent through the Unity LauncherEntry API (for example download progress or unread counts) show on dock icons again on Plasma 6.6 and later, where they had stopped appearing; these badges also stay visible during Do Not Disturb, like Krema's other badges

## [0.8.0] - 2026-09-28

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
- Show Desktop (Meta+D) no longer hides the dock; KWin now treats Krema as a dock, like Plasma panels (#16)
- Krema no longer crashes on Debian 13 and Ubuntu 25.04 when you quit it while the Settings window is still opening or open (#27)
- Window previews now appear next to the hovered icon when the dock is on a monitor that does not start at the top-left corner of the desktop, instead of being pushed to that monitor's far edge
- Left-clicking an app with two or more open windows now brings up the window you used last, and each further click switches to the app's next window; previously the click did nothing. Scrolling over such an app from another app also starts at its last-used window instead of its first one (#10)

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
