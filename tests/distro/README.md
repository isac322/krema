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

What it does (each phase prints `[distro] <phase>: <seconds>s`):

1. **Runtime container** (in the background). The target's runtime image
   (see [Images](#images)) is taken from the local tag, else pulled from
   `ghcr.io/isac322/krema-ci`, else built locally with
   `tests/distro/build-ci-image.sh`. A container of it starts and, on the
   rolling targets, is fully upgraded (`packages.sh upgrade`) while the
   package builds.
2. **Package.** `tests/distro/build-package.sh <target>` packs the current
   worktree as `krema-<version>.tar.gz` and builds it in a container of the
   target's builder image (same local → ghcr → local-build order) with the
   repo's own packaging (`packaging/obs/krema.spec`, `packaging/obs/debian.*`,
   `packaging/arch/PKGBUILD`; helpers in `pkg/`): rolling targets are fully
   upgraded first, build dependencies that a newer `packaging/` adds are
   installed (the check is offline: rpm database, `dpkg-checkbuilddeps`,
   `pacman -T`), and the compilers run behind ccache
   (`KREMA_CCACHE_DIR`, default `tests/distro/.cache/ccache/<target>/`).
   The package is cached in `tests/distro/.cache/packages/<target>/`; it is
   rebuilt only when missing or with `KREMA_DISTRO_REBUILD_PACKAGE=1`, so
   **rebuild after changing `src/` or `packaging/`**.
3. **Install.** The package is copied into the runtime container and
   installed through the distro's package manager (`dnf`, `zypper`,
   `apt-get`, `pacman -U`), so dependency resolution is part of the test;
   the container is committed as `krema-e2e-distro:<target>`. Nothing of
   krema is built from source in it. While the package built, `packages.sh
   depfetch` already downloaded the deps the packaging declares (into the
   package-manager cache, installing nothing), so the install mostly runs
   the transaction.
4. **Suite.** `tests/appium/run-e2e.sh` with `KREMA_E2E_IMAGE`,
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
   desktop file. With `KREMA_E2E_SHARDS=N` (N > 1) the script instead starts
   N `run-e2e.sh` containers concurrently: shard `i` (0-based) gets
   `KREMA_E2E_SHARD=i/N` (the suite deselects all but every N-th collected
   test), writes to `artifacts-distro-<target>/shard-<i>/`, and is prefixed
   `[shard i]` in the log; the script fails if any shard fails, after all
   shards finish. Each shard is a full session — own container, KWin,
   D-Bus session and WebDriver — so N is bounded by host CPU/RAM, and the
   llvmpipe compositors contend for CPU.

Without access to ghcr.io (offline, no login, fork) or on an arm64 host
(only amd64 images are published) every image is built locally from the
same Dockerfile; the result is the same, only slower the first time.

Environment:

| Variable | Effect |
| --- | --- |
| `KREMA_DISTRO_REBUILD_PACKAGE=1` | rebuild the package even if one is cached |
| `KREMA_DISTRO_SKIP_IMAGE_BUILD=1` | reuse the existing `krema-e2e-distro:<target>` image: no runtime container, no package install |
| `KREMA_CCACHE_DIR` | host ccache directory of the package build (default `tests/distro/.cache/ccache/<target>`); `CCACHE_MAXSIZE` (default 500M) is passed through |
| `KREMA_CI_REGISTRY` | image repository (default `ghcr.io/isac322/krema-ci`) |
| `KREMA_E2E_ARTIFACTS` | artifacts directory (default `tests/appium/artifacts-distro-<target>`) |
| `KREMA_E2E_PLATFORM` | container platform; defaults to `linux/amd64` for `arch` and `opensuse-slowroll` |
| `KREMA_E2E_SHARDS` | number of concurrent suite shards (default 1 = one `run-e2e.sh`); with N > 1, `LP_NUM_THREADS` defaults to CPUs / N so the shards' llvmpipe renderers do not oversubscribe the CPUs |
| other `KREMA_E2E_*` | as in `tests/appium/run-e2e.sh` (screen size, output count, docker args) |

## Images

One package, `ghcr.io/isac322/krema-ci`, holds every prebuilt CI image.
`tests/distro/image-ref.sh <kind> [<target>]` prints a reference;
`tests/distro/build-ci-image.sh [--pull] <kind> [<target>]` builds it
locally under that reference (with `--pull`: local tag, else pull, else
build).

