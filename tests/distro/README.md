# Krema distro E2E tests (Tier 3)

Tier 2 (`tests/appium/`) runs the AT-SPI E2E suite against krema built from
source on Fedora. Tier 3 runs **the same suite** against krema **installed from
its distro package** on each supported distribution, with that distribution's
own KWin, Qt, KDE Frameworks, Kirigami and PipeWire. It catches what Tier 2
cannot:

* packaging bugs: missing or misnamed runtime dependencies (`Requires` in
  `packaging/obs/krema.spec`, `Depends` in `packaging/obs/debian.control`,
  `depends` in `packaging/arch/PKGBUILD`), wrong install paths;
* behaviour that differs with the Qt/KF/Kirigami/KWin versions a distro ships.

Everything runs in one unprivileged container per target, exactly like Tier 2:
no VM, no `--privileged`, no KVM.

## Running

```sh
tests/distro/run-distro-e2e.sh fedora-43                    # full suite
tests/distro/run-distro-e2e.sh debian-13 test_smoke.py -x   # any pytest arguments
tests/distro/run-distro-e2e.sh opensuse-tumbleweed --shell  # shell in the container
```

Host requirements are Tier 2's (Docker with buildx, optionally `/dev/dri`;
see `tests/appium/README.md`), plus GNU tar. In practice that means a Linux
Docker host: Tier 3 needs a DRM render node for capture tests, and on host
kernels >= 6.15 that node must be a platform-bus vgem (see the next
paragraph). On macOS there is no usable path: OrbStack's Linux kernel has no
DRM driver at all, so `/dev/dri` never appears in the container and the
capture tests cannot run — use a Lima VM with vgem instead (the setup in
`tests/appium/README.md`'s macOS section; it is what was used to develop
and verify this tier).

Results, JUnit XML and logs go to
`tests/appium/artifacts-distro-<target>/`. The script exits with pytest's
status.

On a host kernel >= 6.15 load vgem with `sudo tests/appium/setup-vgem.sh`,
not `modprobe vgem` (after `rmmod vgem` if it is already loaded). Since 6.15
the kernel registers vgem on the faux bus; the targets with KWin < 6.5
(`debian-13`, `ubuntu-25.04`, `ubuntu-25.10`, `opensuse-leap-16.0`) only use
a platform-bus vgem (their libdrm < 2.4.126 cannot enumerate faux devices,
and KWin < 6.5 opens a faux vgem's render node, where dumb buffers fail).
Without it they composite with QPainter: no screenshots, no PipeWire
thumbnails. `setup-vgem.sh` builds vgem out-of-tree as the platform device it
was before 6.15, as the CI job does.

What it does:

1. **Package.** `tests/distro/build-package.sh <target>` packs the current
   worktree as `krema-<version>.tar.gz` and builds it in a clean container of
   the target's base image with the repo's own packaging
   (`packaging/obs/krema.spec`, `packaging/obs/debian.*`,
   `packaging/arch/PKGBUILD`; helpers in `pkg/`). The result is cached in
   `tests/distro/.cache/packages/<target>/`; it is rebuilt only when missing
   or with `KREMA_DISTRO_REBUILD_PACKAGE=1`, so **rebuild after changing
   `src/` or `packaging/`**.
2. **Image** `krema-e2e-distro:<target>` from `image/Dockerfile`
   (`image/packages.sh` drives the stages; package names per family live in
   `image/fedora.sh`, `image/suse.sh`, `image/debian.sh`, `image/arch.sh`):
   * the Tier 2 runtime on the target distro: `kwin_wayland` (file
     capabilities dropped, as in Tier 2), PipeWire + WirePlumber, AT-SPI,
     the python modules of the suite, kwrite/kfind fixtures, Breeze
     icons/style and fonts; `XDG_MENU_PREFIX=plasma-` as in a Plasma
     session;
   * KDE's selenium-webdriver-at-spi (same pinned commit and
     `tests/appium/tools/*.patch`) and `krema-test-window`, both built in a
     builder stage **on the same distro**;
   * last layer: the krema package installed through the distro's package
     manager (`dnf`, `zypper`, `apt-get`, `pacman -U`), so dependency
     resolution is part of the test. Nothing of krema is built from source.
3. **Suite.** `tests/appium/run-e2e.sh` with `KREMA_E2E_IMAGE`,
   `KREMA_E2E_SKIP_BUILD=1` and `KREMA_E2E_BINARY=/usr/bin/krema`: the
   session setup is Tier 2's `entrypoint.sh`, minus the source sync and
   krema build. `KREMA_E2E_DISTRO=<target>` is exported to the tests, and
   KWin runs with its Wayland permission checks on
   (`KWIN_WAYLAND_NO_PERMISSION_CHECKS=0`, made overridable by
   `tests/appium/tools/run-permission-checks.patch`): the installed
   `com.bhyoo.krema.desktop` must declare the privileged interfaces krema
   binds (`X-KDE-Wayland-Interfaces`), as in a real session. That
   declaration also makes krema privileged for xdg-activation; on KWin < 6.5
   without it krema cannot raise its own Settings window from the dock.
   Tier 2 keeps the checks off: a krema built from source has no installed
   desktop file.

Environment:

| Variable | Effect |
| --- | --- |
| `KREMA_DISTRO_REBUILD_PACKAGE=1` | rebuild the package even if one is cached |
| `KREMA_DISTRO_SKIP_IMAGE_BUILD=1` | reuse the existing `krema-e2e-distro:<target>` image |
| `KREMA_E2E_ARTIFACTS` | artifacts directory (default `tests/appium/artifacts-distro-<target>`) |
| `KREMA_E2E_PLATFORM` | container platform; defaults to `linux/amd64` for `arch` and `opensuse-slowroll` |
| other `KREMA_E2E_*` | as in `tests/appium/run-e2e.sh` (screen size, output count, docker args) |

