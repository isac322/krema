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

Roughly 25 s per run after the image is cached (build + two capture passes +
assertions). No `--privileged`, no `--cap-add`, no `/dev/dri`, nothing on the
host but a container runtime.

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
├── dbus-run-session                     isolated session bus
│   └── kwin_wayland --virtual           KDE compositor, headless, llvmpipe
│       └── krema (built with -DKREMA_TEST_HOOKS=ON)
│           └── tests/ci/frameprobe      fixed-step clock + capture + input
└── tests/ci/assert_frames.py            assertions over the captured stream
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
`setting`, `shortcut`, `menuitem`. Target an item by `objectName` (`"item": "dockItem0"`, with optional
`x`/`y` offset from its centre) rather than raw coordinates — a raw point
silently lands in the gap between icons if the icon size changes, and the
failure then reads as "the animation never happened". Add `"window": "<key>"`
to address a surface other than the dock.

`menuitem` activates an entry of the open context menu by its label
(`{"type": "menuitem", "name": "Settings..."}`). The menu is a native QMenu, so
a synthetic click on the Quick window cannot reach it; triggering the QAction is
what the user's click ends up doing. Open the menu with a right `click` first.

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

Both are off by default: PNG encoding dominates runtime, and the raw frames are
about 170x the size of the video. After encoding, `prune_frames.py` keeps only
the keyframes `review.md` links and gzips the first pass's capture stream,
which takes a full run from ~600 MB to ~17 MB.

Numeric assertions still decide pass/fail. The video is for the part that is
genuinely visual — whether the glow reads as a glow.

## Reading results in CI

Three places, in the order a reviewer needs them.

**Job summary** — `summarize.py` writes which QA items failed, at which frame,
expected versus measured, plus per-scenario reproducibility. No download.

**Pull request comment** — an animated GIF of every scenario, embedded inline,
failures first and the rest folded per feature area. This is the only way to see
motion on GitHub without downloading: raw `.mp4` and release assets are both
served as `application/octet-stream` with `content-disposition: attachment`, so
a `<video>` tag never plays, while raw image URLs return a real `image/*` type.

The GIFs keep every captured frame — none are dropped. Decimating to a lower
frame rate would hide what the suite exists to catch: a 150 ms animation is nine
frames, and throwing five away leaves a blur indistinguishable from a snap. They
are fitted inside a 620x380 box rather than scaled to a fixed width, because a
vertical dock is a 108 px wide window and upscaling it turned a 32 KiB animation
into 1.6 MB of interpolated noise. All 42 come to about 2.8 MB.

They live on a `ci-media` branch under `pr-<n>/<run id>/`. Each run clones the
existing tree, drops only its own older runs, and force-pushes the result as a
parentless commit, so other pull requests keep working previews and the branch
never accumulates history. The run id is in the path because GitHub proxies
markdown images through camo and caches by URL — a fixed path would serve the
previous run's animation beside current numbers. A separate workflow drops the
directory when the pull request closes. Fork pull requests get a read-only token
and the step no-ops.

The comment is found by a hidden `<!-- krema-frame-tests -->` marker and
patched in place; `gh pr comment --edit-last` posted a second one instead,
leaving a stale summary with dead images above the current result. If the body
would exceed GitHub's size limit it degrades to failures only.

**`frame-captures` artifact** — MP4 for every scenario, `result.json`, the
gzipped capture streams, and the keyframes `review.md` links. ~8 MB.

## What to test

`tests/ci/qa-checklist.md` lists 140 user-facing QA items with an automation
tag each. Scenarios here implement the `AUTO` and `AUTO-REQ` ones.

## Verified behaviour and limits

Measured on this image (Fedora 43, KWin 6.7.3, Qt 6.10, aarch64, llvmpipe).

**Works:**

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
- **An animation's first frame is not reproducible.** Everything after it is.
  `QUnifiedTimer` registers a newly started animation through a deferred 0 ms
  timer, so the registration lands on either side of the next clock advance
  depending on sub-frame timing, and the first delta varies: measured 39%, 43%,
  51%, 53%, 54%, 69% and 100% of range across runs of the same scenario.
  Draining posted events before advancing does not fix it (measured, made it
  worse). Consequences:
  - `animates` skips the first two frames when judging whether a value snapped
    or eased (`skip_onset_frames`), because that judgement on the onset frame is
    a coin flip. A real one-frame snap still fails, through the
    intermediate-value floor.
  - Never assert an exact value on an animation's first two frames.
  - Roughly half the scenarios are byte-identical across two passes; the rest
    differ only at frames where an action or an animation starts. Frame count,
    item set, every settled value and the shape of the animation body are
    reproducible, which is what the assertions here rest on. Each scenario
    reports `repro: byte-identical` or the largest gap and where it is.
- **No `Animator` types.** `ScaleAnimator`, `OpacityAnimator` and friends run on
  the render thread outside `QUnifiedTimer` and would escape the fixed-step
  clock. `src/qml` currently uses none; keep it that way, or frame-stepped
  determinism breaks silently.
- **Only exercised on aarch64 so far.** Every measurement above comes from an
  `linux/arm64` container. `frame-tests.yml` runs on `ubuntu-latest`, i.e.
  x86_64, where nothing here has executed yet: the unprivileged compositor
  start, the `setcap -r` workaround, llvmpipe `MultiEffect` rasterisation, the
  ~25 s runtime and the exact onset floats are all unvalidated on that arch.
  Treat the first x86_64 CI run as the validation step, not as a regression
  gate — the base image is multi-arch, so no image change should be needed.
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