| Reference | Content | Hashed inputs |
| --- | --- | --- |
| `runtime-<target>-<hash>` | `image/Dockerfile --target runtime`: base image, `kwin_wayland` (file capabilities dropped, as in Tier 2), PipeWire + WirePlumber, AT-SPI, the suite's python modules, kwrite/kfind fixtures, Breeze icons/style and fonts, `XDG_MENU_PREFIX=plasma-`; KDE's selenium-webdriver-at-spi (same pinned commit and `tests/appium/tools/*.patch`) and `krema-test-window`, both built on the same distro | `image/Dockerfile`, `image/packages.sh`, `image/<family>.sh`, every file under `tests/appium/tools/` (patches and `krema-test-window/`), target id, family and base image with its digest |
| `builder-<target>-<hash>` | `image/Dockerfile --target builder`: base image, packaging toolchain (`rpm-build`, `devscripts`/`equivs`, `base-devel`), ccache, and krema's build dependencies from `packaging/` at image build time | same as runtime (one hash per target) |
| `ctest-fedora-43-<hash>` | `tests/appium/Dockerfile --target ctest-image` (Build & tests) | `tests/appium/Dockerfile` |
| `vgem-<kernel release>-<hash>` | `FROM scratch` with `/vgem.ko` built by `tests/appium/setup-vgem.sh` for that runner kernel | `tests/appium/setup-vgem.sh` |