## Targets

Every row of `tests/distro/targets.tsv` (base images pinned by digest there),
plus `arch`:

| Target | Family | Base image | Platforms |
| --- | --- | --- | --- |
| `fedora-42` | fedora | `fedora:42` | amd64, arm64 |
| `fedora-43` | fedora | `fedora:43` | amd64, arm64 |
| `fedora-44` | fedora | `fedora:44` | amd64, arm64 |
| `fedora-rawhide` | fedora | `fedora:rawhide` | amd64, arm64 |
| `opensuse-tumbleweed` | suse | `opensuse/tumbleweed` | amd64, arm64 |
| `opensuse-slowroll` | suse | `opensuse/tumbleweed` switched to the Slowroll repositories | amd64 |
| `opensuse-leap-16.0` | suse | `opensuse/leap:16.0` | amd64, arm64 |
| `debian-13` | debian | `debian:13-slim` | amd64, arm64 |
| `ubuntu-25.04` | debian | `ubuntu:25.04` | amd64, arm64 |
| `ubuntu-25.10` | debian | `ubuntu:25.10` | amd64, arm64 |
| `ubuntu-26.04` | debian | `ubuntu:26.04` | amd64, arm64 |
| `arch` | arch | `archlinux:latest` | amd64 |

`arch` and `opensuse-slowroll` publish amd64 packages only; on an arm64 host
they need amd64 emulation (qemu-user binfmt) and are otherwise CI-only.

The target list is `targets.tsv`, one row per repository in
`packaging/obs/project.meta.xml` (its `obs_repository` column). The weekly
distro release watcher (`.github/workflows/distro-release-watch.yml`,
`scripts/check_distro_releases.py`) only compares the OBS/COPR/PPA channels
with upstream releases and does not read `targets.tsv`: when one of its
`distro-release` issues adds an OBS repository, add the matching row here too
(lock the base image with `tests/distro/update-digests.sh`) so Tier 3 runs on it.

The pass criterion per target is Tier 2's result for the same suite. A
difference that only one distro shows is root-caused: harness or image
problems are fixed here; a genuine krema or packaging bug on that distro is
reported and only then pinned with a `xfail(strict=True)` conditioned on
`KREMA_E2E_DISTRO`.

## CI

`.github/workflows/distro-e2e.yml` runs every target as its own job on a
GitHub-hosted `ubuntu-latest` (amd64) runner, so `arch` and
`opensuse-slowroll` run natively there. It triggers on pull requests touching
`src/`, `packaging/`, `tests/`, `CMakeLists.txt` or the workflow, weekly on
a schedule (to catch drift in the rolling distros), and on demand
(`workflow_dispatch`, optional `targets` input: comma/space separated ids,
default `all`). The release procedure (`.claude/commands/release.md`)
dispatches it on `master` and waits for it to pass before tagging. A newer
push to a pull request cancels its running jobs; `fail-fast` is off, so one
distro's failure does not hide another's.

Each job builds and loads the platform-bus vgem with
`tests/appium/setup-vgem.sh` (see Running) and runs
`tests/distro/run-distro-e2e.sh <target> -rs`: `kwin_wayland --virtual`
with one output, compositing with OpenGL through llvmpipe on the vgem
device. The job adds a JUnit summary to the step summary (a skip caused by
QPainter compositing or a missing render node fails it) and uploads
`tests/appium/artifacts-distro-<target>/` as `distro-e2e-<target>`.

The `fedora-43` job then runs the `@pytest.mark.outputs(2)` tests again in
a 2-output session, reusing the image it just built:

```sh
KREMA_E2E_OUTPUT_COUNT=2 KREMA_DISTRO_SKIP_IMAGE_BUILD=1 \
KREMA_E2E_ARTIFACTS=tests/appium/artifacts-distro-fedora-43-2out \
    tests/distro/run-distro-e2e.sh fedora-43 -m outputs -rs
```

with its own JUnit summary and the artifact `distro-e2e-fedora-43-2out`.

Expected result: every target matches Tier 2 — `77 passed, 4 skipped,
0 xfailed` (the 4 skips are the 2-output tests, which the `fedora-43`
2-output run covers). The suite pins no krema bug
with an xfail, conditional or not. Differences in the distros' libraries are
handled in the harness rather than in expectations: Qt's AT-SPI roles and
extents (`PAGE_ROLE`, `SETTINGS_STACK_XPATH`, `painted_rect()` in
`tests/appium/krema_e2e/krema.py`, keyed on the runtime `env.QT_VERSION`).

| Targets | Result |
| --- | --- |
| `fedora-42`, `fedora-43`, `fedora-44`, `fedora-rawhide`, `opensuse-tumbleweed`, `opensuse-slowroll`, `opensuse-leap-16.0`, `debian-13`, `ubuntu-25.04`, `ubuntu-25.10`, `ubuntu-26.04`, `arch` | 77 passed, 4 skipped, 0 xfailed |

Package and image are built from scratch on every run (about 2 minutes
each), the suite takes about 8 minutes, and a full-matrix run about
15 minutes. There is no layer cache: twelve distro images would not fit the
repository's 10 GB Actions cache, which `e2e.yml` also uses.

## Cleanup

The script leaves the package cache (`tests/distro/.cache/`), the image
`krema-e2e-distro:<target>`, its base image and BuildKit's layer cache.
Remove them with:

```sh
rm -rf tests/distro/.cache
docker image rm krema-e2e-distro:<target>
docker builder prune
```
