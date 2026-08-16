# Deterministic UI Frame Tests

Runs the real dock on a real KWin compositor entirely inside one unprivileged
container, replays a scripted input scenario, and captures **every rendered
frame** — so a 150 ms hover animation becomes ~9 inspectable samples instead of
a screenshot race.

```bash
DOCKER_HOST=tcp://localhost:2375 docker build -f tests/ci/Dockerfile -t krema-ui-ci tests/ci
DOCKER_HOST=tcp://localhost:2375 docker run --rm \
    -v "$PWD:/src:ro" -v /tmp/krema-frames:/out \
    krema-ui-ci bash /src/tests/ci/run-frame-tests.sh
```

Runtime, artifact size, and architecture support must be measured again after
the strict comparator, native-menu mapping checks, and release-build guard run
in CI. The runner still needs no `--privileged`, `--cap-add`, or `/dev/dri`.

## How it differs from `tests/docker/`

| | `tests/docker/` | `tests/ci/` (this) |
|---|---|---|
| Compositor | host-side `kwin_wayland`, socket bind-mounted in | inside the container |
| Krema | installed from an external `.rpm`/`.deb` | built from the checkout |
| Question answered | does the package install and start on distro X | does the UI *behave* correctly |
| Granularity | process stayed alive for N seconds | per-frame item geometry, properties and pixels |

Both are useful. Packaging regressions belong in `tests/docker/`; behaviour and
animation regressions belong here.

## Architecture

```
container (unprivileged)
├── release build (-DKREMA_TEST_HOOKS=OFF)
│   └── dependency/symbol check: no Qt6Test or FrameProbe
├── dbus-run-session                     isolated session bus
│   └── kwin_wayland --virtual           KDE compositor, headless, llvmpipe
│       └── krema (built with -DKREMA_TEST_HOOKS=ON)
│           └── tests/ci/frameprobe      fixed-step clock + capture + input
└── tests/ci/assert_frames.py            assertions + strict pass comparison
```

### The determinism trick

`tests/ci/frameprobe` does four things, all opt-in:

1. **Virtual animation clock.** A `QAnimationDriver` subclass returns a counter
   from `elapsed()` and advances it by exactly `KREMA_PROBE_STEP_MS` per
   captured frame. Animation progress becomes a pure function of the frame
   number, independent of how slow llvmpipe is on the runner.
2. **Self-driven frame loop.** Each tick runs in the event loop (never inside a
   render pass) and does: grab pixels → dump item state → inject scripted input
   → advance the clock. `QQuickWindow::grabWindow()` renders the scene itself,
   so the PNG and the JSON row always describe the same instant.
3. **Real synthetic input.** Actions go through `QTest::mouseMove/mouseClick/
   keyClick`, i.e. Qt's normal delivery path with hit-testing, hover and
   `onClicked`. A detached signal or a broken input region fails the test; a
   direct property poke would not.
4. **Full state dump.** Every `QQuickItem` in every Qt Quick window, with
   geometry, opacity, visibility **and its QML-declared properties**. Krema's
   zoom lives in `DockItem.currentScale`, a QML property — a capture limited to
   base `QQuickItem` fields sees nothing move.

A permanently running `QVariantAnimation` is started alongside the driver. This
is not cosmetic: `QUnifiedTimer` only refreshes its internal `lastTick` while at
least one animation runs, so without it the first animation started after an
idle gap is charged the entire elapsed virtual time as one delta and **completes
in a single frame instead of animating** — the exact failure this harness exists
to catch, silently passing.

## Writing a scenario

`tests/ci/scenarios/<name>.json`. Full schema:

```json
{
  "description": "One sentence: what the user does and what they should see",
  "config": {"General/Edge": 2, "General/IconSize": 96},
  "fixture_windows": 1,
  "actions": [
    {"frame": 5,  "type": "move",    "item": "dockItem0"},
    {"frame": 20, "type": "click",   "item": "dockItem0", "button": "right"},
    {"frame": 40, "type": "key",     "key": "Right"},
    {"frame": 60, "type": "setting", "name": "iconSize", "value": 96}
  ],
  "assertions": [
    {"qa": "QA-ITEM-007", "kind": "animates", "item": "dockItem0",
     "prop": "currentScale", "comment": "hovering grows the icon from 1.0 to 1.6",
     "from": 1.0, "to": 1.6, "start_after": 5, "settled_by": 25}
  ]
}
```