Every job pulls a builder and a runtime image, so their size is on the
critical path. Installs run without weak dependencies/recommends and
without docs in every family (Fedora's `dnf builddep` included). The
publisher pushes zstd-compressed layers (`KREMA_CI_PUSH=1` in
`build-ci-image.sh`, docker-container buildx builder), which are smaller
and decompress faster than gzip. A local `--load` build is unaffected.

`<hash>` is the first 12 hex digits of a sha256 over those files' paths and
contents plus the listed build arguments, so a change to any input yields a
new tag, which is missing from the registry until the publisher pushes it:
the job then builds that image itself. Package names per family live in
`image/fedora.sh`, `image/suse.sh`, `image/debian.sh` and `image/arch.sh`,
one file per family so a change to one family leaves the others' images
(and tags) alone.

What deliberately stays out of the images:

* **krema and its runtime dependencies stay out of the runtime image.** The
  runtime list is Tier 2's minus krema's build and runtime dependencies;
  installing the package at job time through the package manager is what
  proves that `Requires`/`Depends`/`depends` pull in everything krema
  needs. Baking them in would hide a missing dependency.
* **`packaging/` is not a hash input of the builder.** The builder carries
  the build dependencies as of its build; at job time `packages.sh
  builddeps` checks them offline and installs only what a newer
  `packaging/` adds, and `rpmbuild`/`dpkg-buildpackage`/`makepkg` re-check
  them (with versions) before building. A packaging change therefore never
  waits for an image rebuild, and never builds against stale deps.
* **Runtime and builder of a target share one hash**, so they are always
  published, missing or rebuilt together: a package is never built on a
  builder whose distro snapshot differs from the runtime it is installed
  into.

Freshness: the hash does not change when a distro's repositories move on.
`.github/workflows/ci-images.yml` rebuilds and re-pushes the images of the
rolling targets (`fedora-rawhide`, `opensuse-tumbleweed`,
`opensuse-slowroll`, `arch`) daily and the others weekly. Because krema
links Qt private API, a day-old rolling image plus today's repositories
would be a partial upgrade, so on those four targets both the builder and
the runtime container run the full upgrade (`dnf distro-sync`,
`zypper dup`, `pacman -Syu`) at job time, the runtime one while the package
builds; the images are upgraded at build time too, which keeps that delta
small. The stable targets get no job-time upgrade: their builder and runtime
come from the same repository snapshot.

Dependency download happens off the critical path. Right after the runtime
upgrade, `packages.sh depfetch` downloads the packaging's declared runtime
deps into the package-manager cache (`dnf --downloadonly`+`keepcache`,
`zypper --download-only`+`keeppackages`, `apt-get --download-only`,
`pacman -Sw`), which the `krema` install then consumes. A metadata snapshot
at most a day old is equally valid evidence: the install still runs the
package manager's full dependency resolution and transaction; only the
downloads were warmed. If the snapshot has moved past what the cache holds,
the install retries once after refreshing its metadata.

Debug packages stay on. `rpmbuild` still produces debuginfo/debugsource,
`dpkg-buildpackage` the dbgsym package and `makepkg` the `-debug` package,
although CI installs only the binary package: splitting the debug info is
the step that strips the shipped binary and adds its `.gnu_debuglink`
(Fedora also its `.gnu_debugdata`), so turning it off would test a
different binary. Only the rpm payload compression is lowered
(`_binary_payload w3.zstdio`, `pkg/rpm-build.sh`): it changes how the files
are compressed inside the `.rpm`, not the installed files or the dependency
metadata.

On Fedora the ccache hits are all preprocessed, not direct: Fedora's
`optflags` pass `-Wp,-U_FORTIFY_SOURCE,-D_FORTIFY_SOURCE=3` (a single
`-Wp,` argument), and ccache disables direct mode for it. The flag form is
a deliberate upstream choice (redhat-rpm-config) so our spec does not
override it; the preprocessed hits still skip the compiler entirely, so the
build is fast.

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
the product (`src/`, `packaging/`, `CMakeLists.txt`, `LICENSES/`), the
harness (`tests/distro/`, `tests/appium/` except its Dockerfile and
CMakeLists.txt; not Markdown) or the workflow, weekly on a schedule (to
catch drift in the rolling distros), and on demand (`workflow_dispatch`,
optional `targets` input: comma/space separated ids, default `all`). The
release procedure (`.claude/commands/release.md`) dispatches it on `master`
and waits for it to pass before tagging. A newer push to a pull request
cancels its running jobs; `fail-fast` is off, so one distro's failure does
not hide another's.

Each job logs in to ghcr.io with the job's `GITHUB_TOKEN` (`packages:
read`) and loads the platform-bus vgem (see Running): the module comes from
the `vgem-<kernel release>-<hash>` image when the publisher has built one
for the runner's kernel, else `tests/appium/setup-vgem.sh` installs the
headers and builds it. The job restores the target's ccache
(`KREMA_CCACHE_DIR`, keyed on the builder reference; saved only by
default-branch runs, which is where Actions caches are shared with every
pull request). It then runs `tests/distro/run-distro-e2e.sh <target> -rs`
with `KREMA_E2E_SHARDS=2`: two concurrent `kwin_wayland --virtual` sessions
compositing with OpenGL through llvmpipe on the vgem device, each running
every second collected test (all targets, including `ubuntu-25.04`; its old
preview flake was a PipeWire/KWin untyped-buffer race that
`tests/appium/krema_e2e/preview.py` now renegotiates once). The job adds a
JUnit summary of every `junit.xml` (one per shard) to the step summary (a
skip caused by QPainter compositing or a missing render node fails it) and
uploads `tests/appium/artifacts-distro-<target>/` as
`distro-e2e-<target>`.

`.github/workflows/ci-images.yml` is the only workflow with `packages:
write`. It builds every image with `build-ci-image.sh`, pushes it, and seeds
each target's ccache with `build-package.sh <target>`: on pushes to
`master` that change an image input (missing references only), daily for
the rolling targets, weekly for the others and `ctest`, and on demand.
Pull requests never push; a pull request that changes an image input builds
the new image in its own jobs.

Back in `distro-e2e.yml`, the `fedora-43` job then runs the
`@pytest.mark.outputs(2)` tests again in a 2-output session, reusing the
image it just built:

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

Per-phase wall time of a job when the images are published and the ccache
is warm (4-vCPU runner; the runtime phases overlap the package phases):

| Phase | Stable target | Rolling target |
| --- | --- | --- |
| `builder-image` (pull) | ~20–40 s | ~20–40 s |
| `builder-upgrade` | 0 s | ~10–60 s (one day of updates) |
| `build-deps` (offline check) | ~1 s | ~1 s |
| `package-build` (ccache hits + packaging) | ~20–40 s | ~20–40 s, more after a toolchain update |
| `runtime-image` + `runtime-upgrade` + `dep-fetch` (background) | ~40–90 s | ~40–120 s |
| `package-install` (deps prefetched; txn only) | ~20–35 s | ~20–35 s |
| `image-commit` | ~5–15 s | ~5–15 s |
| `suite` (2 shards) | ~260–330 s | ~260–330 s |

A pull request that changes an image input pays the local image build
instead of the pull (a few minutes, like before the images were published).

## Cleanup

The script leaves the package and ccache caches (`tests/distro/.cache/`),
the image `krema-e2e-distro:<target>`, the runtime and builder images
(`ghcr.io/isac322/krema-ci:*`), their base images and BuildKit's layer
cache. Remove them with:

```sh
rm -rf tests/distro/.cache
docker image rm krema-e2e-distro:<target>
docker image rm $(docker image ls -q ghcr.io/isac322/krema-ci)
docker builder prune
```
