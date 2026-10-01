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

# Build and install an Arch package from the local checkout (HEAD plus
# uncommitted changes to tracked files), not the released tarball that
# packaging/arch/PKGBUILD downloads.
package:
    #!/usr/bin/env bash
    set -euo pipefail
    describe=$(git describe --tags --long --match 'v*')
    pkgver=$(sed -E 's/^v//; s/-([0-9]+)-(g[0-9a-f]+)$/.r\1.\2/' <<<"$describe")
    workdir=build/arch-package
    rm -rf "$workdir"
    mkdir -p "$workdir"
    tree=$(git stash create)
    git archive --format=tar.gz --prefix="krema-$pkgver/" \
        -o "$workdir/krema-$pkgver.tar.gz" "${tree:-HEAD}"
    sed -e "s|^pkgver=.*|pkgver=$pkgver|" \
        -e "s|^source=.*|source=(\"krema-$pkgver.tar.gz\")|" \
        -e "s|^sha256sums=.*|sha256sums=('SKIP')|" \
        packaging/arch/PKGBUILD > "$workdir/PKGBUILD"
    cd "$workdir"
    makepkg -si
    kbuildsycoca6 --noincremental

# OBS 로컬 빌드 테스트
obs-build-rpm distro="openSUSE_Tumbleweed" arch="x86_64":
    osc build {{distro}} {{arch}} packaging/obs/krema.spec

obs-build-deb distro="Debian_13" arch="x86_64":
    osc build {{distro}} {{arch}} packaging/obs/debian.control

distro-update-digests target="all":
    tests/distro/update-digests.sh {{target}}

# Install .desktop file and app icons for development (KWin Wayland protocol access)
dev-desktop:
    #!/usr/bin/env bash
    set -euo pipefail
    mkdir -p ~/.local/share/applications
    sed -e 's|@KDE_INSTALL_FULL_BINDIR@/krema|'"$PWD"'/build/dev/bin/krema|' \
        -e '/^NoDisplay=/d' \
        src/com.bhyoo.krema.desktop.in > ~/.local/share/applications/com.bhyoo.krema.desktop
    for png in src/icons/*-apps-com.bhyoo.krema.png; do
        size=$(basename "$png" | cut -d- -f1)
        install -Dm644 "$png" ~/.local/share/icons/hicolor/${size}x${size}/apps/com.bhyoo.krema.png
    done
    install -Dm644 src/icons/sc-apps-com.bhyoo.krema.svg ~/.local/share/icons/hicolor/scalable/apps/com.bhyoo.krema.svg
    kbuildsycoca6 --noincremental
    echo "Installed dev launcher and icons under ~/.local/share"

# Remove dev .desktop file
dev-desktop-clean:
    @rm -f ~/.local/share/applications/com.bhyoo.krema.desktop ~/.local/share/icons/hicolor/*/apps/com.bhyoo.krema.*
    @echo "Removed dev .desktop file and icons"