A scenario that cannot verify its QA item declares that gap instead of
pretending to pass with zero assertions:

```json
{
  "description": "Why this scenario exists",
  "actions": [],
  "not_verified": [
    {"qa": "QA-NOTI-012", "reason": "Needs a demanding-attention fixture"}
  ],
  "assertions": []
}
```

An empty assertion list without at least one `not_verified` row is a test
failure. The old `blocked` and `not_verifiable` keys are rejected.

`config` becomes `~/.config/kremarc` before krema starts — that is how a user's
saved settings reach the dock. Keys are `Group/Entry`; the group defaults to
`General`. Names and defaults come from `src/config/krema.kcfg`.

`max_frames` overrides the default 140-frame capture for scenarios that need
longer, such as the edge transition (~45 frames to settle on its own).

The dock's pinned launchers are set by the runner, not by krema's shipped
default, which points at Dolphin, Konsole, Kate and System Settings — none of
which are in this image, so every item would render as a generic placeholder
and hide real icon regressions. The image installs KWrite, KFind, Okular and
Gwenview for their `.desktop` files and icons, plus one synthetic launcher with
a deliberately long name so the tooltip layout check has something to stress.
They are pinned as absolute `file://` URLs rather than `applications:` ids:
`LauncherTasksModel` then builds the `KService` straight from the path, so
nothing depends on a ksycoca database keyed to a matching `XDG_DATA_DIRS`.

`fixture_windows: N` maps N real `xdg_toplevel` clients first. Anything that
depends on `TasksModel` window rows (previews, running indicators, active
state) needs this; krema alone shows only the pinned launchers.

Actions run at the named frame: `move`, `click`, `press`, `release`, `key`,
`setting`, `shortcut`, `menuitem`. Target an item by `objectName`
(`"item": "dockItem0"`, with optional `x`/`y` offset from its centre) rather
than raw coordinates. Add `"window": "<key>"` to address another surface.
Mouse actions with `"native": true` travel through KWin's test-only fake-input
protocol, so Qt receives a compositor-delivered event with a real Wayland
serial. Native context-menu clicks require this; `QTest` events never reach the
compositor and cannot authorize an `xdg_popup`.

`menuitem` activates an entry of the open context menu by its label
(`{"type": "menuitem", "name": "Settings..."}`). The probe resolves the
`QAction` geometry and clicks it through the same KWin fake-input path. Menu
assertions require `"mapped": true`, and frame capture composites the exposed
popup window, so a constructed menu with a blank screenshot fails.

`shortcut` triggers a registered global action by name
(`{"type": "shortcut", "name": "focus-dock"}`). Krema's keyboard navigation is
reachable only through the `focus-dock` global shortcut, and KGlobalAccel key
delivery does not work in this session, so synthetic keys cannot enter it.
Triggering the QAction runs the exact slot the shortcut fires; it does not
cover the key-sequence-to-action binding, which KGlobalAccel owns. Registered
names: `toggle-dock`, `focus-dock`, `activate-entry-1`..`9`,
`new-instance-entry-1`..`9` (`src/app/application.cpp:191,203,215,231`).

`setting` changes a value at runtime by driving the generated KConfigXT
property on the `DockSettings` singleton. It exists because krema has **no**
`KConfigWatcher`: rewriting `kremarc` while it runs does nothing. Driving the
property follows the real path an immediate settings change takes — mutator,
`*Changed` signal, QML rebinding.

Assertion kinds:

| `kind` | asserts |
|---|---|
| `animates` | value leaves `from`, reaches `to`, and no frame covers more than `max_step_ratio` (default 0.75) of the range |
| `settles` | numeric property equals `value` at `frame` |
| `equals` | string/bool property equals `value` at `frame` |
| `ordered` | several items are strictly `ascending`/`descending` at `frame` |
| `stable` | property present every frame and never changes |
| `absent` | no item with this name exists at `frame` |
| `count` | number of items of `type` (optionally `name_prefix`, `visible_only`) |
| `menu` | active popup menu `entries` in order, plus `contains`/`excludes`/`enabled` |
| `window` | a window with `key` exists, with expected `visible`/`w`/`h` |

