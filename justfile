default:
    @just --list

# Collaborative AI helper for QML and C++ tasks
ai:
    gemini --include-directories ./src,./scripts,./tools

# Robust configuration that skips ccache if not found on the system
configure:
    cmake --preset dev -DCMAKE_CXX_COMPILER_LAUNCHER=$(which ccache 2>/dev/null || echo "")

build: configure
    cmake --build --preset dev

release:
    cmake --preset release
    cmake --build --preset release

test:
    ctest --preset dev

# Run with optional flags: e.g., just run "--debug-geom"
run *args: build
    XDG_DATA_DIRS="$HOME/.local/share:/usr/local/share:/usr/share${XDG_DATA_DIRS:+:${XDG_DATA_DIRS}}" QT_PLUGIN_PATH="/usr/lib/qt6/plugins${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}" ./build/dev/bin/krema {{args}}

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

docker-runtime-images target="all":
    tests/docker/build-images.sh {{target}}

docker-runtime-update-digests target="all":
    tests/docker/update-digests.sh {{target}}

docker-runtime-publish target="all":
    tests/docker/publish-images.sh {{target}}

docker-runtime-smoke target package_dir:
    tests/docker/run-smoke.sh {{target}} {{package_dir}}

# Install .desktop files for development (KWin Wayland protocol access & autostart)
# IMPORTANT: Exec= must be a plain path — no shell wrappers or escapes.
# Broken Exec= lines cause kbuildsycoca6 to reject the file, which prevents
# KWin from granting X-KDE-Wayland-Interfaces (e.g. org_kde_plasma_window_management).
dev-desktop:
    @mkdir -p ~/.local/share/applications
    @mkdir -p ~/.config/autostart
    @sed -e 's|@KDE_INSTALL_FULL_BINDIR@/krema|'"$PWD"'/build/dev/bin/krema|' \
         -e '/^NoDisplay=/d' \
         src/com.bhyoo.krema.desktop.in > ~/.local/share/applications/com.bhyoo.krema.desktop
    @sed -e 's|@KDE_INSTALL_FULL_BINDIR@/krema|'"$PWD"'/build/dev/bin/krema|' \
         src/com.bhyoo.krema.autostart.desktop.in > ~/.config/autostart/com.bhyoo.krema.autostart.desktop
    @kbuildsycoca6 --noincremental
    @echo "Installed dev launcher to ~/.local/share/applications/com.bhyoo.krema.desktop"
    @echo "Installed dev autostart to ~/.config/autostart/com.bhyoo.krema.autostart.desktop"
    @echo "Sycoca cache rebuilt. KWin will now grant Wayland protocol access."
    @# Clean up legacy dev desktop files if they exist
    @rm -f ~/.local/share/applications/org.krema.dev.desktop
    @rm -f ~/.local/share/applications/org.krema.desktop

# Remove dev .desktop files
dev-desktop-clean:
    @rm -f ~/.local/share/applications/org.krema.dev.desktop
    @rm -f ~/.local/share/applications/org.krema.desktop
    @rm -f ~/.local/share/applications/com.bhyoo.krema.desktop
    @rm -f ~/.config/autostart/com.bhyoo.krema.autostart.desktop
    @echo "Removed dev .desktop files"
