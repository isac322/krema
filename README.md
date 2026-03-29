# Krema

[![WIP](https://img.shields.io/badge/Status-Work_in_Progress-yellow.svg)](#roadmap)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/License-GPL--3.0--or--later-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![KDE Plasma 6](https://img.shields.io/badge/KDE_Plasma-6-1d99f3.svg)](https://kde.org/plasma-desktop/)
[![Qt 6](https://img.shields.io/badge/Qt-6.8+-41cd52.svg)](https://www.qt.io/)

> **Note:** Krema is under active development. Core features are functional, but some features are still in progress. See the [Roadmap](ROADMAP.md) for details.

A lightweight, high-performance dock for KDE Plasma 6 — spiritual successor to [Latte Dock](https://github.com/KDE/latte-dock).

Krema brings back the beloved dock experience for KDE Plasma users who miss Latte Dock. Built from scratch with C++23, Qt 6, and KDE Frameworks 6, it delivers smooth parabolic zoom animations, live window previews via PipeWire, and deep native integration with the Plasma desktop.

<!-- TODO: Add screenshots — see docs/image-specs/ for capture instructions -->

## Why Krema?

- **Your dock is back** — Krema picks up where Latte Dock left off, purpose-built for Plasma 6
- **A dock that speaks Plasma** — Uses KDE's own frameworks (Kirigami, KConfig, KColorScheme, KGlobalAccel) and respects your theme, shortcuts, and desktop conventions
- **Lightweight by design** — GPU-accelerated rendering via QRhi, Wayland-native via Layer Shell, lazy resource allocation
- **Make it yours** — 6 background styles including acrylic frosted glass and Mica, configurable zoom, spacing, and position

## Features

### Core Dock
- **Parabolic Zoom** — macOS-style icon magnification on hover
- **Icon Size Normalization** — Auto-detects icon padding and scales for uniform appearance
- **Indicator Dots** — Visual markers for running applications
- **Pin/Unpin** — Keep favorite apps in the dock
- **Context Menu** — Right-click for pin, close, new instance (KDE Breeze native)
- **Mouse Wheel Cycling** — Scroll to switch between windows of the same app

### Visual Styles
- **Panel-Inherit** — Match your Plasma panel style automatically
- **Semi-Transparent** — Subtle translucency over your desktop
- **Transparent** — Fully transparent dock background
- **Tinted** — Custom color with adjustable opacity
- **Acrylic / Frosted Glass** — Blur + noise texture effect
- **Mica** — System accent color based, inspired by Windows 11 Mica
- **Adaptive Opacity** — Switches to opaque when windows overlap the dock

### Window Previews
- **PipeWire Thumbnails** — Live GPU-efficient window previews on hover
- **Multi-Window List** — All windows of grouped apps shown together
- **Click-to-Activate** — Click a preview to switch to that window
- **Preview Close Button** — Close windows directly from the preview popup

### Integration & Accessibility
- **KDE Native Integration** — Kirigami UI, KDE color schemes, Plasma theme colors
- **Wayland Native** — Built on Layer Shell protocol for proper dock behavior
- **Smart Visibility** — Always visible, auto-hide, dodge windows, or smart hide modes
- **Drag & Drop** — Reorder dock items, drop files onto apps, add launchers by dragging .desktop files
- **Settings UI** — Kirigami-based settings dialog (FormCard) for easy customization
- **Keyboard Accessibility** — Full keyboard navigation with AT-SPI screen reader support

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Meta+\` | Toggle dock visibility |
| Meta+1-9 | Activate Nth app |
| Meta+Shift+1-9 | Launch new instance of Nth app |
| Meta+F5 | Focus dock for keyboard navigation |
| Arrow keys | Navigate between dock items / preview thumbnails |
| Enter | Activate focused item |
| Escape | Exit keyboard navigation |

## Installation

[![Packaging status](https://repology.org/badge/vertical-allrepos/krema.svg)](https://repology.org/project/krema/versions)

| Distribution | Versions | Architectures |
|---|---|---|
| Arch Linux / Manjaro | Rolling | x86_64, aarch64 |
| Fedora | 42, 43, Rawhide | x86_64, aarch64 |
| openSUSE | Tumbleweed, Slowroll | x86_64, aarch64 |
| Ubuntu | 25.04, 25.10, 26.04 | x86_64, aarch64 |
| Debian | 13 (Trixie) | x86_64, aarch64 |

<details>
<summary><strong>Arch Linux / Manjaro (AUR)</strong></summary>

```bash
yay -S krema
# or
paru -S krema
```
</details>

<details>
<summary><strong>Fedora (COPR)</strong></summary>

```bash
sudo dnf copr enable isac322/krema
sudo dnf install krema
```
</details>

<details>
<summary><strong>openSUSE (OBS)</strong></summary>

```bash
# Tumbleweed
sudo zypper addrepo https://download.opensuse.org/repositories/home:isac322/openSUSE_Tumbleweed/home:isac322.repo
sudo zypper refresh
sudo zypper install krema

# Slowroll
sudo zypper addrepo https://download.opensuse.org/repositories/home:isac322/openSUSE_Slowroll/home:isac322.repo
sudo zypper refresh
sudo zypper install krema
```
</details>

<details>
<summary><strong>Ubuntu (PPA)</strong></summary>

```bash
sudo add-apt-repository ppa:isac322/krema
sudo apt update
sudo apt install krema
```

Alternatively, use the [OBS repository](https://download.opensuse.org/repositories/home:/isac322/) for Ubuntu 25.04/26.04.
</details>

<details>
<summary><strong>Debian 13 (OBS)</strong></summary>

```bash
echo 'deb http://download.opensuse.org/repositories/home:/isac322/Debian_13/ /' | sudo tee /etc/apt/sources.list.d/krema.list
curl -fsSL https://download.opensuse.org/repositories/home:isac322/Debian_13/Release.key | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/home_isac322.gpg > /dev/null
sudo apt update
sudo apt install krema
```
</details>

<details>
<summary><strong>Building from Source</strong></summary>

### Dependencies (Arch Linux / Manjaro)

```bash
sudo pacman -S --needed \
    cmake extra-cmake-modules ninja ccache just gcc \
    qt6-base qt6-declarative qt6-wayland qt6-shadertools \
    kwindowsystem kconfig kcoreaddons ki18n \
    kglobalaccel kcolorscheme kiconthemes kcrash kxmlgui kservice \
    kirigami layer-shell-qt wayland kpipewire plasma-workspace \
    catch2
```

### Minimum Versions

| Dependency | Minimum Version |
|---|---|
| KDE Plasma | 6.0.0 |
| Qt 6 | 6.8.0 |
| KDE Frameworks 6 | 6.0.0 |
| CMake | 3.22 |
| C++ Compiler | GCC 13+ / Clang 17+ |

### Build & Run

```bash
just configure    # cmake --preset dev
just build        # cmake --build --preset dev
just test         # ctest --preset dev
just run          # run krema
```

</details>

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the full development roadmap.

## Acknowledgments

Krema is a spiritual successor to [Latte Dock](https://github.com/KDE/latte-dock), which served the KDE community for years as the go-to dock application. We are grateful for the Latte Dock project and the community that built and maintained it. Krema aims to carry that spirit forward for KDE Plasma 6.

## Contributing

Contributions are welcome! Please open an issue to discuss your idea before submitting a pull request.

## License

Krema is licensed under [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt).