Every assertion carries `qa`, the checklist id it covers, and results are
reported per id. `animates` is the one that matters — it fails both when the
value never reaches its target and when it snaps there in one frame.

## Screenshot and video review

`KREMA_SCREENSHOTS=1` dumps a PNG per frame and writes `review.md` /
`review.json` per scenario, pairing each assertion's keyframes with its
expectation in prose. This is the input for visual review by a person or a
vision model.

`KREMA_VIDEO=1` additionally encodes `<scenario>.mp4` from those frames. Every
frame is stamped with its number, virtual time, the action firing on it and any
assertion anchored to it, so a failing frame number scrubs straight to the
moment it describes. Fedora's `ffmpeg-free` has neither libx264 nor libvpx, so
the encoder ladder is libopenh264, then SVT-AV1, then MPEG-4; all frames are
padded to one canvas because the dock window resizes mid-capture when the edge
or the icon size changes.

```bash
docker run --rm -e KREMA_VIDEO=1 \
    -v "$PWD:/src:ro" -v /tmp/krema-frames:/out \
    krema-ui-ci bash /src/tests/ci/run-frame-tests.sh hover-zoom
```

Both are off by default because PNG encoding dominates runtime. After encoding,
`prune_frames.py` keeps only the keyframes `review.md` links and gzips the first
pass's capture stream. The first CI run of the strict harness must establish the
new runtime and artifact-size baseline.

Numeric assertions and strict capture reproducibility decide pass/fail. Every
pass must have the same frame numbers and complete captured window, item, menu,
geometry, and property payload. The video remains for the part that is genuinely
visual — whether the glow reads as a glow.

## Reading results in CI

Three places, in the order a reviewer needs them.

**Job summary** — `summarize.py` writes assertion-group and unique-QA counts,
which QA items failed or were not verified, expected versus measured values,
and strict per-scenario reproducibility. No download.

**Pull request comment** — an animated GIF of every scenario, embedded inline,
failures first and the rest folded per feature area. This is the only way to see
motion on GitHub without downloading: raw `.mp4` and release assets are both
served as `application/octet-stream` with `content-disposition: attachment`, so
a `<video>` tag never plays, while raw image URLs return a real `image/*` type.

The GIFs keep every captured frame — none are dropped. Decimating to a lower
frame rate would hide what the suite exists to catch: a 150 ms animation is nine
frames, and throwing five away leaves a blur indistinguishable from a snap. They
are fitted inside a 620x380 box rather than scaled to a fixed width, because a
vertical dock is a 108 px wide window, so upscaling adds interpolated noise and
unnecessary artifact weight.

They live on a `ci-media` branch under `pr-<n>/<run id>/`. Each run clones the
existing tree, drops only its own older runs, and force-pushes the result as a
parentless commit, so other pull requests keep working previews and the branch
never accumulates history. The run id is in the path because GitHub proxies
markdown images through camo and caches by URL — a fixed path would serve the
previous run's animation beside current numbers. A separate workflow drops the
directory when the pull request closes.

The test job has only `contents: read`, checks out with
`persist-credentials: false`, and uploads an artifact. A second job downloads
that artifact without checking out or executing pull-request source; only that
publication job has `contents: write` and `pull-requests: write`. Fork pull
requests skip publication.

The comment is found by a hidden `<!-- krema-frame-tests -->` marker and patched
in place; `gh pr comment --edit-last` posted a second one instead, leaving a
stale summary with dead images above the current result. If the body would
exceed GitHub's size limit it degrades to failures and not-verified scenarios.

**`frame-captures` artifact** — MP4 for every scenario, `result.json`, the
gzipped capture streams, and the keyframes `review.md` links.

## What to test

`tests/ci/qa-checklist.md` lists 140 user-facing QA items with an automation
tag each. `qa_coverage.py` derives and validates the coverage table directly
from those entries, so category totals cannot drift from the checklist.

## Previously observed behaviour and current limits

Previously observed on Fedora 43, KWin 6.7.3, Qt 6.10, aarch64, and llvmpipe;
the current strict harness still requires its first CI validation.

**Previously observed with the earlier harness:**

