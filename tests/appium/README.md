# Krema AT-SPI E2E tests

This is the Tier 2 suite: it runs a source-built `krema` in a real KWin
session. Tier 3 reuses the same scenarios against the installed package on
each supported distribution; see `tests/distro/README.md`.

Automated end-to-end tests for the real dock. Each test starts a fresh krema
inside a private `kwin_wayland --virtual` session, drives it with real input
(KWin fake-input), and asserts on observable
state: the AT-SPI tree (through
KDE's [selenium-webdriver-at-spi]), KWin's window list, `kremarc`, and
screenshots.

Everything runs in one unprivileged container (`docker run`, no
`--privileged`, no KVM, no GPU), on linux/amd64 and linux/arm64.

[selenium-webdriver-at-spi]: https://invent.kde.org/sdk/selenium-webdriver-at-spi

## Running

```sh
tests/appium/run-e2e.sh                          # every tests/appium/test_*.py
tests/appium/run-e2e.sh test_smoke.py            # one file
tests/appium/run-e2e.sh test_smoke.py::test_focus_dock_shortcut_focuses_a_dock_button
tests/appium/run-e2e.sh -k zoom -x               # any pytest arguments
tests/appium/run-e2e.sh --shell                  # shell in the container after the krema build
```

Test paths may also be given relative to the repository root
(`tests/appium/test_smoke.py`). The script exits with pytest's status.

Multi-monitor tests are marked `@pytest.mark.outputs(n)` and run in a
session with `n` outputs, placed left to right:

```sh
KREMA_E2E_OUTPUT_COUNT=2 KREMA_E2E_ARTIFACTS=tests/appium/artifacts-2out \
    tests/appium/run-e2e.sh -m outputs -rs \
    --deselect test_06_settings.py::test_set012_selected_subset_preserves_docks_and_routes_shortcuts
KREMA_E2E_OUTPUT_COUNT=3 KREMA_E2E_ARTIFACTS=tests/appium/artifacts-3out \
    tests/appium/run-e2e.sh \
    test_06_settings.py::test_set012_selected_subset_preserves_docks_and_routes_shortcuts -rs
```

`--output-count n` creates `n` outputs of `KREMA_E2E_SCREEN_WIDTH` x
`KREMA_E2E_SCREEN_HEIGHT` each. Screenshots and previews need a DRM render
node (see "Screenshots and previews"): `sudo modprobe vgem` on kernels that
ship it, otherwise `sudo tests/appium/setup-vgem.sh` builds and loads vgem
out-of-tree. On kernels >= 6.15, whose vgem is a faux device, use
`setup-vgem.sh` (after `rmmod vgem`) for Tier 3 targets with KWin < 6.5
(see `tests/distro/README.md`); it builds vgem as the platform device those
KWin versions expect.

The default one-output session intentionally skips SET-008's two-output
cases, SET-012's two- and three-output cases, and SET-013/VIS-008's
two-output cases because their required output count differs. The two-output
command above explicitly deselects the three-output subset case; the
three-output command runs that node alone. Use both runs for full
multi-output coverage.
`tools/run-output-count.patch` makes `selenium-webdriver-at-spi-run` pass
`--output-count` to `kwin_wayland`. On the second output, Qt reports a
dock's AT-SPI rects shifted by the output's x offset.

What it does:

1. `docker build` of `tests/appium/Dockerfile` (cached; `KREMA_E2E_SKIP_BUILD=1`
   skips it).
2. Mounts the checkout read-only at `/src`, rsyncs it to `/work`, and builds
   the `krema` target incrementally into the named volume
   `krema-e2e-build-<arch>` (`KREMA_E2E_VOLUME` overrides it; delete the
   volume for a clean build).
3. Starts PipeWire, then `selenium-webdriver-at-spi-run`, which creates a
   private D-Bus session, `kwin_wayland --virtual`, the AT-SPI bus, and the
   WebDriver server on `:4723`, and finally runs pytest inside that session.

With CMake: `-DBUILD_TESTING=ON -DKREMA_E2E_TESTS=ON` registers the whole
suite as the CTest test `krema_e2e` (label `e2e`), which calls
`run-e2e.sh`.

### Local setup

**Linux.** Docker and the vgem virtual DRM driver are the only requirements —
`run-e2e.sh` sees `/dev/dri` and passes it into the container:

```sh
sudo modprobe vgem          # render node; or `sudo tests/appium/setup-vgem.sh`
tests/appium/run-e2e.sh
```

**macOS.** Docker always runs in a VM (OrbStack, Docker Desktop, Lima), and
none of those kernels ships `vgem`, so `/dev/dri` never appears.
Without a DRM render node KWin 6.7 falls back to QPainter compositing:
ScreenShot2 answers every request with an error and `zkde_screencast`
(KPipeWire preview thumbnails) is disabled — tests needing a screenshot
skip or fail (see "Screenshots and previews"). Run the suite inside a Lima
VM with a generic kernel instead:

```sh
limactl create --name=krema-e2e --cpus=8 --memory=16 --disk=80 \
    --vm-type=vz template://docker-rootful
# load vgem on every boot (and once now); the VM's kernel (>= 6.15) makes it
# a faux device, which KWin < 6.5 cannot use: for Tier 3 targets with such a
# KWin, replace it after each boot with `sudo rmmod vgem &&
# sudo tests/appium/setup-vgem.sh` (needs make, gcc and the kernel headers)
limactl shell krema-e2e -- sudo sh -c 'echo vgem >/etc/modules-load.d/vgem.conf'
limactl shell krema-e2e -- sudo modprobe vgem
# the checkout must be writable: run-e2e.sh recreates tests/appium/artifacts/
# and the Docker build context is the repository
limactl edit krema-e2e --set '.mounts=[{"location":"<repo>","writable":true}]'

limactl shell --workdir <repo> krema-e2e -- sudo tests/appium/run-e2e.sh
```

(`limactl edit` applies on the next start; `limactl stop krema-e2e` first if
the VM is already running.)

### Environment variables

Consumed by `run-e2e.sh`/`entrypoint.sh` (host side) and `krema_e2e.env`
(inside the session):

| Variable | Default | Effect |
|---|---|---|
| `KREMA_E2E_SKIP_BUILD` | unset | `1` skips the `docker build`; the image must already exist |
| `KREMA_E2E_IMAGE` | `krema-e2e:local` | image tag to build and run |
| `KREMA_E2E_VOLUME` | `krema-e2e-build-<arch>` | named volume holding the incremental krema build; delete it for a clean build |
| `KREMA_E2E_ARTIFACTS` | `tests/appium/artifacts` | output directory; emptied at the start of each run |
| `KREMA_E2E_PLATFORM` | native | `--platform` for docker build/run, e.g. `linux/amd64` |
| `KREMA_E2E_SCREEN_WIDTH`, `KREMA_E2E_SCREEN_HEIGHT` | 1024, 768 | size of each virtual output (px) |
| `KREMA_E2E_OUTPUT_COUNT` | 1 | number of outputs (`--output-count`), laid out left to right; tests marked `@pytest.mark.outputs(n)` run only when the session has exactly `n` |
| `KREMA_E2E_DOCKER_ARGS` | unset | extra `docker run` arguments, e.g. `--cpus=2` to approximate a slower CI runner |
| `KREMA_E2E_SHARD` | unset | `<i>/<N>` (0-based): run only every N-th collected test and deselect the rest. `tests/distro/run-distro-e2e.sh` sets it per shard container (`KREMA_E2E_SHARDS`); composes with `-k`/`-m` (sharding applies to the filtered collection). The default build volume gets a `-shard-<i>` suffix |

### CI

`run-e2e.sh` is the local development loop; CI does not call it on a
source-built krema. Three workflows are involved:

* `.github/workflows/ci-images.yml` publishes the prebuilt images the other
  two pull, all in the package `ghcr.io/isac322/krema-ci`, tagged by
  `tests/distro/image-ref.sh` with a hash of the files that define each
  image: the `ctest-image` stage of this directory's Dockerfile, each distro
  target's runtime and builder images (`tests/distro/README.md`), and a
  `FROM scratch` image holding `vgem.ko` built by `setup-vgem.sh` on the
  runner kernel. It runs on pushes to `master` that change an image input,
  daily (every distro target and the ctest image are rebuilt and re-pushed)
  and on demand, and seeds each distro target's package-build ccache on the
  default branch. A ref missing from ghcr.io, e.g. in a pull request that
  changes an image input, is built locally by
  `tests/distro/build-ci-image.sh` instead, so no result depends on the
  registry.
  Its scheduled and dispatched runs also prune the package: untagged
  versions older than 2 days and superseded-hash tags not updated for 14
  days are deleted, never a ref the current tree computes.
* `.github/workflows/e2e.yml`, job `Build & tests`: pulls the ctest image
  (the `base` package set plus ccache, without the AT-SPI stack), builds
  every target with `BUILD_TESTING=ON` through ccache (persisted across runs
  in an `actions/cache` `.ccache/` dir, `CCACHE_MAXSIZE=500M`, seeded by runs
  on `master`), and runs the full `ctest` (unit, integration, KWin, QML and
  desktop-entry tests) except the `e2e` label, in parallel with
  `-j$(nproc)`.
* `.github/workflows/distro-e2e.yml`: runs this suite against the packaged
  krema on each of the 12 distro targets (`tests/distro/README.md`), on
  `kwin_wayland --virtual` with an out-of-tree vgem (`setup-vgem.sh`;
  GitHub's Azure kernel ships no vgem, so the module for the runner kernel
  comes from ghcr.io, or is built in the job on a kernel ci-images.yml has
  not seen). The suite runs sharded in three containers
  (`KREMA_E2E_SHARDS=3`, `KREMA_E2E_SHARD` above). The `fedora-43` and
  `ubuntu-26.04` entries also run two-output SET-008 and SET-012 cases, then
  the three-output SET-012 subset/shortcut case, reusing the installed
  package image. Each leg writes
  `tests/appium/artifacts-distro-<target>-2out/junit.xml` or
  `tests/appium/artifacts-distro-<target>-3out/junit.xml` and uploads the
  matching `distro-e2e-<target>-2out` or `distro-e2e-<target>-3out` artifact.
  Each run adds a JUnit summary to the step summary. See
  [installed-package reproduction commands](../distro/README.md#selected-monitor-reproduction).

Changes that only touch `tests/qml`, `tests/unit`, `tests/kwin`,
`tests/integration` or Markdown do not start `distro-e2e.yml`; changes that
only touch this directory's tests and harness (anything but the
Dockerfile), `tests/distro` (except its two image scripts) or Markdown do
not start `Build & tests`.

### Artifacts (`tests/appium/artifacts/`, gitignored, recreated every run)

| File | Content |
|---|---|
| `junit.xml` | JUnit report |
| `pytest.log` | pytest output (also streamed to the console) |
| `session.log` | KWin, D-Bus, AT-SPI and WebDriver stderr, and how `kwin_wayland` ended (`[e2e] kwin_wayland exited with status N` / `killed by signal N`, also printed to the console when the session fails) |
| `<test id>/krema-N.log` | krema stdout/stderr for the N-th start in that test |
| `<test id>/failure.png`, `atspi-tree.xml`, `kwin-windows.json`, `kremarc` | written when a test fails |
| `<test id>/slide-samples.txt` | VIS-002/VIS-003: time and y of every dock item sample taken while the panel slid |
| `<test id>/thumbnail-focus.txt` | KBD-006: focus ring vs other thumbnail blue pixel counts, one line per screenshot until the ring was painted |
| `<test id>/reservation-geometry.jsonl` | VIS-009: observed KWin fixture ID/frame/workarea/full-output bounds and resting AT-SPI icon/dock-surface bounds for reserved states |
| `test-windows.log` | fixture window output, including received key presses |
| `krema-build.log`, `cmake-configure.log` | krema build |
| `kactivitymanagerd.log` | activity manager output, only when the image has it (Tier 3; see below) |
| `startup-crash/` | logs of a session whose KWin crashed before pytest started (see below) |
| `[startup-crash-]backtrace-<core>.txt` | gdb backtrace of a KWin core, only when the host's `core_pattern` is a path mounted into the container and the image has gdb |

**KWin startup crash.** `kwin_wayland` occasionally dies from SIGSEGV (in
its `QQmlThread`) about a second into the session, before
`selenium-webdriver-at-spi-run` hands over to pytest: an upstream crash,
seen in about 1 of 100 CI sessions. `entrypoint.sh` then restarts the whole
session exactly once (fresh `XDG_RUNTIME_DIR`, PipeWire, D-Bus, KWin and
XDG homes) and says so on the console (`[e2e] kwin_wayland crashed during
session startup (signal N); restarting the session once`). It does so only
if all three hold: the session failed, KWin died from a signal, and the
session's inner half, which runs pytest, never started, so no test ran and
no krema was started. A crash after pytest started, a second startup crash,
or any other failure fails the run as before. To get a backtrace (e.g. for
an upstream report), run with `sudo sysctl kernel.core_pattern=/tmp/cores/core.%e.%p`,
`KREMA_E2E_DOCKER_ARGS="-v /tmp/cores:/tmp/cores --ulimit core=-1"` and an
image with gdb (symbols come from debuginfod when `DEBUGINFOD_URLS` is set in the container).

**Activity manager.** When the image has kactivitymanagerd (Tier 3:
krema's `plasma-workspace` dependency pulls it in), `entrypoint.sh` starts
it inside the kwin session before pytest and waits for
`org.kde.ActivityManager` on the bus, as Plasma starts it as a session
service. Left to D-Bus activation, it would start from the bus's
environment, which has no `WAYLAND_DISPLAY`: it tried xcb and aborted
(SIGABRT) on every krema start (about 100 times per CI run), and the CI
runner's apport processed each core dump.

### Timings (this host: OrbStack on macOS arm64, 10 CPUs)

| Step | Cold | Warm |
|---|---|---|
| Image build | ~6 min (dnf ~4.2 min, image export ~1 min, selenium-webdriver-at-spi ~14 s) | ~1 s (cached) |
| krema build | ~35 s (empty volume) | <1 s (no changes) |
| `test_smoke.py` (6 tests) | ~12 s | ~12 s |

## Coverage

`test_smoke.py` (6 tests) exercises the harness itself: the `Krema Dock`
tool bar is exposed and bottom-anchored, windows group into one dock item,
a click activates a window, hover zooms, the Focus Dock shortcut focuses a
dock button, and ScreenShot2 captures the rendered dock.

The scenario suites `test_01_keyboard_nav.py` … `test_07_visibility.py`
automate the manual checklists in `tests/e2e/scenarios/0[1-7]-*.md`.
`test_08_reservation.py` adds the real maximized-window geometry checks.
`test_09_task_zones.py` adds real pinned/running ordering, lifecycle,
separator accessibility/geometry, and zone-drag checks.
`test_10_task_zone_input.py` adds native boundary-input checks for task-zone
keyboard traversal, magnified hit targets, and grouped-preview retargeting.
Each scenario's `**Automated:**` lines point back to the tests below. Rows with
named automated nodes assert on the AT-SPI tree (states, names, geometry),
the KWin window list, the `kremarc` file, pixel analysis of ScreenShot2
screenshots, or AT-SPI events. Existing pass statuses reflect the recorded
suite results. Issue 54 rows remain `pending` until the parent completes Tier 2
and packaged Tier 3 QA.
The issue #55 regression `QA-PREV-01` has recorded pre-fix and fixed results:
the pre-fix run failed after a 33 ms entry, while the fixed run passed three
fresh opens in one run. The fast path is tested separately from pixel waits.

`MOUSE-017` is a native KWin test, not an Appium fixture. Its test-only
scripted effect reads `EffectWindow.iconGeometry` independently of
libtaskmanager. Initial publication, dock movement, icon resizing, hidden
new-task/group creation, reveal, and teardown passed in the updated runtime.
The old runtime failed the initial visible-task assertion with an empty target
before reaching hidden cases; no old-runtime hidden-case result was recorded.
Headless target assertions do not verify visible Magic Lamp or Squash rendering.

`KBD-009` runs three VisibilityMode variants; `KBD-007` runs the pointer
parked and at the screen centre. SET-008 runs with two outputs. SET-012 runs
its switch/fallback/persistence cases with two outputs and its
primary-excluding subset/shortcut case with three outputs. SET-013 and VIS-008
also have two-output cases; use the commands in Running above.
SET-008 is also covered by `tests/integration/test_settings_lifecycle.cpp`
(ctest `krema_integration_tests`).

Lower level than this suite, Tier 1 `tests/qml/` (`ctest -R qml`, label
`qml`; see `tests/qml/README.md`) loads the real QML files headless against
mocked C++ backends. The new click-policy checks use consumer-visible QML
state such as popup visibility, tooltip text, membership transitions, and
non-left input paths; they do not replace the Tier 2 KWin and AT-SPI oracles.
Tier 3 runs the Tier 2 Appium scenarios against installed distro packages.


| TC | Test(s) | Oracle | Status |
|---|---|---|---|
| KBD-001 | `test_01_keyboard_nav.py::test_kbd001_meta_alt_d_focuses_first_dock_item`, `test_kbd001_focus_dock_shortcut_focuses_first_dock_item` | AT-SPI, KGlobalAccel, screenshot | pass |
| KBD-002 | `test_01_keyboard_nav.py::test_kbd002_arrow_keys_move_focus_between_items` | AT-SPI | pass |
| KBD-003 | `test_01_keyboard_nav.py::test_kbd003_down_opens_preview_with_first_thumbnail_focused` | AT-SPI, screenshot | pass |
| KBD-004 | `test_01_keyboard_nav.py::test_kbd004_enter_activates_focused_thumbnail_window` | KWin, AT-SPI | pass |
| KBD-005 | `test_01_keyboard_nav.py::test_kbd005_escape_exits_keyboard_navigation` | AT-SPI | pass |
| KBD-006 | `test_01_keyboard_nav.py::test_kbd006_left_right_move_between_thumbnails` | AT-SPI, screenshot | pass |
| KBD-007 | `test_01_keyboard_nav.py::test_kbd007_delete_closes_focused_thumbnail_window` | AT-SPI, KWin | pass |
| KBD-008 | `test_01_keyboard_nav.py::test_kbd008_mouse_movement_cancels_keyboard_mode`, `test_kbd008_mouse_movement_over_dock_cancels_keyboard_mode` | AT-SPI | pass |
| KBD-009 | `test_01_keyboard_nav.py::test_kbd009_keyboard_mode_keeps_hidden_dock_visible`, `test_kbd009_dock_auto_hides_again_after_escape` | AT-SPI, KWin | pass |
| MOUSE-001 | `test_02_mouse.py::test_mouse001_left_click_activates_and_unminimizes_running_app`, `test_mouse001_click_without_motion_on_an_item_that_appeared_under_the_pointer` | KWin, AT-SPI | pass |
| MOUSE-002 | `test_02_mouse.py::test_mouse002_left_click_launches_pinned_app`, `test_mouse002_pinned_launch_bounces` (six click-policy cases for the launch test) | KWin, screenshot | baseline pass; Issue 54 cases pending |
| MOUSE-003 | `test_02_mouse.py::test_mouse003_parabolic_zoom_on_hover` | AT-SPI, screenshot | pass |
| MOUSE-004 | `test_02_mouse.py::test_mouse004_tooltip_shows_app_name_on_hover` | screenshot | pass |
| MOUSE-005 | `test_02_mouse.py::test_mouse005_scroll_wheel_cycles_grouped_windows` (six click-policy cases) | KWin | baseline pass; Issue 54 cases pending |
| MOUSE-006 | `test_02_mouse.py::test_mouse006_middle_click_launches_new_instance`, `test_mouse006_launch_bounce_lasts_until_the_new_window_maps` (six click-policy cases for the launch test) | KWin, AT-SPI events | baseline pass; Issue 54 cases pending |
| MOUSE-007 | `test_02_mouse.py::test_mouse007_indicator_dots_reflect_running_state` | screenshot | pass |
| MOUSE-008 | `tests/kwin/test_grouped_activation.cpp` (ctest `krema_grouped_activation_tests`; existing cycle/no-launch cases plus Issue 54 click-minimize cases) | KWin | baseline pass; Issue 54 cases pending |
| MOUSE-009 | `test_02_mouse.py::test_mouse009_in_place_zoom_scales_icons_without_moving_them` | AT-SPI, screenshot | pass |
| PREV-001 | `test_03_preview.py::test_prev001_hover_opens_preview_above_dock_with_live_thumbnails`; `test_08_reservation.py::test_prev_reservation_hover_popup_stays_inward_of_resting_dock_item` (four edges × reservation off/on, floating on; geometry/title only, no DRM) | AT-SPI, screenshot for original thumbnail case; KWin popup/surface/item geometry and real hover for reservation regression | baseline pass; reservation geometry regression 8/8 native Tier 2 pass; current thumbnail pixels/DRM and installed Tier 3 unverified |
| ICON-008 | `test_03_preview.py::test_icon008_minimized_preview_fallback_preserves_raw_artwork` | screenshot, KWin, TaskManager | pending; DRM capture required on current host |
| PREV-002 | `test_03_preview.py::test_prev002_grouped_app_shows_one_thumbnail_per_window_in_a_row` | AT-SPI | pass |
| PREV-003 | `test_03_preview.py::test_prev003_clicking_a_thumbnail_activates_that_window` | KWin, AT-SPI | pass |
| PREV-004 | `test_03_preview.py::test_prev004_close_button_closes_that_window`, `test_prev004_delete_key_closes_focused_thumbnail_window`, `test_prev004_closing_last_window_closes_preview_and_returns_to_dock` | KWin, AT-SPI | pass |
| PREV-005 | `test_03_preview.py::test_prev005_preview_closes_when_pointer_leaves`, `test_prev005_close_on_leave_is_delayed`, `test_prev005_preview_stays_closed_when_a_task_row_appears_while_leaving` | AT-SPI | pass |
| PREV-006 | `test_03_preview.py::test_prev006_single_window_preview` | screenshot, AT-SPI | pass |
| PREV-007 | `test_03_preview.py::test_prev007_opening_preview_announces_window_count` | AT-SPI event | pass |
| QA-PREV-01 | `test_03_preview.py::test_qa_prev01_atspi_visible_popup_accepts_fast_pointer_entry` | AT-SPI, KWin, real input | pass (pre-fix failed at 33 ms; fixed 1/1) |
| CTX-001 | `test_04_context_menu.py::test_ctx001_right_click_opens_native_menu_at_the_item`, `test_ctx001_about_krema_is_the_fifth_entry`, `test_ctx001_quit_is_the_last_entry` | KWin | pass |
| CTX-002 | `test_04_context_menu.py::test_ctx002_pin_keeps_the_app_in_the_dock_after_it_closes` | AT-SPI, kremarc | pass |
| CTX-003 | `test_04_context_menu.py::test_ctx003_unpin_removes_a_closed_app`, `test_ctx003_unpinned_running_app_stays_until_it_closes` | AT-SPI, kremarc | pass |
| CTX-004 | `test_04_context_menu.py::test_ctx004_new_instance_launches_another_window` | KWin | pass |
| CTX-005 | `test_04_context_menu.py::test_ctx005_close_closes_every_window_of_an_unpinned_app`, `test_ctx005_close_keeps_a_pinned_app_without_indicator` | KWin, AT-SPI | pass |
| CTX-006 | `test_04_context_menu.py::test_ctx006_settings_entry_opens_the_settings_window` | KWin, AT-SPI | pass |
| DND-001 | `test_05_drag.py::test_dnd_001_drag_reorders_dock_items` (also: focus returns to the previously active window) | AT-SPI, screenshot, KWin | pass |
| DND-002 | `test_05_drag.py::test_dnd_002_reorder_persists_after_restart` | AT-SPI, kremarc | pass |
| DND-003 | `test_05_drag.py::test_dnd_003_drag_shows_ghost_dimmed_source_and_drop_indicator` | screenshot | pass |
| ICON-009 | `test_05_drag.py::test_icon009_drag_ghost_uses_raw_client_artwork` | screenshot, KWin, TaskManager | pending; DRM capture required on current host |
| DND-004 | `test_05_drag.py::test_dnd_004_drag_released_outside_dock_keeps_order`, `test_dnd_004_escape_cancels_drag` (both also: focus returns to the previously active window) | AT-SPI, kremarc, screenshot, KWin | pass |
| SET-001 | `test_06_settings.py::test_set001_settings_opens_once_with_formcard_controls_and_keeps_dock_shown` | KWin, AT-SPI | pass |
| SET-002 | `test_06_settings.py::test_set002_icon_size_spinbox_resizes_dock_live_and_keeps_zoom_proportion` | AT-SPI, kremarc | pass |
| SET-003 | `test_06_settings.py::test_set003_auto_hide_applies_immediately_and_persists` | AT-SPI, kremarc | pass |
| SET-004 | `test_06_settings.py::test_set004_acrylic_background_applies_live` | screenshot, kremarc | pass |
| SET-005 | `test_06_settings.py::test_set005_changed_settings_persist_across_restart` | AT-SPI, kremarc, screenshot | pass |
| SET-006 | `test_06_settings.py::test_set006_screen_edge_top_moves_dock_to_top` | KWin, kremarc, screenshot | pass |
| SET-007 | `test_06_settings.py::test_set007_custom_tint_color_is_applied_and_saved` | AT-SPI, kremarc, screenshot | pass |
| SET-008 | `test_06_settings.py::test_set008_monitor_mode_all_monitors_from_open_settings`, `test_set008_follow_active_mouse_opening_settings_keeps_dock_on_its_screen`, `test_set008_follow_active_shortcuts_act_on_the_shown_dock`, `test_set008_follow_active_mouse_trigger_moves_dock_to_the_pointer_screen` | KWin, AT-SPI, kremarc | pass |
| SET-009 | `test_06_settings.py::test_set009_quit_while_settings_is_open_exits_cleanly` | KWin (process exit) | pass |
| SET-010 | `test_06_settings.py::test_set010_zoom_style_combo_switches_zoom_live_and_persists` | AT-SPI, kremarc | pass |
| SET-011 | `test_06_settings.py::test_set011_zoom_animation_preset_and_custom_tabs_apply_and_persist`; preset default/persistence/Instant/disabled state also in `test_set001_settings_opens_once_with_formcard_controls_and_keeps_dock_shown`, `test_set005_changed_settings_persist_across_restart`, `test_set010_zoom_style_combo_switches_zoom_live_and_persists` | AT-SPI, kremarc | partial: settings automated (validation pending for SET-011); timing and easing covered by QML tests, live-dock curve screenshots manual |
| SET-012 | `test_06_settings.py::test_set012_selected_monitors_toggle_keeps_settings_open`, `test_set012_selected_monitors_fallback_warns_and_keeps_saved_names[empty]`, `test_set012_selected_monitors_fallback_warns_and_keeps_saved_names[disconnected]` (2 outputs); `test_set012_selected_subset_preserves_docks_and_routes_shortcuts` (3 outputs) | KWin dock/preview output and identity, AT-SPI switches/warning/focus, kremarc, KGlobalAccel | automated; validation pending |
| VIS-001 | `test_07_visibility.py::test_vis001_always_visible_dock_stays_shown_over_a_maximized_window`, `test_vis001_always_visible_reserves_the_dock_area_for_maximized_windows` | AT-SPI, screenshot, KWin | pass |
| VIS-002 | `test_07_visibility.py::test_vis002_auto_hide_hides_after_timeout_and_frees_the_screen` | AT-SPI, KWin | pass |
| VIS-003 | `test_07_visibility.py::test_vis003_auto_hide_shows_on_screen_edge_approach` | AT-SPI, KWin | pass |
| VIS-004 | `test_07_visibility.py::test_vis004_dodge_windows_hides_while_a_window_overlaps_the_dock`, `test_vis004_dodge_windows_hides_for_an_inactive_overlapping_window` | AT-SPI, KWin | pass |
| VIS-005 | `test_07_visibility.py::test_vis005_smart_hide_hides_only_for_the_active_overlapping_window` | AT-SPI, KWin | pass |
| VIS-006 | `test_07_visibility.py::test_vis006_keyboard_navigation_keeps_auto_hide_dock_visible` | AT-SPI, KWin | pass |
| VIS-007 | `tests/kwin` ctest `krema_showdesktop_tests` (added on master) | KWin | pass (C++ KWin test, not this suite) |
| MOUSE-010 | `test_02_mouse.py::test_mouse010_click_policies_observe_single_and_group_window_state` (six policy cases) | KWin, AT-SPI | pending |
| MOUSE-011 | `test_02_mouse.py::test_mouse011_membership_change_reselects_single_and_group_actions` | KWin, AT-SPI | pending |
| MOUSE-012 | `test_02_mouse.py::test_mouse012_group2_restores_one_mru_child_when_all_children_are_minimized` | KWin, AT-SPI | pending |
| MOUSE-013 | `test_02_mouse.py::test_mouse013_group_preview_clears_tooltip_and_restores_it_after_close` (two cases: `window-hover500`, `launcher-hover0`) | KWin, screenshot | pending; DRM capture required |
| MOUSE-014 | `test_02_mouse.py::test_mouse014_fast_hover_launcher_tooltip_healthy_control` | screenshot | pending; DRM capture required |
| MOUSE-015 | `test_02_mouse.py::test_mouse015_unconfigured_defaults_keep_single_active_and_group_cycle_mru` | KWin | pending |
| MOUSE-016 | `test_02_mouse.py::test_mouse016_nonleft_activation_paths_ignore_mouse_click_policies` (six policy cases) | KWin, AT-SPI | pending |
| MOUSE-017 | `tests/kwin/test_delegate_geometry.cpp` (ctest `krema_delegate_geometry_tests`) | Independent KWin `EffectWindow.iconGeometry` | partial: initial target, move/resize, groups, teardown passed; hidden new-task/group and reveal pending; visible Magic Lamp/Squash animation manual |
| PREV-008 | `test_03_preview.py::test_prev008_explicit_group_click_shows_all_thumbnails_and_selected_child_closes` | KWin, AT-SPI, screenshot | pending; DRM capture required |
| PREV-009 | `test_03_preview.py::test_prev009_explicit_group_pending_hide_retargets_after_reenter` | AT-SPI, screenshot | pending; DRM capture required |
| PREV-011 | `test_11_preview_surface.py::test_prev011_grouped_preview_surface_tracks_two_three_and_single_layouts` (Left, Right, horizontal control) | Entire popup/thumbnail containment in KWin native surface, real last-thumbnail input, exact PID/internalId, wrong underlying-client negative, live grow/shrink/reuse, transparent-area pass-through | pass: source-built and Fedora 43 installed RPM; no DRM or pixel proof |
| DND-005 | `test_05_drag.py::test_dnd005_release_inside_outside_and_exit_reenter_preserves_window_state` (72 base scenarios: six policy pairs × 12 source/release cases, plus 24 held-left/right release-order controls) | KWin, AT-SPI, screenshot | pending; DRM capture required |
| CLK-002 | `test_06_settings.py::test_clk002_click_action_combinations_apply_live_persist_and_restore` (six cases), `test_06_settings.py::test_clk002_all_screens_share_live_click_choices_and_recreated_dock_restores_them` (`outputs(2)`) | KWin, AT-SPI, `kremarc` | pending |
| CLK-011 | `test_06_settings.py::test_clk011_preview_controls_follow_hover_and_explicit_group_choice` (six cases) | AT-SPI, screenshot, `kremarc` | pending; DRM capture required |
| CLK-012 | `test_07_visibility.py::test_clk012_repeated_explicit_preview_releases_visibility_hold` (six cases), `test_clk012_repeated_explicit_preview_releases_follow_active_screen_hold` (four `outputs(2)` cases) | KWin, AT-SPI, screenshot | pending; DRM capture required |
| KWIN-CLK | `tests/kwin/test_grouped_activation.cpp` ctest `krema_grouped_activation_tests`: seven named click-minimize/default/membership cases | KWin model state | pending |
| QML-CLK | `tests/qml/tst_dock_main.qml`: 12 named popup, tooltip, membership, group0, single, and non-left/drag consumer cases | QML consumer state | pending |
| SET-015 | `test_06_settings.py::test_set015_reservation_switch_is_native_conditional_and_autosaves` (native switch, autosave, mode-dependent visibility); VIS-009 for geometry/restart | AT-SPI switch, unchanged process, `kremarc`; KWin geometry in VIS-009 | native Tier 2 coverage; installed Tier 3 unverified |
| VIS-009 | `test_08_reservation.py::test_vis009_reservation_off_maximized_window_uses_full_output`, `test_vis009_already_maximized_window_reflows_on_live_reservation_toggle`, `test_vis009_live_icon_size_and_floating_update_maximized_consumer`, `test_vis009_visibility_policy_ignores_and_retains_reservation_preference`, `test_vis009_fresh_config_default_reserves_screen_space_for_maximized_consumer` (four edges, both saved preferences for mode changes; fresh default also verifies the AT-SPI description `Maximized windows avoid the dock`, checked/showing states, and no stored reservation key); `test_vis009_reservation_off_and_on_persist_and_reflow_existing_window_after_restart` (bottom) | Real KWin maximized state/frame/workarea/output bounds, window identity, and edge-aware dock geometry | native Tier 2 coverage; no DRM; installed Tier 3 unverified |
| SET-016 | `test_09_task_zones.py::test_tzone001_live_toggle_orders_visible_tasks_and_persists` (four edges, live on/off, ON/OFF restarts), `test_tzone007_fresh_default_separation_is_off` | AT-SPI switch/ordered items/separator, real app windows, `kremarc` | native Tier 2 coverage; no DRM; installed Tier 3 unverified |
| CTX-007 | `test_09_task_zones.py::test_tzone003_grouped_instances_keep_one_pinned_slot_and_close_differently`, `test_tzone004_pin_and_unpin_use_real_context_menu_and_preserve_membership`; TZONE-002 for empty-section divider state | Real fixture windows/context menu input, ordered AT-SPI app names, separator state, saved pinned membership | native Tier 2 coverage; no DRM; installed Tier 3 unverified |
| DND-006 | `test_09_task_zones.py::test_tzone005_on_reorders_inside_both_zones_and_clamps_cross_boundary`, `test_tzone006_off_allows_real_cross_zone_reorder_without_auto_pin` (both cross-boundary directions with separation on/off) | Real drags, ordered AT-SPI app names, saved pinned membership | native Tier 2 coverage; visual drop-indicator/drag ghost/paint unverified; installed Tier 3 unverified |
| KBD-010 | `tests/appium/test_10_task_zone_input.py::test_kbd010_task_navigation_survives_native_launch_and_close` (four edges) | Real keys, AT-SPI item focus, native launch/close, KWin activation | native Tier 2 coverage; F12 is a delivery probe, not launch proof; painted-divider quality and KWin RPC unverified |
| MOUSE-018 | `tests/appium/test_10_task_zone_input.py::test_mouse018_separation_modes_and_magnified_boundary_hits` (four edges × separation ON/OFF, `MaxZoomFactor=1.6`) | Real pointer hover, AT-SPI separator/item geometry, outward-neighbor reflow, exact native PID hit targets | native Tier 2 coverage; rest separator clearance is measured in TZONE-002 to iconSpacing ±1 px on all four edges; pixel/AA, overlap screenshots, and DRM unverified |
| PREV-010 | `tests/appium/test_10_task_zone_input.py::test_prev010_last_thumbnail_and_other_app_pin_transitions` (horizontal three-thumbnail and vertical two-thumbnail invocations) | Ordered grouped window IDs/titles, every-slot physical selection and exact KWin activation, AT-SPI preview state | Strict partition/order/index/pin-transition coverage with thumbnail-center reachability in the native input surface. PREV-011 covers whole-popup/thumbnail containment. Image/RHI/DRM/default-timing unverified |
The Tier 1 QML names are:
`test_groupPreviewClickShowsPopupAndSuppressesTooltip`,
`test_groupPreviewClickStopsDelayedTooltip`,
`test_groupPreviewClickReenterDuringHideKeepsTooltipHidden`,
`test_previewCloseWhilePointerOverItemRestartsTextTooltip`,
`test_fastLauncherTooltipAtZeroDelay`,
`test_fastLauncherTooltipRecoversAfterPreviewClose`,
`test_previewClosePreservesPendingHoverDeadline`,
`test_previewInvalidationDuringDragDoesNotReopenTooltip`,
`test_group0ClickDoesNotOpenPreview`,
`test_singleClickDoesNotOpenPreview`,
`test_membershipTransitionsUseCurrentGroupingAction`, and
`test_nonLeftKeyboardAndDragNeverOpenPreview`.


### Issue 54 click-policy contracts

The table maps every approved QA contract to a consumer observation and its
automation tier. `pending` means the test definition is present but the
parent's full verification has not run. QA-CLK-013 keeps the approved
source-review disposition for a real non-minimizable fixture.

| Contract | Consumer observation | Automation | Tier |
|---|---|---|---|
| QA-CLK-001 | Active single remains focused and unminimized; group0 keeps MRU A→B→A | `test_mouse015_unconfigured_defaults_keep_single_active_and_group_cycle_mru`; KWin `Default activation keeps an active single window focused and unminimized` | Tier 2 |
| QA-CLK-002 | Both settings apply live, save independently, survive restart, and remain shared across recreated docks | `SET-013`; `test_clk002_click_action_combinations_apply_live_persist_and_restore`; `test_clk002_all_screens_share_live_click_choices_and_recreated_dock_restores_them` | Tier 2, `outputs(2)` |
| QA-CLK-003 | Active single click minimizes the actual window | `test_mouse010_click_policies_observe_single_and_group_window_state`; KWin `Single-window minimize clicks honor actual focus and minimized state` | Tier 2 |
| QA-CLK-004 | Minimized single restores and takes focus | `test_mouse010_click_policies_observe_single_and_group_window_state`; KWin `Single-window minimize clicks honor actual focus and minimized state` | Tier 2 |
| QA-CLK-005 | Background single activates without minimizing | `test_mouse010_click_policies_observe_single_and_group_window_state`; KWin `Single-window minimize clicks honor actual focus and minimized state` | Tier 2 |
| QA-CLK-006 | Group1 opens one explicit popup, leaves single clicks alone, and suppresses text tooltip | `test_mouse010_click_policies_observe_single_and_group_window_state`; `test_prev008_explicit_group_click_shows_all_thumbnails_and_selected_child_closes`; `test_mouse013_group_preview_clears_tooltip_and_restores_it_after_close`; QML `test_groupPreviewClickShowsPopupAndSuppressesTooltip`, `test_singleClickDoesNotOpenPreview` | Tier 1 + Tier 2 |
| QA-CLK-007 | Group0 retains MRU cycling and does not minimize children | `test_mouse010_click_policies_observe_single_and_group_window_state`; `test_mouse015_unconfigured_defaults_keep_single_active_and_group_cycle_mru`; KWin `Left-click on a grouped app cycles through its windows`; QML `test_group0ClickDoesNotOpenPreview` | Tier 1 + Tier 2 |
| QA-CLK-008 | Explicit popup lists all children; selected child activates/restores and pending hide retargets | `test_prev008_explicit_group_click_shows_all_thumbnails_and_selected_child_closes`; `test_prev009_explicit_group_pending_hide_retargets_after_reenter` | Tier 2 |
| QA-CLK-009 | Launcher/startup and wheel no-launch behavior remains unchanged in all six policy pairs; no empty popup | `test_mouse002_left_click_launches_pinned_app`; `test_mouse005_scroll_wheel_cycles_grouped_windows` | Tier 2 |
| QA-CLK-010 | Accessible press, keyboard, Meta+N, middle/right, and wheel paths ignore mouse click policies | `test_mouse016_nonleft_activation_paths_ignore_mouse_click_policies`; six-case `test_mouse005_scroll_wheel_cycles_grouped_windows`; six-case `test_mouse006_middle_click_launches_new_instance`; QML `test_nonLeftKeyboardAndDragNeverOpenPreview` | Tier 1 + Tier 2 |
| QA-CLK-011 | Hover remains independent; explicit preview clears tooltip and restores it after close without overlap | `SET-014`; `test_mouse013_group_preview_clears_tooltip_and_restores_it_after_close`; `test_mouse014_fast_hover_launcher_tooltip_healthy_control`; `test_prev009_explicit_group_pending_hide_retargets_after_reenter`; `test_clk011_preview_controls_follow_hover_and_explicit_group_choice`; QML `test_groupPreviewClickStopsDelayedTooltip`, `test_groupPreviewClickReenterDuringHideKeepsTooltipHidden`, `test_previewCloseWhilePointerOverItemRestartsTextTooltip`, `test_fastLauncherTooltipAtZeroDelay`, `test_fastLauncherTooltipRecoversAfterPreviewClose`, `test_previewClosePreservesPendingHoverDeadline`, `test_previewInvalidationDuringDragDoesNotReopenTooltip` | Tier 1 + Tier 2 |
| QA-CLK-012 | Repeated explicit popup releases AutoHide/Dodge/SmartHide and follow-screen holds | `VIS-008`; `test_clk012_repeated_explicit_preview_releases_visibility_hold`; `test_clk012_repeated_explicit_preview_releases_follow_active_screen_hold` | Tier 2, `outputs(2)` |
| QA-CLK-013 | Invalid indices preserve unrelated focus/minimized state; non-minimizable guard remains source-review only | KWin `Invalid minimize-click indices preserve unrelated focus and minimized windows`; API/source review | Tier 2 + review |
| QA-CLK-014 | Actual inside/outside/exit-reenter drag releases preserve every window state and keep the popup closed; the additional held-left/right release-order controls preserve the same state and popup invariants | `test_dnd005_release_inside_outside_and_exit_reenter_preserves_window_state` (72 base scenarios plus 24 chord controls) | Tier 2 |
| QA-CLK-015 | 1→2→1 membership uses the current action immediately | `test_mouse011_membership_change_reselects_single_and_group_actions`; QML `test_membershipTransitionsUseCurrentGroupingAction`; KWin `Minimize-click targeting tracks single-group-single window membership` | Tier 1 + Tier 2 |
| QA-CLK-016 | Group2 minimizes only the active child and preserves other children | `test_mouse010_click_policies_observe_single_and_group_window_state`; KWin `Active grouped-window clicks minimize only the current child` | Tier 2 |
| QA-CLK-017 | Group2 restores only the existing MRU child when no child is active or all are minimized | `test_mouse012_group2_restores_one_mru_child_when_all_children_are_minimized`; KWin `Background grouped-window minimize clicks enter only the most recently used child` | Tier 2 |
| QA-CLK-018 | Group2 follows the current KWin focus after the previous child was minimized | `test_mouse010_click_policies_observe_single_and_group_window_state`; KWin `Grouped minimize clicks follow current KWin focus rather than the previous target` | Tier 2 |

The dedicated normal-hover pending-deadline and drag-time tooltip-invalidation
seams are covered by the named Tier 1 QML tests, not by new Appium fixtures.
ScreenShot2/PipeWire observations and installed-package Tier 3 runs require
the existing DRM/vgem environment. OrbStack's QPainter path cannot provide
that proof.

### Screen-reservation and task-section contracts

Run the native reservation-control and real maximized-window checks against
the current source build:

```sh
KREMA_E2E_SKIP_BUILD=1 tests/appium/run-e2e.sh \
    test_06_settings.py::test_set015_reservation_switch_is_native_conditional_and_autosaves \
    test_08_reservation.py -rs
```

`KREMA_E2E_SKIP_BUILD=1` reuses the existing image, not the source binary;
the runner still builds the changed source incrementally. This command
does not cover task-section acceptance checks. Off/live-toggle, icon-size/floating, and Auto Hide/Dodge Windows cases run on all four edges.
The matrix covers both saved reservation preferences with Settings open and after it closes, restoration to AlwaysVisible, restart persistence/reflow, the fresh-config default reservation case, and the supplemental hover-preview geometry cases.
Each geometry test writes
`reservation-geometry.jsonl` with
observed KWin frame/workarea/full-output bounds and fixture identity, plus
resting AT-SPI icon/surface geometry when reserved.
The supplemental hover-preview node in `test_08_reservation.py` checks the
actual popup and preview surface sit inward of the resting dock icon on
each edge with reservation off/on and floating enabled. It checks the
fixture title and visible popup geometry without DRM; live thumbnail pixels
are not exercised by this node.


Run the task-section suite separately:

```sh
KREMA_E2E_SKIP_BUILD=1 tests/appium/run-e2e.sh test_09_task_zones.py -rs
```

The live-switch and separator-geometry nodes run on all four edges.
The lifecycle/context-menu and drag nodes use the bottom edge. Both
cross-boundary drag directions are checked with separation on/off, and both
preferences are checked across restart. TZONE-007 separately checks the
unchecked native switch with a fresh configuration; the other nodes'
explicitly configured false preference is not proof of that default.

`SET-015`/`VIS-009` must inspect the existing maximized fixture window's
settled KWin frame geometry before and after each real settings toggle.
Sending a maximize command, reading `ReserveScreenSpace`, or checking a
source setter is not evidence that KWin changed the usable area. The
four-edge matrix compares the correct leading/trailing output edge, includes
floating gap and changed icon size, and checks Auto Hide/Dodge Windows with
both saved values. Restart checks observe restored switch state and frame
bounds, not just the saved key.

`SET-016`/`CTX-007` observe pinned membership and visual app order throughout
launch, close, grouping, pin, unpin, and restart. Running pinned apps stay in
their pinned slots with one icon per app; an empty section hides the divider.
`DND-006` uses actual drags in both directions: separation on permits
within-section ordering and clamps cross-section moves without auto-pinning;
separation off restores free ordering. The assertions cover nearest-valid
source-zone positions and membership; visual drop-indicator/drag ghost/paint
remain unverified.

`KBD-010` covers launch/close and focus traversal on all four edges; F12 is a
delivery probe, not launch proof. `PREV-010` keeps three-thumbnail horizontal
coverage and a two-thumbnail vertical fixture. It preserves the initial
ordered window-identity baseline across other-app unpin/repin transitions
and checks index-1 remapping, every-slot physical selection, exact native
activation, and thumbnail-center reachability in the native input surface.
The native preview depth tracks the QML popup extent with a 400 px minimum,
subject to the available output limit. `PREV-011`
(`test_11_preview_surface.py::test_prev011_grouped_preview_surface_tracks_two_three_and_single_layouts`)
checks whole-popup and whole-thumbnail containment.
These checks do not establish divider paint/AA, preview image/RHI content,
DRM capture, installed-package Tier 3, or KWin RPC behavior. Preview input uses
configured `PreviewHideDelay=1500`, `PreviewHoverDelay=500`, and
`MaxZoomFactor=1`; default timing remains unverified.

Without DRM, run the geometry, AT-SPI, real-input, lifecycle, and persistence
checks and record their outcomes. Report divider paint, pixel hover/zoom,
and capture-dependent preview checks separately as unavailable; do not skip
the geometry suite because ScreenShot2 fails. A headless geometry result
does not establish painted-divider quality or the root cause in the user's
original desktop environment. Tier 3 status remains pending until the same
checks run against an installed package.

## Known krema bugs

Grouped preview rows can still exceed the available output capacity. The
native preview depth is limited by the output extent; thumbnail wrapping and
scrolling are not implemented, so unusually large rows may extend beyond
the surface.
Tests marked `outputs(2)` or `outputs(3)` are intentionally skipped when the
session has a different output count because the exact number of displays is a
test precondition.
Issue #55 is covered by `QA-PREV-01`.

To pin a newly found bug, write the test for the correct behavior and mark
it `@pytest.mark.xfail(strict=True, reason="krema bug: ...")` (or put the
mark on the failing `pytest.param`), and list it here with its mechanism.
`strict=True` turns the fix into an XPASS that fails the run until the
marker is removed. Condition the mark (`KREMA_E2E_DISTRO`, a runtime
version) only when the bug shows on some stacks alone; differences in the
libraries themselves, such as Qt's AT-SPI roles and extents, are handled in
the harness (`krema_e2e.krema`), not with xfails.

## Writing tests

```python
from krema_e2e import config, env, kwin, input as inp, wait_until
from krema_e2e.krema import Krema, Rect
import pytest

@pytest.mark.kremarc({"PinnedLaunchers": [config.launcher("org.kde.kwrite")], "IconSize": 64})
def test_something(krema: Krema, apps):
    win = apps.open("Alpha")                       # fixture window, waits until KWin maps it
    krema.click_item("Alpha")                      # real pointer click on the dock item
    wait_until(lambda: kwin.active_window().pid == win.pid)
```

Rules:

* Every assertion waits for a condition with a timeout (`wait_until`,
  `wait_stable`, `krema.wait_for_item`). Never assert after a fixed sleep.
* Each test gets its own krema, XDG directories, and fixture windows; nothing
  leaks between tests. Tests run serially: they share one KWin, one pointer,
  and one keyboard.
* A dock item's accessible name is the **window title** while its task has
  one window, and the app's `.desktop` `Name` once several windows are
  grouped. A pinned launcher whose `.desktop` file is missing shows the
  desktop id (`org.kde.dolphin.desktop`).
* The default test config pins nothing (`krema_e2e.krema.DEFAULT_CONFIG`),
  so the dock only shows what the test opens. Use `@pytest.mark.kremarc({})`
  to get krema's built-in defaults.

## API contract (`krema_e2e`)

The follow-up scenario suites build on this API. All coordinates passed to
`krema_e2e.input` are global screen pixels.

### pytest fixtures and markers (`krema_e2e.pytest_plugin`, loaded by `conftest.py`)

| Name | Description |
|---|---|
| `krema` fixture | A started `Krema` with fresh XDG dirs. Stopped after the test. Collects failure artifacts. Fails the test if krema exited during it. |
| `apps` fixture | `TestWindows` manager; closes leftover windows after the test. |
| `@pytest.mark.kremarc(settings)` | kremarc written before krema starts (format below). `{}` = krema defaults. |
| `@pytest.mark.no_krema_autostart` | The `krema` fixture does not call `start()`. |
| `class KremaTest` | Optional base class: sets `self.krema`/`self.apps`; the class attribute `kremarc` applies to all its tests. |

### `krema_e2e.krema`

`Krema(home: Path, config: Mapping | None = None, name: str = "krema")` is one
krema process with `XDG_{CONFIG,DATA,CACHE,STATE}_HOME` under `home`.
`config=None` writes `DEFAULT_CONFIG`; `{}` writes nothing.

Lifecycle and config:

| Member | Description |
|---|---|
| `start(timeout=30)` | Launch krema (log: `artifacts/<name>/krema-N.log`), attach a WebDriver session to its AT-SPI tree by PID, and wait for the `Krema Dock` tool bar and the KWin dock surface. Waits for a previous instance to release `org.kde.krema` first. |
| `stop(timeout=10)` | Quit the session, SIGTERM (then SIGKILL) krema. |
| `restart(timeout=30)` | `stop()` + `start()`; XDG dirs and kremarc are kept. |
| `is_running() -> bool`, `pid`, `process`, `driver` | `driver` is the raw Appium `webdriver.Remote` (implicit wait 50 ms, one lookup pass per call). |
| `config_path`, `write_config(settings)`, `read_config() -> {group: {key: str}}` | kremarc. `write_config` takes effect on the next (re)start. |
| `environment() -> dict` | Environment krema is started with. |

AT-SPI lookup (XPath tags are role names with `_`: `tool_bar`, `button`,
`popup_menu`, `frame`, `label`; attributes `name`, `description`, `states`,
`accessibility-id`):

| Member | Description |
|---|---|
| `find(xpath) -> WebElement \| None`, `find_all(xpath) -> list`, `wait_for(xpath, timeout=10)` | Generic lookup in krema's tree. |
| `toolbar()` | The `[tool bar] "Krema Dock"`. |
| `items()`, `item_names() -> list[str]` | Dock item buttons in visual order. |
| `item(name)`, `wait_for_item(name, timeout=10)`, `wait_for_no_item(name, timeout=10)` | One dock item by accessible name. |
| `focused_item() -> str \| None` | Name of the item with the `focused` state. |
| `wait_keyboard_focus(surface="dock")` | Wait until KWin gives the surface keyboard focus (its window is KWin's active window). Call it before the first key after entering keyboard navigation: the `focused` state appears before KWin applies the layer surface's keyboard interactivity, and an earlier key goes to the previously active window. |
| `preview_popup()`, `preview_visible() -> bool`, `thumbnails() -> list` | Preview `[popup menu]` (always in the tree, 0x0 while hidden) and its thumbnail buttons (name = window title). |
| `settings() -> WebElement \| None` | The Settings window frame (`SETTINGS_XPATH`). |
| `page_source() -> str` | Whole tree as XML. |
| `has_state(element, state) -> bool` (module function) | `focused`, `showing`, `visible`, `focusable`, `sensitive`, `active`, ... |
| Constants | `TOOLBAR_XPATH`, `ITEMS_XPATH`, `PREVIEW_XPATH`, `THUMBNAILS_XPATH`, `SETTINGS_XPATH`, `DEFAULT_CONFIG`; `PAGE_ROLE` (a Settings page is `page_tab` before Qt 6.11, `panel` since) and `SETTINGS_STACK_XPATH` (the Settings page stack, matched by its page children because its own role varies by distro; Qt 6.11 adds a `filler` level), both from `env.QT_VERSION` (runtime `qVersion()`) |

Lookup cost. Every XPath lookup (`find`, `find_all`, `wait_for`, every
`item*`/`preview*`/`surface_rect` helper) makes selenium-webdriver-at-spi
build an XML tree of krema's AT-SPI tree and evaluate the XPath on it.
Upstream reads each node with about eight synchronous D-Bus round trips
(~1 ms per node). The image patches it (`tools/pipelined-tree.patch`) to
send each node's calls (`GetRole`, the `Name`, `Description` and
`AccessibleId` properties, `GetState`, `GetChildren`) asynchronously, all
in flight at once. With every attribute read, the XML is the same element
for element as upstream's (checked on the dock, the open preview, every
Settings page and an open Settings combo box). Qt's `Cache.GetItems`
returns no items, so no single call returns the tree. Anything the patch
cannot reproduce exactly (an error reply, another application's child, a
role outside libatspi's table) falls back to the upstream walk. On the host
of "Timings" above, building the full tree takes 2.6 ms instead of 12 ms
for the dock (11 nodes) and 28 ms instead of 191 ms with the Settings
window open (180 nodes). The cost still grows with the tree: pages visited
in the Settings window stay in it (up to ~380 nodes). A lookup that matches
nothing still rebuilds the tree until the 50 ms implicit wait has elapsed.

Each find (element or elements) builds the tree with only the attributes
its XPath reads. The role (the tag name), path and children are always
read. name, description, accessibility-id and states are read only when the
XPath names them after `@` or `attribute::`, and any other attribute
reference (`@*`, `@node()`, an `@` inside a string literal) reads all of
them. An XPath cannot see an attribute it does not name, so it matches the
same elements. A lookup like `//tool_bar[@name='Krema Dock']` makes three
calls per node instead of six, which halves lookups while Settings is open.
The page source always has every attribute.

Upstream checks that each match's index path still leads to the same object
by comparing names and descriptions, and returns nothing for the whole
lookup on a difference. A dock item's description changes whenever its
window gains or loses "Active" or its window count changes, so a lookup
could miss an item that existed throughout (on CI, 34 of 42 such rejections
in two full-matrix runs were the same object; with KWin switching the
active window in a loop, 70-80% of `find_all` calls returned nothing). The
pipelined build compares D-Bus object paths instead: at each step of the
walk, and when an index has shifted it finds the child by object path, so
a match is dropped only when its object has left the tree.

Geometry. On Wayland, AT-SPI rects (`element.rect`, `Rect.of(element)`) are
relative to the element's surface. The surface's screen position comes from
KWin: the krema window whose client size equals the surface's AT-SPI frame.

| Member | Description |
|---|---|
| `Rect(x, y, width, height)`, `.center`, `.contains(x, y)`, `Rect.of(element)` | |
| `painted_rect(rect, rest) -> Rect` (module function), `EXTENTS_IGNORE_SCALE` | Where a zoomed bottom-edge dock item is drawn, given its unzoomed rect `rest`. Qt < 6.9 (`EXTENTS_IGNORE_SCALE`) reports a scaled item's transformed top-left corner with its untransformed size, so a zoomed item keeps its base width there; the zoom factor is recovered from how far the corner rose above `rest`. From 6.9 on it returns `rect` unchanged. Compare zoomed widths through it, never through `Rect.of(item).width`. |
| `windows() -> list[kwin.Window]` | KWin windows of this krema (dock, preview, menus, settings). |
| `surface_rect(surface="dock") -> Rect \| None` | Screen rect of `"dock"`, `"preview"` or `"settings"`; None while unmapped. |
| `to_screen(rect, surface="dock") -> Rect`, `screen_rect(element, surface="dock") -> Rect` | Surface-local to screen coordinates. |
| `item_center(name) -> (x, y)` | Screen centre of a dock item. |
| `settled_item_center(name) -> (x, y)` | `item_center` once it has stopped moving (adding/removing an item animates the panel width and shifts its neighbours). `hover_item`, `click_item` and `scroll_item` aim with it. |

Input and UI flows:

| Member | Description |
|---|---|
| `hover_item(name, steps=5, step_ms=40)` | Glide the pointer onto an item from 40 px above the dock. For a task item this also opens its preview popup after `PreviewHoverDelay`; wait for it or let `move_away()` dismiss it before asserting a rest state. |
| `click_item(name, button="left")`, `scroll_item(name, dy=15)` | Real pointer click or wheel on an item. |
| `move_away(close_preview=True)` | Glide the pointer to the top centre, then wait for the preview popup to be dismissed (the item stays zoomed while the preview is open). `close_preview=False` only moves the pointer. |
| `open_context_menu(name) -> kwin.Window` | Right-click and wait for the QMenu popup; returns its KWin window (screen geometry). |
| `choose_context_menu_entry(label, entries)` | Keyboard selection (Down x position, Return) in the open menu. |
| `context_menu_entries(pinned, is_window, has_notifications=False) -> list[str]` (module function) | Enabled menu entries in order, mirroring `src/models/dockcontextmenu.cpp`. |
| `open_settings(via_item, entries=None) -> kwin.Window` | Open Settings through an item's context menu; returns the "Settings — Krema" window. |
| `screenshot(name, area=None) -> PIL.Image.Image` | RGB screen image, also saved to `artifacts/<name of krema>/<name>.png`. With `area` only that part is captured, the rest is black, and the PNG holds only the area (see "Screenshots and previews"). |

### `krema_e2e.windows` (fixture windows)

`krema-test-window` is a small Qt program: one process shows one window with a
chosen title and Wayland app_id. It logs each key press it receives.
Installed app ids with `.desktop` files: `env.TEST_APP_ID`
(`org.kde.krema.testwindow`, "Krema Test Window") and `env.TEST_APP2_ID`
(`org.kde.krema.testwindow2`, "Krema Second Test Window"). Any other app id
works too, e.g. `org.kde.kwrite` to pose as KWrite (kwrite and kfind
`.desktop` files and breeze icons are installed).

| Member | Description |
|---|---|
| `TestWindows.open(title, app_id=TEST_APP_ID, width=400, height=300, badge=None, accessible=False, color=None, timeout=10) -> TestWindow` | Start a window and wait until KWin maps it. `badge=N` emits a Unity `LauncherEntry` count for the app id. `accessible=True` puts its widgets on the AT-SPI bus. `color="red"` (any QColor name) fills the window content solidly, so screenshots can tell which window a preview thumbnail shows. |
| `TestWindows.close(tw)`, `close_all()`, `open_windows` | SIGTERM and wait until KWin drops the window. |
| `TestWindows.key_presses(title=None) -> [(title, qt_key, qt_modifiers)]` | Keys the fixture windows received. A combo KWin consumed as a global shortcut does not appear. |
| `TestWindow.title`, `app_id`, `pid`, `process`, `window`, `internal_id` | `window` is the KWin snapshot taken when the window appeared. |
| `TestWindow.refresh() -> kwin.Window \| None`, `is_active() -> bool` | Current KWin state. |

### `krema_e2e.input` (real input through KWin fake-input)

| Function | Description |
|---|---|
| `move(x, y, duration_ms=0)` | Move the pointer; above 50 ms the motion is interpolated. |
| `move_path(points, step_ms=50)`, `line(start, end, steps) -> points` | Stepped paths, one motion event per point. |
| `click(x=None, y=None, button="left", count=1, hold_ms=0)` | `left`, `middle`, `right`, `back`, `forward`. |
| `press(button, x=None, y=None)`, `release(button)` | Low-level; prefer `drag`. |
| `drag(start, end, steps=10, step_ms=50, hold_ms=300, button="left")` | Press, hold, move, release in one chain. |
| `scroll(x, y, dy=0, dx=0)` | Wheel; positive `dy` scrolls down, 15 = one notch. |
| `key(*names, hold_ms=0)` | Combo to the focused surface or KWin's shortcut handling: `key("Escape")`, `key("Meta", "Alt", "d")`, `key("ctrl", "a")`. Names: single characters, `meta`/`super`, `ctrl`, `alt`, `shift`, `return`/`enter`, `escape`, `tab`, `space`, `backspace`, `delete`, arrows, `home`, `end`, `pageup`, `pagedown`, `f1`-`f12`. |
| `type_text(text)` | Character by character. |
| `pointer_position()`, `run_actions(action_sets)`, `key_value(name)` | Last position set by this module, raw W3C action chains, and the name-to-W3C key map. |
| `background_actions(action_sets)` | Context manager: runs a raw chain in the background while the body runs; leaving the body ends the chain's pause marked `"interruptible": True`, then waits for the chain to finish. |

The image patches upstream inputsynth (`tools/inputsynth-fixes.patch`) so that
every `pointerMove` is absolute, which multi-move chains and drags need, so
that W3C `Meta` sends Super, which KDE calls Meta, so that a pause marked
`"interruptible": true` can be ended early (the one in progress, or the
next), and so that it has a persistent `--stdin` mode.

Every call hands its chain to one long-lived `inputsynth --stdin` process
per session, started on first use and started again if it dies. Chains are
sent over stdin as JSON lines, and inputsynth answers each one when it has
finished, so a chain costs a pipe round trip instead of a process start
(about 100 ms on CI). All chains share that process's one fake-input device.
Each chain runs on a thread of its own, so a key press sent during a drag's
pause (`test_05_drag.py`) is not held up. `background_actions` ends its
chain's interruptible pause with a request for that chain only. A chain
that exceeds its timeout kills the process and raises `TimeoutExpired`, as
before. The process exits when pytest does (stdin closes).

The patch also skips the per-point server roundtrip for zero-duration mouse
moves. Upstream sends each `pointer_motion_absolute` and roundtrips after
it, so a hover path of tens of steps paid a roundtrip per step. Now such a
move is enqueued and flushed immediately, in request order with everything
sent before it, and marked pending on the chain's worker thread. The chain
is answered only after a roundtrip has confirmed any motion still pending
on that thread, so a pure-motion chain costs one roundtrip instead of one
per point; a later action's own barrier (button press or release, key,
wheel, touch, an interpolated move's steps) confirms it first and adds
nothing. The pending flag is `thread_local`, so a concurrent chain's
barrier, its Escape key or interruptible pause end, cannot clear another
chain's unconfirmed motion. A flush that cannot write everything falls
back to the roundtrip the move used to do, so buffered requests never sit
out the pauses that follow them.
Positive-duration mouse moves keep their interpolated steps and final
roundtrip unchanged, and pen, touch, key, button, wheel and pause
behaviour are unchanged.

`drag` presses, moves and releases in one chain, so the button is held
throughout. A button held across separate calls lasts only as long as the
`--stdin` process: a crash or a timeout restarts it and drops the button. A
mid-drag check (`test_05_drag.py`) therefore holds the button in an
interruptible pause of the drag's own chain and ends it as soon as its
checks are done.

Every inputsynth process registers its own fake-input device, and KWin
advertises `wl_seat` pointer capability only while some pointer device
exists. Without one the seat loses the pointer, and Qt releases its
`wl_pointer` without a leave: the surface under the pointer stays hovered,
and if krema binds the new `wl_pointer` only after KWin has handled the next
motion, it also misses that enter or leave. When each call ran its own
inputsynth process, this left the dock hovered under load after the pointer
had moved away. The session fixture `_pointer_capability`
(`hold_pointer_capability(park)`) keeps a second inputsynth alive for the
whole session, so the pointer capability never drops, as with a real mouse,
also before the first chain and while the `--stdin` process restarts. It
parks the pointer mid-screen first.

### `krema_e2e.kwin` (oracle)

| Function | Description |
|---|---|
| `windows() -> list[Window]` | All KWin windows, including layer-shell surfaces, in stacking order. |
| `app_windows()` | Normal windows that do not skip the taskbar. |
| `active_window() -> Window \| None` | |
| `activate(internal_id)`, `set_minimized(internal_id, bool)` | Setup helpers, not assertions. |
| `cursor_pos() -> (x, y)` | KWin's pointer position. |
| `evaluate(js, timeout=10)` | Run JavaScript in KWin's scripting engine; the script calls `report(value)` once with a JSON-serializable value. |
| `screenshot(path, area=None) -> PIL.Image.Image` | RGB screen image via ScreenShot2, flattened onto black (KWin 6.7 returns RGBA with a transparent desktop, KWin 6.3 an opaque image already on black), so pixel oracles see the same image on every KWin; also saved as PNG to `path`. With `area` KWin captures only that part: the image stays screen-sized and is black elsewhere, and the PNG holds only the area (see "Screenshots and previews"). Raises `ScreenshotUnavailable` when KWin composites with QPainter. |
| `bounds(*rects) -> (x, y, width, height)` | Bounding rect of several rects, e.g. one `screenshot` area for several pixel checks. |
| `compositing_type() -> str`, `can_capture() -> bool` | `"OpenGL"` or `"QPainter"`. |

`Window` fields: `internal_id`, `title`, `app_id` (desktop file name),
`resource_class`, `pid`, `active`, `minimized`, frame geometry `x, y, width,
height` (`.geometry`), client geometry `client_x, client_y, client_width,
client_height` (`.client_geometry`), `normal_window`, `dock`,
`special_window`, `skip_taskbar`, `desktops`, `output`. Krema's layer-shell
surfaces report `normal_window=True`, `skip_taskbar=True` and no desktops.
Its QMenu popups report `normal_window=False`.

### `krema_e2e.shortcuts` (KGlobalAccel)

| Function | Description |
|---|---|
| `invoke_shortcut(name, component="krema", timeout=10)` | `invokeShortcut` over D-Bus; waits until the action is registered. Krema actions: `toggle-dock`, `focus-dock`, `activate-entry-1..9`, `new-instance-entry-1..9`. |
| `shortcut_names(component="krema")`, `shortcut_keys(action, component="krema") -> [int]` | Registered actions and their active keys (Qt combined key ints). |
| `set_shortcut_keys(action, keys, component="krema")` | Rebind; `[]` unbinds. |
| `META_ALT_D` | Qt combined key of krema's default Focus Dock shortcut, Meta+Alt+D. |

### `krema_e2e.config` (kremarc)

`write_kremarc(path, settings)` and `read_kremarc(path) -> {group: {key: raw}}`.
`settings` is either flat (`{"IconSize": 64}`, group `General`) or grouped
(`{"General": {...}, "Screen-Virtual-0": {...}}`). bool becomes
`true`/`false`, and lists are comma-joined with KConfig escaping. Helpers:
`launcher(desktop_id) -> "applications:<id>.desktop"`, `as_bool(raw)`,
`as_list(raw)`, and the constants `ALWAYS_VISIBLE`, `AUTO_HIDE`,
`DODGE_WINDOWS`, `EDGE_TOP`, `EDGE_BOTTOM`, `EDGE_LEFT`, `EDGE_RIGHT`. Key
names are the entries in `src/config/krema.kcfg`.

### `krema_e2e.waits`, `krema_e2e.dbus`, `krema_e2e.env`

* `wait_until(predicate, timeout=10, interval=0.05, message=None)` returns the
  first truthy value. Exceptions count as "not yet". On timeout it raises
  `WaitTimeout`, an `AssertionError`, with the last value.
* `wait_stable(getter, duration=0.5, timeout=10, interval=0.05)` waits until a
  value stops changing, for example after zoom animations.
* `dbus.call(service, path, iface, method, signature=None, *args)`,
  `dbus.get_property`, `dbus.has_name`, `dbus.list_names`, `dbus.name_pid`,
  and `dbus.session_bus()` for the test session bus.
* `env`: `SCREEN_WIDTH`, `SCREEN_HEIGHT`, `KREMA_BINARY`, `ARTIFACTS_DIR`,
  `WEBDRIVER_URL`, `TEST_APP_ID`, `TEST_APP_NAME`, `TEST_APP2_ID`,
  `TEST_APP2_NAME`, `QT_VERSION` (runtime Qt version as an int tuple, for
  version-conditional expectations), and
  `artifact_path(name)`.

`wait_until` polls every 50 ms by default: it returns only when the
predicate is true, so a shorter interval lowers latency and checks the same
thing. `wait_stable` defaults to a 0.5 s settle window because the window
must be longer than any pause in the transition (dock hide delay 400 ms,
attention animation pauses up to 2 s). A call may pass `duration=0.3` only
when the value comes from a hover zoom or a layout animation that starts
immediately, with no timer of 300 ms or more before its first change and no
pause inside the animation.

## Screenshots and previews (need a DRM render node)

KWin 6.7 only composites with OpenGL when it can open a DRM device. Its
virtual backend asks libdrm for a device with a render node
(`src/backends/virtual/virtual_backend.cpp`; vgem is the one exception that it
opens through its primary node). Without one it falls back to QPainter. In
QPainter mode:

* ScreenShot2 answers every request with `org.kde.KWin.ScreenShot2.Error.Cancelled`,
  because the screenshot plugin reads pixels back through OpenGL.
* Screencasting (`zkde_screencast`, which KPipeWire uses for preview
  thumbnails) is disabled. `ScreencastManager` requires `OpenGLCompositing`,
  so thumbnails show their fallback.

`kwin.can_capture()` reports the mode, and the smoke screenshot test skips
with the reason. To enable capture, give the container a vgem device:
`run-e2e.sh` passes `/dev/dri` through automatically when the host has one.

| `/dev/dri` in the container | Compositing |
|---|---|
| vgem (`sudo modprobe vgem`, or `sudo tests/appium/setup-vgem.sh`) | OpenGL (llvmpipe) |
| none | QPainter |

```sh
sudo modprobe vgem        # or: sudo tests/appium/setup-vgem.sh
tests/appium/run-e2e.sh
```

The OrbStack VM on the development Mac has no DRM driver, so capture cannot
be exercised there.

Screenshots of an area: `krema.screenshot(name, area)` and
`kwin.screenshot(path, area)` take an optional `(x, y, width, height)` in
screen coordinates (a `Rect`, or `kwin.bounds(...)` of several). KWin then
renders and reads back only that area (ScreenShot2.CaptureArea through the
screenshotter helper, patched in `tools/pipelined-tree.patch`). A dock item
costs 13 ms instead of 52 ms on fedora-43 and 29 ms instead of 180 ms on
debian-13. Both functions return a PIL image, not a path. It stays
screen-sized and black outside the area, so checks index it with screen
coordinates, and inside the area it has the same pixels as a full capture
(compared for the areas the tests use on both targets). The PNG in
`artifacts/` then holds only the area. Pass the rect the check reads. If
that rect is read only after the shot, pass the surface that contains it
(`krema.surface_rect(...)`) and keep the read after the shot. Checks that
diff or scan the whole screen, checks over the whole preview surface or the
Settings window, artifact-only shots and the failure screenshot capture the
full screen.

Thumbnail-click, close-button, and outside-close tests still call
`preview.wait_on_screen(krema, popup)` first (PREV-003, PREV-004 close
button, PREV-005), so they need capture as well. AT-SPI reports the popup
as shown before its rows finish layout; it starts at 17x38 and grows as
they arrive. The helper originally also guarded against the delayed
input-region commit described in Investigation 4. It remains useful for
stable layout and thumbnail geometry: the popup's AT-SPI rect and KWin's
preview surface must agree and stay unchanged for 0.3 s (the layout has
no timer or animation).
The helper then checks that screenshot pixels inside that final rect, where
no other KWin window covers it, are no longer the empty black desktop: the
popup has painted its opaque background at its final geometry. Callers read
their glide target after this.

`QA-PREV-01` intentionally exercises the faster AT-SPI-visible-to-pointer-entry
path. Before hovering, `preview.fast_pointer_entry` looks up the in-process
AT-SPI popup and its already mapped KWin surface. It then polls `showing`,
`visible`, and current popup extents every 5 ms without serializing the whole
webdriver tree. As soon as those extents fit inside the surface, it enters
the popup centre and sends one short in-popup motion without waiting for
painted pixels. Three fresh opens must each finish entry within 190 ms of
the first observed visibility, within the recorded visibility-to-first-frame
race window, and stay visible for another 500 ms. This does not claim
stationary-pointer recovery or that every timing race is eliminated.

## Investigations

Each result below was reproduced in this harness (KWin 6.7.5, Qt 6.10, KF 6.30).

### 1. Does fake-input pointer at the bottom screen edge trigger auto-hide show? Yes.

EIS-based remote input was earlier reported not to reach the layer-shell
trigger strip; the fake-input pointer here does. The test used
`VisibilityMode=1` (AutoHide) and one window. With the pointer away, the
dock item is hidden: AT-SPI `showing=false`, and the item rect is pushed
below the 108 px surface (`y=120`). Pointer moves had these results:

| Pointer at | Dock |
|---|---|
| y=760 (inside the surface, above the trigger strip) | stays hidden |
| y=766 or y=767 (bottom 2 px) | shown within 1.5 s (`showing=true`, `y=40`) |
| leaves again | hides |
| 10-step glide from y=700 to y=767 | shown |

Tests can therefore use real edge hover: `inp.move(x, env.SCREEN_HEIGHT - 1)`
followed by `wait_until(lambda: has_state(krema.item(name), "showing"))`.

### 2. Does a real Meta+F5 key combo trigger KGlobalAccel? Yes, but KWin's own Meta+F5 binding wins.

This needs `kwin_wayland` **without** `--no-global-shortcuts`, so that KWin
hosts kglobalaccel (`entrypoint.sh` sets `TEST_WITHOUT_GLOBAL_SHORTCUTS=0`).
It also needs inputsynth's Meta fix. Upstream maps W3C Meta to the `Meta_L`
keysym, which in the evdev keymap is `<META>` at shift level, so it sent
Shift+keycode 205, not Super.

Real key combos do reach KGlobalAccel. Alt+F4 (KWin, "Window Close") closes
the active window. After the fix, a fixture window receives Meta+x with
`Qt::MetaModifier`.

Meta+F5 still does not focus the dock. The fixture window receives only the
Meta press, not the F5 press, and the pointer jumps from (40, 40) to the
centre of the active window. KGlobalAccel lists the same key,
`0x10000000|0x01000034` (Meta+F5), for two actions:

* `kwin/MoveMouseToFocus` ("Move Mouse to Focus", a KWin default)
* `krema/focus-dock`

KWin's action handles the key. After
`shortcuts.set_shortcut_keys("MoveMouseToFocus", [], component="kwin")`,
the real Meta+F5 gives a dock button the `focused` state.

**Product finding (resolved):** Krema's former default Focus Dock shortcut
conflicted with this stock KWin default. On an unmodified Plasma session,
Meta+F5 moved the mouse instead of focusing the dock, and kglobalacceld < 6.7
dropped the contested key from krema entirely. The default is now
Meta+Alt+D, which no stock Plasma component claims; setups still on Meta+F5
are moved to it. `test_kbd001_meta_alt_d_focuses_first_dock_item` presses
the real combo. `invokeShortcut("focus-dock")` over D-Bus always works.

### 3. Is the native QMenu context menu in AT-SPI with a proper registry? No, by Qt design.

The registry in this setup works: `at-spi2-registryd` runs on the session's
a11y bus, and the webdriver, krema and fixture windows all see each other.
After a right-click, KWin maps a new krema window: the popup, with
`normal_window=False`, 128x183 at the cursor. A walk of the whole desktop
tree finds no menu, `Pin to Dock` or `Quit` entries. `QAccessibleApplication`
builds its children from `topLevelWindows()` and skips `Qt::Popup` windows,
so a QMenu is never a child of the application. This is Qt behavior, not a
bus problem.

How to use the menu anyway, as implemented in `Krema`:

* `open_context_menu(name)` returns the popup's KWin window with its screen
  geometry.
* The popup takes the keyboard grab, so Down/Return navigation works
  (`choose_context_menu_entry`). The disabled app-name header and separators
  are skipped, and the order of enabled entries comes from
  `context_menu_entries()`. Choosing "Close" this way closed the window.
  Choosing "Settings..." opened "Settings — Krema", whose AT-SPI frame
  (`name="Settings"`) matches the KWin client geometry.

### 4. Historical issue #55: AT-SPI visibility preceded the input-region commit.

In the pre-fix investigation, `PreviewController::doShow()` set the popup's
input region through `updateInputRegion()` → `QWindow::setMask()`
(`src/shell/previewcontroller.cpp`). Qt's Wayland backend sent
`wl_surface.set_input_region` without a commit. This double-buffered state
applied with the preview surface's next commit: the first frame that drew
the popup. On the first open after a krema start that commit came 100-190 ms
after `set_input_region`
(`WAYLAND_DEBUG=client`). AT-SPI showed the popup at once. A pointer
flicked onto the popup in between received no `wl_pointer.enter`. KWin did
not send one when the region later appeared under the resting pointer, so
the dock's 200 ms hide timer closed the preview
(`Hide timer fired, previewHovered: false`). Flicks 10-40 ms after AT-SPI
showed the popup closed it in 12 of 22 runs; flicks 200 ms or more after
never did. Screenshot-dependent PREV tests therefore continue to wait for
the drawn popup with `preview.wait_on_screen`; `QA-PREV-01` intentionally
exercises the earlier AT-SPI-visible-to-pointer-entry path by deriving a
current AT-SPI/KWin coordinate and entering it without a pixel wait.

## Preview input-region regression (issue #55)

`QA-PREV-01` covers the approved fast AT-SPI-visible-to-pointer-entry path:
the exact popup-only input region must take the pointer before the existing
hide delay expires. It does not widen that region, change the delay, or test
stationary-pointer recovery. `preview.wait_on_screen` remains in the other
PREV tests for layout and paint stability, not as a substitute for this path.
The recorded pre-fix run failed after a 33 ms entry; the fixed run passed three
fresh opens in one run. The test does not claim that every timing race is gone.

The Focus Dock default in `src/app/application.cpp` (`focusDockAction`) is
Meta+Alt+D, clear of KWin's "Move Mouse to Focus" (Meta+F5).
