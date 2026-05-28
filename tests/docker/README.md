# Docker GUI Runtime Smoke Images

These images are for distro-by-distro installation and GUI startup smoke tests of externally built Krema packages. They intentionally do not contain Krema. Build Krema with OBS/osc first, mount or copy the resulting package into a runtime container, install it inside that container, and launch it against an isolated virtual Wayland display.

## Architecture

Use one host-side `kwin_wayland --virtual` compositor per smoke test and mount only that compositor's Wayland socket into the container. This keeps the distro image small while still using the KDE compositor that is closest to Krema's production surface. Each concurrent test gets a unique runtime directory, socket name, D-Bus session, container name, and package workspace.

`cage` is not the default because it is a kiosk compositor. It is useful for generic Wayland client experiments, but it does not provide the KDE/Plasma compositor behavior that a Plasma dock needs for a meaningful smoke test. The KWin virtual backend is also not a full Plasma desktop, so the smoke test covers installability, Wayland connection, Qt/KDE runtime loading, and basic layer-shell startup without an immediate crash. Real Plasma behavior such as `LibTaskManager` window population, global shortcuts, and PipeWire window previews still requires the existing real or kwin-mcp E2E path.

## Targets

`targets.tsv` mirrors the supported OBS/package matrix used by the project docs:

- openSUSE Tumbleweed, Slowroll, and Leap 16.0
- Fedora 42, 43, 44, and Rawhide
- Debian 13
- Ubuntu 25.04, 25.10, and 26.04

The default image name is `krema/gui-runtime:<target-id>`.

Slowroll uses `opensuse/tumbleweed:latest` as the container base because there
is no official `opensuse/slowroll` runtime image. The Dockerfile disables the
base repositories and enables the official Slowroll OSS and update repositories
before installing runtime dependencies.

## Build images

```bash
DOCKER_HOST=tcp://localhost:2375 tests/docker/build-images.sh all
```

Build one target:

```bash
DOCKER_HOST=tcp://localhost:2375 tests/docker/build-images.sh debian-13
```

Build a specific architecture when using a multi-arch builder:

```bash
KREMA_DOCKER_PLATFORM=linux/arm64 DOCKER_HOST=tcp://localhost:2375 tests/docker/build-images.sh ubuntu-26.04
```

## Build packages with osc

Use the OBS repository name from `targets.tsv` with the existing `justfile` recipes or direct `osc build` commands. Examples:

```bash
osc build openSUSE_Tumbleweed x86_64 packaging/obs/krema.spec
osc build Fedora_42 x86_64 packaging/obs/krema.spec
osc build Debian_13 x86_64 packaging/obs/debian.control
osc build xUbuntu_26.04 x86_64 packaging/obs/debian.control
```

Put the resulting `.rpm` or `.deb` files in a directory per target. The runtime image installs the package from that directory using the distro package manager so dependencies resolve from the image's configured repositories.

## Run a smoke test

```bash
DOCKER_HOST=tcp://localhost:2375 tests/docker/run-smoke.sh debian-13 /path/to/osc/results/debian-13
```

The host script starts `kwin_wayland --virtual`, waits for the private Wayland socket, runs the target image, mounts the package directory as `/packages`, installs the package, waits up to `KREMA_SMOKE_READY_TIMEOUT` seconds for the `krema` process to appear, and runs `dbus-run-session -- krema` long enough to confirm it stays alive on the virtual display.
Set `KREMA_SMOKE_SCREENSHOT_DIR=/path/to/screenshots` to capture a KWin workspace BMP screenshot while the dock is visible after the process readiness check passes.

## Coverage

Covered:

- distro package installation from local osc artifacts
- package dependency resolution through the target distro repositories
- Qt Wayland client startup
- KDE runtime and QML module loading
- LayerShellQt startup against KWin without an immediate crash

Not covered:

- real Plasma shell interaction
- populated `LibTaskManager` task rows
- global shortcut delivery from the compositor
- PipeWire window preview capture
- full mouse/keyboard scenario execution

Use `tests/e2e/README.md` and kwin-mcp for the higher-level UX scenarios, and keep a real Plasma desktop pass for window-management and preview behavior.
