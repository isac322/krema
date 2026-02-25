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
- [x] Dock visibility modes — 4 types (AlwaysVisible, AlwaysHidden, DodgeWindows, SmartHide)
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
  - [ ] Background style selection → moved to M7
- [x] "Settings..." entry in dock context menu

---

## Milestone 6: Window Previews ✅

Preview popup showing window thumbnails on mouse hover.

- [x] Window thumbnail preview on mouse hover (PipeWire-based)
- [x] Multi-window preview list for grouped apps
- [x] Click preview to activate window
- [x] Close button in preview

---

## Milestone 7: Background Styles + Visual Polish ⬅️ Current

Visual refinement and polish.

- [x] Additional background styles
  - [x] Semi-transparent
  - [x] Transparent
  - [x] Tinted (custom color + opacity)
  - [x] Acrylic / Frosted Glass (blur + noise texture)
  - [x] Mica (system accent color based)
  - [x] Adaptive Opacity (switch to opaque when windows overlap)
- [ ] Attention-demanding animations
  - [ ] Icon wiggle (IsDemandingAttention)
  - [ ] Badge count display (notification count)
  - [ ] Highlight glow effect
- [x] Icon size normalization option
  - [x] Auto-detect icon internal padding and scale (default: enabled)
  - [x] Option to use original icon size as-is

---

## Milestone 8: Multi-Monitor + Virtual Desktops

Multi-screen environment support.

- [ ] Multi-monitor
  - [ ] Per-screen dock instances
  - [ ] Active screen tracking (dock follows active screen)
  - [ ] Per-monitor dock settings
- [ ] Virtual desktops
  - [ ] Filter by current virtual desktop
  - [ ] Show all desktops option

---

## Milestone 9: Widget System + System Tray

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
- [ ] CI pipeline (GitHub Actions)
- [ ] Comprehensive test suite (Catch2)

---

## Tech Stack

| Component | Technology |
|---|---|
| Language | C++23 |
| Desktop Environment | **KDE Plasma 6** (Wayland only) |
| UI Framework | Qt 6.8+ / Qt Quick |
| KDE Frameworks | KDE Frameworks 6.0+ (Config, WindowSystem, I18n, CoreAddons, GlobalAccel) |
| KDE Plasma Libraries | LibTaskManager, LayerShellQt, KPipeWire |
| Additional KDE Libraries | Kirigami, Kirigami Addons, KColorScheme, KService, KCrash, KXmlGui |
| Rendering | QRhi (automatic Vulkan/OpenGL selection) |
| Build System | CMake + ECM + Ninja |
| Packaging | CPack (Arch/RPM/DEB), OBS |
| Testing | Catch2 3.x |
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
│  │ Manager) │  │(4 visibility modes)│    │
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
│  │(6 styles+     │ │(blur/contrast) │   │
│  │ adaptive)     │ │                │   │
│  └───────────────┘ └────────────────┘   │
├─────────────────────────────────────────┤
│   WaylandDockPlatform (LayerShellQt)     │
│   KDE Plasma 6 Wayland only             │
└─────────────────────────────────────────┘
```
