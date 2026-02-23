default:
    @just --list

configure:
    cmake --preset dev

build: configure
    cmake --build --preset dev

release:
    cmake --preset release
    cmake --build --preset release

test:
    ctest --preset dev

run: build
    ./build/dev/bin/krema

format:
    ninja -C build/dev clang-format

clean:
    rm -rf build/

package:
    cd packaging/arch && makepkg -si

# OBS 로컬 빌드 테스트
obs-build-rpm distro="openSUSE_Tumbleweed" arch="x86_64":
    osc build {{distro}} {{arch}} packaging/obs/krema.spec

obs-build-deb distro="Debian_13" arch="x86_64":
    osc build {{distro}} {{arch}} packaging/obs/debian.control

# Install .desktop file for development (KWin Wayland protocol access)
dev-desktop:
    @mkdir -p ~/.local/share/applications
    @sed 's|@KDE_INSTALL_FULL_BINDIR@/krema|'$PWD'/build/dev/bin/krema|' \
        src/org.krema.desktop.in > ~/.local/share/applications/org.krema.desktop
    @echo "Installed dev .desktop file to ~/.local/share/applications/org.krema.desktop"
    @echo "Run: kbuildsycoca6 --noincremental"
    @# Clean up legacy dev desktop file if it exists
    @rm -f ~/.local/share/applications/org.krema.dev.desktop

# Remove dev .desktop file
dev-desktop-clean:
    @rm -f ~/.local/share/applications/org.krema.desktop
    @echo "Removed dev .desktop file"
