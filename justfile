default:
    @just --list

configure:
    cmake --preset dev

build:
    cmake --build --preset dev

release:
    cmake --preset release
    cmake --build --preset release

test:
    ctest --preset dev

run:
    ./build/dev/src/krema

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
