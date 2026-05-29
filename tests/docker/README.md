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

The default local image name is `krema/gui-runtime:<target-id>`. The published ghcr image is `ghcr.io/isac322/krema-gui-runtime-<target-id>:<tag>` (see [Publish to ghcr.io](#publish-to-ghcrio)).

Slowroll uses `opensuse/tumbleweed:latest` as the container base because there is no official `opensuse/slowroll` runtime image. The Dockerfile disables the base repositories and enables the official Slowroll OSS and update repositories before installing runtime dependencies. Slowroll's OBS packages are amd64-only, so the runtime image is also amd64-only.

## Reproducibility and the base digest lockfile

`targets.tsv` records a `base_digest` column with the sha256 manifest list digest of each `base_image`. The build scripts always pass `BASE_IMAGE` as `docker.io/<image>@<sha256:digest>` so rebuilding the same git revision pulls the exact same base layers — even when the upstream tag (for example `opensuse/tumbleweed:latest` or `fedora:rawhide`) has rolled forward.

Refresh the lockfile when a base needs to be bumped:

```bash
DOCKER_HOST=tcp://localhost:2375 tests/docker/update-digests.sh all
git add tests/docker/targets.tsv
git commit -m "chore(tests): refresh runtime image base digests"
```

`update-digests.sh` calls `docker buildx imagetools inspect` and rewrites `targets.tsv` in place. Refreshing the digest is an explicit user action so that every reproducibility break is visible in `git log`.

Tumbleweed and Slowroll share `opensuse/tumbleweed:latest` and therefore share a single manifest digest in the lockfile. The Slowroll-specific single-platform constraint is applied at publish time, not in the lockfile.

## Build images locally

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

If any `targets.tsv` row still has a `pending` `base_digest` value the build aborts with an instruction to run `update-digests.sh` first.

## Parallel builds

Both `build-images.sh` and `publish-images.sh` build up to `KREMA_DOCKER_PARALLEL` targets concurrently (default `4`). When more than one target is selected, each target's output is redirected to `KREMA_DOCKER_LOG_DIR/<target>.log` (default `${TMPDIR:-/tmp}/krema-{build,publish}-images/<target>.log`) and a pass/fail summary is printed at the end. The script exits non-zero if any target fails.

Single-target invocations and `KREMA_DOCKER_PARALLEL=1` keep the original streaming-to-terminal behaviour. Dry-run (`KREMA_DOCKER_DRY_RUN=1`) also stays serial so the resolved buildx commands print in target order.

```bash
# Default: 4-wide parallel build of every target
DOCKER_HOST=tcp://localhost:2375 tests/docker/build-images.sh all

# 8-wide on a beefier machine
KREMA_DOCKER_PARALLEL=8 DOCKER_HOST=tcp://localhost:2375 tests/docker/publish-images.sh all

# Old serial behaviour
KREMA_DOCKER_PARALLEL=1 DOCKER_HOST=tcp://localhost:2375 tests/docker/build-images.sh all
```

Parallel mode requires bash 5.1+ (uses `wait -n -p`). The scripts refuse to run with `KREMA_DOCKER_PARALLEL>1` on older bash and tell the caller to set `KREMA_DOCKER_PARALLEL=1`.

## Publish to ghcr.io

`tests/docker/publish-images.sh` builds each image with `docker buildx` from the locked base digest and pushes a multi-arch OCI manifest to `ghcr.io/<owner>/<repo-prefix>-<target_id>` (default: `ghcr.io/isac322/krema-gui-runtime-<target_id>`).

Tag scheme:

| Tag | Purpose |
|---|---|
| `<YYYYMMDD>-<git-sha7>` | Immutable, reproducible reference. Use this in CI / READMEs / smoke runs. |
| `latest` | Mutable convenience tag. Pushed only when the current branch is `master` and the working tree is clean. |

Authentication is the caller's responsibility:

```bash
gh auth token | docker login ghcr.io -u <user> --password-stdin
```

Build and push every target:

```bash
DOCKER_HOST=tcp://localhost:2375 tests/docker/publish-images.sh all
```

Single target:

```bash
DOCKER_HOST=tcp://localhost:2375 tests/docker/publish-images.sh fedora-44
```

Dry-run (prints the resolved buildx command and tags without pushing):

```bash
KREMA_DOCKER_DRY_RUN=1 tests/docker/publish-images.sh all
```

Override the `:latest` policy:

```bash
KREMA_DOCKER_PUSH_LATEST=1 tests/docker/publish-images.sh fedora-44  # always push :latest
KREMA_DOCKER_PUSH_LATEST=0 tests/docker/publish-images.sh fedora-44  # never push :latest
```

Notes:

- All targets except `opensuse-slowroll` are published as `linux/amd64,linux/arm64`. Slowroll is `linux/amd64` only because the OBS Slowroll repository is amd64-only.
- Multi-arch builds require QEMU binfmt handlers for the non-native architecture. On Arch:

  ```bash
  sudo pacman -S qemu-user-static qemu-user-static-binfmt
  sudo systemctl restart systemd-binfmt
  ```

- Layer timestamps are normalised via `SOURCE_DATE_EPOCH` set to the commit time of the latest input change, so identical inputs produce byte-identical layer hashes.
- Provenance and SBOM attestations are disabled (`--provenance=false --sbom=false`) to keep manifests small.
- The buildx builder (default name `krema-multiarch`) is auto-created with `--driver docker-container --bootstrap` on first use.

OCI image labels embedded in every push:

- `org.opencontainers.image.{source,url,documentation,licenses}` — project URLs
- `org.opencontainers.image.{revision,created,version}` — git SHA, commit time, immutable tag
- `org.opencontainers.image.base.{name,digest}` — base image lock
- `com.bhyoo.krema.{target,family}` — per-target metadata

## Pull from ghcr.io

```bash
docker pull ghcr.io/isac322/krema-gui-runtime-fedora-44:latest
# or pin to an immutable build:
docker pull ghcr.io/isac322/krema-gui-runtime-fedora-44:20260529-103b39e
```

Use a published image with the existing smoke runner by re-tagging it to the local naming convention:

```bash
TARGET=fedora-44
TAG=latest   # or 20260529-103b39e
docker pull ghcr.io/isac322/krema-gui-runtime-$TARGET:$TAG
docker tag ghcr.io/isac322/krema-gui-runtime-$TARGET:$TAG krema/gui-runtime:$TARGET
DOCKER_HOST=tcp://localhost:2375 tests/docker/run-smoke.sh $TARGET /path/to/osc/results/$TARGET
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
