# Krema Development Roadmap

> A lightweight dock for KDE Plasma 6. Spiritual successor to Latte Dock.
>
> **Core principle:** Not a generic Qt/Wayland app — a **KDE Plasma-native** application.
> Actively leverages KDE Frameworks, LibTaskManager, Kirigami, and other official KDE/Plasma libraries.

---

## Milestone 1: Foundation ✅

- [x] Project structure (CMake + ECM + C++23)
- [x] Wayland platform backend (LayerShellQt — wlr-layer-shell)
- [x] DockPlatform abstraction layer (Plasma Wayland only)
- [x] BackgroundStyle abstraction (PanelInheritStyle — inherits Plasma panel style)
- [x] DockModel (LibTaskManager wrapper — based on TasksModel)
- [x] QML dock UI (parabolic zoom, indicator dots, tooltips)
- [x] Multi-distro packaging setup (Arch, RPM, DEB, OBS)
- [x] TasksModel C++ initialization (classBegin/componentComplete)
- [x] TaskIconProvider (QIcon::fromTheme → QML Image)
- [x] Dock visibility modes — 3 types (AlwaysVisible, AutoHide, DodgeWindows, with a DodgeActiveOnly option)
- [x] QML slide/fade animations (hide/show)

---

## Milestone 2: Context Menu + Settings Persistence ✅

First step toward making the dock truly usable.
Right-click menu for basic operations, and settings saved to file for persistence across restarts.

- [x] Right-click context menu (C++ QMenu — KDE Breeze native)
  - [x] Pin/unpin toggle
  - [x] Close (all windows)
  - [x] Launch new instance
  - [x] App name + separator display
- [x] KWin Wayland protocol access (.desktop file + X-KDE-Wayland-Interfaces)
- [x] Mouse wheel: cycle between windows of the same app on hover (single window = focus)
- [x] KConfig-based settings management
  - [x] Persistent pin launcher list save/load
  - [x] Visibility mode save/restore
  - [x] Icon size, zoom factor, spacing, corner radius save/restore
  - [x] Dock position (Bottom/Top/Left/Right) save/restore
- [x] Restore settings from config file on startup

---

## Milestone 3: Keyboard Shortcuts + Launch Animation ✅

Control the dock via global shortcuts and provide visual feedback on app launch.
Leverages KDE Plasma's global shortcut framework.

