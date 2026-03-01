# Krema Feature Catalog

> Update after each milestone completion using the `feature-analyst` agent.
> Last updated: 2026-03-01 (post-M7 background styles + accessibility + icon normalization)

## Core Dock

| Feature | User Benefit | Differentiator | Status |
|---------|-------------|----------------|--------|
| Parabolic Zoom | macOS-style icon magnification on hover | Only Plasma 6 dock with this | M1 |
| Indicator Dots | Visual indicator for running apps | Standard | M1 |
| Tooltips | App name on hover | Standard | M1 |
| Pin/Unpin | Keep favorite apps in dock | Standard | M2 |
| Context Menu | Right-click for pin, close, new instance | KDE Breeze native menu | M2 |
| Mouse Wheel Cycling | Scroll to switch between windows of same app | Unique UX | M2 |
| Dock Position | Bottom / Top / Left / Right | Standard | M2 |
| Icon Normalization | Auto-detect icon padding and scale for uniform size | Unique polish | M7 |

## Keyboard Shortcuts

| Feature | User Benefit | Differentiator | Status |
|---------|-------------|----------------|--------|
| Meta+\` Toggle | Quick dock show/hide | KDE Global Shortcut | M3 |
| Meta+1-9 Activate | Launch Nth app by keyboard | Power user | M3 |
| Meta+Shift+1-9 New Instance | New instance by keyboard | Unique | M3 |
| Bounce Animation | Visual feedback on app launch | Polish | M3 |

## Drag & Drop

| Feature | User Benefit | Differentiator | Status |
|---------|-------------|----------------|--------|
| Item Reorder | Drag to rearrange dock items | Standard | M4 |
| File Drop | Drop file onto app to open with it | Power user | M4 |
| Launcher Add | Drag .desktop file to add launcher | Convenience | M4 |

## Window Previews

| Feature | User Benefit | Differentiator | Status |
|---------|-------------|----------------|--------|
| PipeWire Thumbnails | Live window preview on hover | Modern, GPU-efficient | M6 |
| Multi-Window List | All windows of grouped app | Standard modern dock | M6 |
| Click-to-Activate | Click preview to switch to window | Standard | M6 |
| Preview Close Button | Close window from preview popup | Convenience | M6 |

## Visual & Customization

| Feature | User Benefit | Differentiator | Status |
|---------|-------------|----------------|--------|
| Panel-Inherit Background | Match Plasma panel style | Unique KDE integration | M7 |
| Transparent Background | Fully transparent dock | Standard | M7 |
| Tinted Background | Custom color + opacity | Customization | M7 |
| Acrylic / Frosted Glass | Blur + noise texture effect | Premium visual | M7 |
| Mica Background | System accent color based, Windows 11 Mica inspired | Unique on Linux | M7 |
| Adaptive Opacity | Auto-switch to opaque when windows overlap | Smart UX | M7 |
| KDE Theme Colors | Auto-adapts to Plasma theme | Deep KDE integration | M5 |
| Kirigami Settings UI | Native KDE settings dialog | KDE-native UX | M5 |

## Accessibility

| Feature | User Benefit | Differentiator | Status |
|---------|-------------|----------------|--------|
| Keyboard Navigation | Meta+F5 focus, arrow keys, Enter/Escape | Full keyboard control | M7 |
| AT-SPI Screen Reader | Accessible names, roles, and announcements | Inclusive design | M7 |
| Visual Focus Ring | Clear focus indicator with Kirigami styling | KDE-native a11y | M7 |
| Preview Keyboard Access | Navigate preview thumbnails via keyboard | Unique a11y depth | M7 |

## Visibility Modes

| Mode | Behavior | Status |
|------|----------|--------|
| Always Visible | Dock always shown | M1 |
| Always Hidden | Hidden, show on hover at screen edge | M1 |
| Dodge Windows | Hide when window overlaps dock area | M1 |
| Smart Hide | Hide when active window overlaps | M1 |

## KDE Integration Points

| Integration | Technology | User Benefit |
|-------------|-----------|--------------|
| LibTaskManager | Window/app management | Native task tracking |
| KConfig / KConfigXT | Settings persistence | Plasma-standard settings |
| KGlobalAccel | Global shortcuts | System-wide shortcuts |
| Kirigami | Settings UI | Native KDE look and feel |
| KColorScheme | Theme colors | Automatic theme adaptation |
| KPipeWire | Window previews | GPU-efficient screen capture |
| LayerShellQt | Wayland positioning | Proper dock behavior |
| KCrash | Crash reporting | KDE standard diagnostics |

## Unique Selling Points (Marketing Emphasis)

1. **Only actively developed dock with parabolic zoom for KDE Plasma 6**
2. **PipeWire-based window previews** — modern, GPU-efficient
3. **6 background styles** including acrylic frosted glass and Mica
4. **Deep KDE integration** — 8+ KDE libraries, not a generic Qt app
5. **Wayland-native** — Layer Shell protocol, no X11 compatibility layers
6. **Full keyboard accessibility** — Meta+F5 navigation with AT-SPI screen reader support
7. **Global keyboard shortcuts** — Meta+1-9 for instant app access
8. **Mouse wheel window cycling** — unique UX innovation
9. **Icon size normalization** — auto-detect padding for uniform icon appearance