- `kwin_wayland --virtual` starts unprivileged with no `/dev/dri`, advertising
  66 globals including `zwlr_layer_shell_v1`, `org_kde_plasma_window_management`,
  `org_kde_kwin_fake_input` and `zkde_screencast_unstable_v1`.
- Krema configures, builds, installs, maps its layer-shell surface on the
  virtual output and populates four launcher `DockItem`s.
- The RHI shader path works on llvmpipe — `MultiEffect` (attention glow, badge
  shadow) rasterises; `QT_QUICK_BACKEND=software` must **not** be set or
  `ShaderEffect`/`MultiEffect` silently render nothing.
- A 150 ms `Behavior on currentScale` is captured across 9 frames with the
  `OutCubic` curve and the parabolic neighbour falloff both visible.

**Known limits:**

- **`kwin_wayland` file capability.** The package ships `cap_sys_nice=ep`; an
  unprivileged container's bounding set has no `CAP_SYS_NICE`, so `execve()`
  fails with `EPERM` before `main()`. The Dockerfile runs `setcap -r`.
- **No compositor-side screenshots.** Without a DRM render node KWin logs
  `Failed to open drm node /dev/dri/renderD128` and falls back to the QPainter
  scene; `org.kde.KWin.ScreenShot2` then answers every capture with
  `Error.Cancelled`. Forcing `KWIN_COMPOSE=O` (with or without
  `EGL_PLATFORM=surfaceless`) does not recover it — the compositor fails to come
  up. Client-side `grabWindow()` is used instead and is strictly better for the
  app's own rendering; only genuinely compositor-side effects (KWindowEffects
  blur-behind, window stacking) remain untestable here.
- **`QTimer`/QML `Timer` are not virtualised.** The animation driver governs
  `QAbstractAnimation` only. `src/qml` has seven wall-clock timers (tooltip
  delay, `autoPreviewTimer`, `dragHoldTimer`, attention and launch-tracking
  timers). Timer-gated state therefore lands on a frame that varies between
  runs. Set `KREMA_PROBE_PACE=1` to make each tick consume `stepMs` of real time
  so timers and the virtual clock advance together, or keep timer-gated items
  out of byte-exact assertions.
- **Animation ticks advance by the driver step, not the real clock.** By default
  `QUnifiedTimer` measures each tick against the wall clock and may hand a new
  animation a catch-up delta. The probe calls
  `QUnifiedTimer::setConsistentTiming(true)`, which is why the test-only target
  links `Qt6::CorePrivate`; a permanently running `QVariantAnimation` keeps the
  unified timer active between user actions.

  The capture is now a strict gate rather than a diagnostic. Every pass must
  contain the same number and sequence of frames and the same complete
  canonical payload, excluding only the derived virtual-time field. Frame
  shifting, omitted edge frames, changed item identity, popup mapping changes,
  or any numeric/property difference fails the scenario.
- **No `Animator` types.** `ScaleAnimator`, `OpacityAnimator` and friends run on
  the render thread outside `QUnifiedTimer` and would escape the fixed-step
  clock. `src/qml` currently uses none; keep it that way, or frame-stepped
  determinism breaks silently.
- **GitHub x86_64 validation is pending.** The previous GitHub run predates the
  strict comparator, mapped-popup requirement, real menu-click path, and
  release-build guard. It is not evidence that the current harness passes.
- **Window rows need a window fixture — but they do work.** Measured: krema
  alone shows 4 `DockItem`s (the pinned launchers); mapping one plain
  `xdg_toplevel` Qt client in the same container makes it 5. The virtual backend
  advertises `org_kde_plasma_window_management` and `WaylandTasksModel`
  populates from it, contrary to what `docs/kde/tasksmodel-virtual-session.md`
  previously assumed. Window-dependent scenarios must therefore launch a client
  before asserting. Still unverified headlessly: `IsDemandingAttention`, and
  PipeWire previews (no PipeWire daemon in the image).

## Escalation path

`assert_frames.py` covers deterministic, numeric expectations. When a scenario's
expected outcome is genuinely visual ("the glow looks right"), the per-frame
PNGs under `<out>/<scenario>/pass1/frames/` are the input for a vision model —
but reach for that only after the numeric assertions cannot express the
property, because a numeric assertion fails with a reason and a VLM does not.