- [x] Global shortcut registration (KF6::KGlobalAccel)
  - [x] Toggle dock show/hide (Meta+\`)
  - [x] Meta+Number(1-9) to activate Nth app
  - [x] Meta+Shift+Number to launch new instance
- [x] App launch bounce animation (icon bounces on launcher click)
- [x] Startup notification integration (uses TasksModel IsStartup role)

---

## Milestone 4: Drag & Drop ✅

Dock item reordering and file drag support.
Resolves the issue where pin/unpin doesn't immediately update position in the dock.

- [x] Drag reorder dock items (including post-pin position changes)
- [x] File drag: drop onto app to open with that app
- [x] URL/desktop file drag to add launchers

---

## Milestone 5: Settings UI + KDE Plasma Native Integration ✅

Settings dialog for GUI-based dock customization.
Introduces Kirigami/KDE Plasma APIs across QML UI for enhanced native integration.

- [x] Kirigami adoption (QML hardcoded values → Kirigami.Units/Theme)
- [x] KColorScheme adoption (QPalette → KDE color scheme)
- [x] Full KDE theme color integration
  - [x] Indicator dot colors (light/dark mode support)
  - [x] Tooltip background/text colors
  - [x] Dock background color
  - [x] Replace all hardcoded colors with KColorScheme/Kirigami.Theme
- [x] ~~KIconThemes adoption~~ (unnecessary — QIcon::fromTheme already works correctly on KDE Plasma via KIconThemes plugin)
- [x] i18n support (KLocalizedString for all user-facing strings)
- [x] Kirigami-based settings dialog (separate window)
  - [x] Icon size, zoom factor, spacing sliders
  - [x] Visibility mode selection (radio buttons)
  - [x] Dock position selection
  - [x] Background opacity slider
  - [x] Background style selection (delivered in M7)
- [x] "Settings..." entry in dock context menu

---

## Milestone 6: Window Previews ✅

Preview popup showing window thumbnails on mouse hover.

- [x] Window thumbnail preview on mouse hover (PipeWire-based)
- [x] Multi-window preview list for grouped apps
- [x] Click preview to activate window
- [x] Close button in preview

---

## Milestone 7: Background Styles + Visual Polish ✅

Visual refinement and polish.

- [x] Additional background styles (alongside Panel Inherit)
  - [x] Transparent
  - [x] Tinted (custom color or system color + opacity)
  - [x] Acrylic / Frosted Glass (blur + noise texture)
- [x] Attention-demanding animations
  - [x] Six animation styles (Bounce, Wiggle, Pulse, Glow, Dot color, Blink)
  - [x] Badge count display (Number, Dot, Off modes)
  - [x] Task progress bar on dock icons
  - [x] Attention auto-stop with configurable duration
  - [x] Do Not Disturb integration
  - [x] "Clear Notifications" context menu action
- [x] Icon size normalization option
  - [x] Auto-detect icon internal padding and scale (default: enabled)
  - [x] Option to use original icon size as-is

---

## Milestone 8a: Virtual Desktop Filtering ✅

- [x] QML singleton → context property refactor (multi-DockShell prerequisite)
- [x] Virtual desktop filtering (current desktop only / all desktops)
- [x] Other-desktop window icon opacity dimming
- [x] Settings UI: virtual desktop mode toggle

---

## Milestone 8b: Multi-Monitor Core ✅

- [x] MultiDockManager (replaces single DockShell in Application)
- [x] All Screens mode (one dock per screen)
- [x] Primary Only mode
- [x] Selected monitors mode with output-name switches, retained disconnected selections, and temporary primary fallback
- [x] Screen hot-plug handling (with debounce)
- [x] Global shortcut policy (primary dock target)
- [ ] PipeWire global stream cap (shared across PreviewControllers)
- [ ] App list filter policy toggle (all apps vs per-screen)
- [x] Settings UI: Monitor Mode selector

---

## Milestone 8c: Per-Screen Settings ✅

- [x] ScreenSettings two-tier KConfig architecture (global defaults + per-screen overrides)
- [x] Per-monitor icon size, edge, visibility, background, zoom factor, floating, corner radius override
- [x] Per-monitor pinned launchers override
- [ ] Settings UI: per-screen settings page

---

## Milestone 8d: Follow Active Mode ✅

- [x] Follow Active logic in MultiDockManager (mouse / focus / composite triggers)
- [x] Screen transition via show/hide (Fade/Slide/Instant settings prepared)
- [x] Debounce (300ms) for screen switch events
- [x] Settings UI: trigger + transition effect selector
- [ ] QML fade/slide transition animations (currently instant show/hide)

---

## Milestone 9: Widget System + System Tray ⬅️ 현재

Extensible widget architecture.

- [ ] DockWidget module system
  - [ ] Widget slot system (left/center/right areas)
  - [ ] Separator widget
  - [ ] Spacer widget
- [ ] System tray integration
  - [ ] StatusNotifierItem protocol support
  - [ ] Display system tray icons in dock
  - [ ] Tray icon click/right-click menus

---

## Future Exploration

- [ ] X11 platform backend (NET_WM hints, strut, input region) — low priority since KDE Plasma 6 is Wayland-first
- [ ] Plasmoid hosting feasibility study
- [ ] Liquid Glass background effect (multi-layer refraction)
- [ ] App menu / trash can widgets
- [x] CI pipeline (GitHub Actions)
- [x] Comprehensive test suite (Catch2 unit/integration/KWin, Qt Quick Test QML, selenium-webdriver-at-spi E2E)

---

## Tech Stack

| Component | Technology |
|---|---|
| Language | C++23 |
| Desktop Environment | **KDE Plasma 6** (Wayland only) |
| UI Framework | Qt 6.8+ / Qt Quick |
| KDE Frameworks | KDE Frameworks 6.0+ (Config, WindowSystem, I18n, CoreAddons, DBusAddons, GlobalAccel, IconThemes) |
| KDE Plasma Libraries | LibTaskManager, LibNotificationManager, LayerShellQt, KPipeWire |
| Additional KDE Libraries | Kirigami, Kirigami Addons, KColorScheme, KService, KCrash, KXmlGui |
| Rendering | QRhi (automatic Vulkan/OpenGL selection) |
| Build System | CMake + ECM + Ninja |
| Packaging | PKGBUILD (AUR), OBS (RPM/DEB) |
| Testing | Catch2 3.x, Qt Quick Test, selenium-webdriver-at-spi (E2E) |
| License | GPL-3.0-or-later |

## Minimum Requirements

| Dependency | Minimum Version |
|---|---|
| **KDE Plasma** | **6.0.0** |
| Qt | 6.8.0 |
| KDE Frameworks | 6.0.0 |
| LayerShellQt | 6.0.0 |
| CMake | 3.22 |
| Wayland | 1.22 |
| C++ Compiler | GCC 13+ / Clang 17+ |

## Architecture Overview

```
┌─────────────────────────────────────────┐
│           QML UI (Qt Quick)              │
│  main.qml, DockItem.qml, animations     │
│  Kirigami.Units/Theme, FormCard settings │
├─────────────────────────────────────────┤
│           C++ Backend Layer              │
│  ┌──────────┐  ┌───────────────────┐    │
│  │ DockModel│  │DockVisibility     │    │
│  │(LibTask  │  │Controller         │    │
│  │ Manager) │  │(3 visibility modes)│    │
│  └──────────┘  └───────────────────┘    │
│  ┌──────────────┐ ┌────────────────┐    │
│  │ DockSettings │ │TaskIconProvider│    │
│  │ (KConfig)    │ │(QIcon→Pixmap)  │    │
│  └──────────────┘ └────────────────┘    │
│  ┌──────────────────────────────────┐   │
│  │PreviewController (KPipeWire)     │   │
│  │Layer-shell overlay + PipeWire    │   │
│  └──────────────────────────────────┘   │
├─────────────────────────────────────────┤
│     DockView (QQuickView)                │
│  ┌───────────────┐ ┌────────────────┐   │
│  │ BackgroundStyle│ │KWindowEffects  │   │
│  │(4 styles)     │ │(blur/contrast) │   │
│  │               │ │                │   │
│  └───────────────┘ └────────────────┘   │
├─────────────────────────────────────────┤
│   WaylandDockPlatform (LayerShellQt)     │
│   KDE Plasma 6 Wayland only             │
└─────────────────────────────────────────┘
```
