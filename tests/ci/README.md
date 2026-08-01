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
  "description": "한 문장으로: 사용자가 무엇을 하면 무엇이 보여야 하는가",
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
     "prop": "currentScale", "comment": "커서를 올리면 1.0에서 1.6까지 커진다",
     "from": 1.0, "to": 1.6, "start_after": 5, "settled_by": 25}
  ]
}
```

`config` becomes `~/.config/kremarc` before krema starts — that is how a user's
saved settings reach the dock. Keys are `Group/Entry`; the group defaults to
`General`. Names and defaults come from `src/config/krema.kcfg`.

`max_frames` overrides the default 140-frame capture for scenarios that need
longer, such as the edge transition (~45 frames to settle on its own).

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

## Screenshot review

`KREMA_SCREENSHOTS=1` dumps a PNG per frame and writes `review.md` /
`review.json` per scenario, pairing each assertion's keyframes with its
expectation in prose. This is the input for visual review by a person or a
vision model; it is off by default because PNG capture dominates runtime.

```bash
docker run --rm -e KREMA_SCREENSHOTS=1 \
    -v "$PWD:/src:ro" -v /tmp/krema-frames:/out \
    krema-ui-ci bash /src/tests/ci/run-frame-tests.sh hover-zoom
```

Numeric assertions still decide pass/fail. The manifest exists for the part
that is genuinely visual — whether the glow reads as a glow.

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
- **Run-to-run reproducibility is structural, not bit-exact.** Measured over the
  scenarios in this repo, roughly half are byte-identical across two passes and
  the rest differ, with the differing frames clustered in the first two or three
  frames after an animation starts: `QUnifiedTimer` registers a newly started
  animation through a deferred 0 ms timer, so whether the frame that applies an
  action also sees the first animation step varies. What *is* reproducible:
  frame count, item set, every settled value, and animation shape — therefore
  every assertion here. Write assertions on settled frames and on curve shape;
  never on the exact value of an animation's first two frames. Each scenario
  reports `repro: byte-identical` or `REPRO differs ... largest gap <n> at
  <item>`, so the magnitude is visible rather than hidden behind a count.
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
