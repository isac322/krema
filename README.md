# Krema

[![WIP](https://img.shields.io/badge/Status-Work_in_Progress-yellow.svg)](#roadmap)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/License-GPL--3.0--or--later-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![KDE Plasma 6](https://img.shields.io/badge/KDE_Plasma-6-1d99f3.svg)](https://kde.org/plasma-desktop/)
[![Qt 6](https://img.shields.io/badge/Qt-6.6+-41cd52.svg)](https://www.qt.io/)

> **Note:** Krema is under active development. Core features (parabolic zoom, window previews, background styles, drag & drop, settings UI) are functional, but some features are still in progress. See the [Roadmap](ROADMAP.md) for details.

A lightweight, high-performance dock for KDE Plasma 6 — spiritual successor to [Latte Dock](https://github.com/KDE/latte-dock).

Krema brings back the beloved dock experience for KDE Plasma users who miss Latte Dock. Built from scratch with C++23, Qt 6, and KDE Frameworks 6, it delivers smooth parabolic zoom animations, live window previews via PipeWire, and deep native integration with the Plasma desktop.

<!-- TODO: Add screenshots here -->

## Features

- **Parabolic Zoom** — macOS-style icon magnification on hover
- **Window Previews** — Live PipeWire-based thumbnails on hover with multi-window support
- **Background Styles** — Panel-inherit, transparent, tinted, and acrylic (frosted glass) backgrounds
- **KDE Native Integration** — Kirigami UI, KDE color schemes, global shortcuts, and Plasma theme colors
- **Drag & Drop** — Reorder dock items, drop files onto apps, add launchers by dragging .desktop files
- **Smart Visibility** — Always visible, auto-hide, dodge windows, or smart hide modes
- **Global Shortcuts** — Meta+\` to toggle, Meta+1-9 to activate apps
- **Wayland Native** — Built on Layer Shell protocol for proper dock behavior
- **Settings UI** — Kirigami-based settings dialog for easy customization

## Installation

Krema packages are available via [Open Build Service (OBS)](https://build.opensuse.org/).

| Distribution | Architecture | Package Format |
|---|---|---|
| Arch Linux | x86_64, aarch64 | .pkg.tar.zst |
| Fedora 40, 41, 42 | x86_64, aarch64 | RPM |
| openSUSE Tumbleweed | x86_64, aarch64 | RPM |
| Ubuntu 24.04, 24.10 | amd64, arm64 | DEB |
| Debian Trixie (13) | amd64, arm64 | DEB |

## Building from Source

### Dependencies (Arch Linux / Manjaro)

```bash
sudo pacman -S --needed \
    cmake extra-cmake-modules ninja ccache just gcc \
    qt6-base qt6-declarative qt6-wayland \
    kwindowsystem kconfig kcoreaddons ki18n \
    layer-shell-qt wayland \
    catch2
```

### Minimum Versions

| Dependency | Minimum Version |
|---|---|
| KDE Plasma | 6.0.0 |
| Qt 6 | 6.6.0 |
| KDE Frameworks 6 | 6.0.0 |
| CMake | 3.22 |
| C++ Compiler | GCC 14+ / Clang 18+ |

### Build & Run

```bash
just configure    # cmake --preset dev
just build        # cmake --build --preset dev
just test         # ctest --preset dev
just run          # run krema
```

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the full development roadmap.

## Contributing

Contributions are welcome! Please open an issue to discuss your idea before submitting a pull request.

## License

Krema is licensed under [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt).
