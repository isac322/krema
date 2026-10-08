# QML component tests (Tier 1)

Qt Quick Test suites that load Krema's **real** QML files from `src/qml`
(`main.qml`, `DockItem.qml`, `PreviewPopup.qml`, `PreviewThumbnail.qml`) and
check their behavior headless (`QT_QPA_PLATFORM=offscreen`, software scene
graph). No compositor, D-Bus, PipeWire, or KWin is needed.

## Run

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build --target krema_qml_tests
ctest --test-dir build -R qml --output-on-failure
```

Each `tst_*.qml` file is its own ctest entry (`qml_tst_*`, label `qml`). To
run a single test function directly:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
  build/bin/krema_qml_tests -input tests/qml/tst_dockitem_zoom.qml test_peakAtHoveredItemEqualsMaxZoom
```

The tests reference `src/qml` by relative path (`../../src/qml`), so run them
from the source tree. They must not be copied into the build directory.

## Suites

| File | Component under test | Covers |
|---|---|---|
| `tst_dockitem_zoom.qml` | `DockItem.qml` + `ZoomAnimationProfile.qml` | Hover zoom fed by the production zoom layout (`DockView.zoomLayout`): the peak equals `maxZoomFactor` at the hovered item, falloff is symmetric and monotonic, neighbours are attenuated, zoom stays at 1.0 outside the panel and beyond the Gaussian range, sigma scales with icon size, the max-zoom setting applies live, zoom eases in and settles back to the rest layout for both styles, zoom animation presets resolve their zoom-in/zoom-out duration and easing (Natural eases in slower than Quick, Instant snaps), custom zoom-in and zoom-out durations follow their own timelines (a 0 ms zoom-in snaps while zoom-out still animates), In place tracks the pointer along the dock without lag, the Scale and Translate transforms match `currentScale`/`currentOffset`, Parabolic pushes neighbours aside with constant gaps and keeps the hovered icon under the pointer, In place scales around fixed centres with overlap, and each edge grows away from the screen edge |
| `tst_dockitem_geometry.qml` | `DockItem.qml` | Item size from `iconSize` per edge (including the indicator reserve), live resize, icon `sourceSize` covering max zoom, icon provider URL and cache-busting, placeholder initial, icon opacity for active/minimized/background/drag-source windows, dimming of other virtual desktops |
| `tst_dockitem_indicators.qml` | `DockItem.qml` | Running dots driven by `IsWindow`/`ChildCount` (capped at 3), with live role updates and active/minimized styling; the badge shows the unread count (`99+` overflow) and follows the number/dot/off modes, scales with icon size, gives the Unity LauncherEntry count priority (per launcher URL, kept during Do Not Disturb), and clears when the window is activated; LauncherEntry progress bar; attention triggers (window, badge increase, SNI, LauncherEntry urgent), the disable setting, DND suppression, auto-stop after the configured duration, and blink restoring icon opacity; accessible description |
| `tst_dock_main.qml` | `main.qml` | One delegate per task (insert and remove), layout from `iconSize` and `iconSpacing` (with live updates), the panel hugging its content and centering, vertical docks, pointer hover to zoom through the real hit testing, Parabolic hover growing the reported background evenly and pushing neighbours aside (In place: background and positions unchanged), zoom settling when the pointer leaves, hover reporting, launcher tooltip versus window preview, `previewEnabled=false`, left/middle/right click and wheel dispatch, launch bounce (`taskLaunching`, `IsStartup`), keyboard navigation (arrows, clamping, Return, Escape, vertical axis, preview open), `computeDropIndex`, `isDesktopFileUrl`, and press-hold drag reorder that does not also fire a click |
| `tst_preview_popup.qml` | `PreviewPopup.qml` + `PreviewThumbnail.qml` | Visibility follows the controller; single-window versus grouped thumbnails (titles, window IDs, active/minimized); a lagging `WinIdList` limits the thumbnail count; incremental updates keep existing delegates (and so their PipeWire streams); app switches rebuild the thumbnails; a removed parent hides the popup; thumbnail click activates the right window and close closes it; thumbnail size follows the setting; the size is reported to the controller; keyboard focus ring; placement per edge; hover keeps the preview open and cancels keyboard navigation |

## How the C++ backends are replaced

Production code exposes C++ objects as `com.bhyoo.krema` singletons
(`DockModel`, `DockSettings`, `NotificationTracker`, `LauncherEntryTracker`)
and as per-engine context properties (`DockView`, `DockActions`,
`DockContextMenu`, `DockVisibility`, `PreviewController`). `main.cpp`
(`QUICK_TEST_MAIN_WITH_SETUP`) prepends `mocks/` to the QML import path, so
the same identifiers resolve to test doubles:

- `mocks/com/bhyoo/krema/*.qml`: QML singletons with the properties and
  methods the QML uses. Defaults mirror `src/config/krema.kcfg`. Action
  methods record `{ name, args }` into `calls`, which you can query with
  `callsTo(name)`. `KremaMocks.resetAll()` restores every mock and runs in each
  `init()`.
- `DockModel.tasksModel` is a C++ `MockTasksModel` (a `QStandardItemModel`
  with TaskManager role names). Tests populate it with
  `addTask({ display, IsWindow, ChildCount, IsActive, AppId, WinIdList, ... })`,
  `addChildTask(row, {...})`, `setTaskData(row, role, value)`, and
  `removeTask(row)`. `DockModel.iconName()`, `appId()`, `isPinned()`
  (`IsLauncher`), and `isOnCurrentDesktop()` read from those rows.
- `DockView.zoomLayout()` is not a copy: it calls the C++ `ZoomLayoutEngine`
  singleton (`krema.test`), which runs the production
  `krema::computeDockZoom` (`src/utils/zoomcalculator.h`) exactly as
  `DockView::zoomLayout` does. `DockItemRow.qml` feeds DockItem through the
  same pipeline as `main.qml` (zoom cursor, `zoomAmount` eased by
  `ZoomAnimationProfile` on entering and leaving, for both zoom styles).
- `LauncherEntryTracker` (Unity LauncherEntry state) is keyed by the task's
  `LauncherUrlWithoutIcon` role; tests set state with
  `LauncherEntryTracker.setEntry(url, { count, countVisible, urgent, progress, progressVisible })`.
- `org.kde.taskmanager` (`AbstractTasksModel` role enum,
  `ScreencastingRequest`) and `org.kde.pipewire` (`PipeWireSourceItem`) are
  C++ mocks registered in `qmltestmocks.cpp`. Type-less `qmldir` files under
  `mocks/` shadow the installed plugins.
- `i18n()` comes from the real `KLocalization::setupLocalizedContext`, as in
  `DockView`.

`TestCase` is an invisible item. Each file therefore has a visible root `Item`
with a `stage` child that fixtures are parented to. Without it, effective
visibility, Row/Flow layout, and pointer delivery would not work.

Internal items have no `objectName`, so `TestUtils.js` finds them by the
properties they expose (for example, the indicator `Flow` or the badge's
`Text.Fit` label).

## Conventions

- Assertions use `tryCompare`/`tryVerify`, not fixed sleeps. Timers under test
  (hover delay, launch safety net, attention duration) are shortened through
  settings where the QML allows it.
- A test only runs QML from `src/qml` plus the mocks. Do not copy application
  QML into this directory.
- Add a new suite by creating `tst_<name>.qml` and listing it in
  `KREMA_QML_TEST_FILES` in `CMakeLists.txt`.

## Not covered here

- Real rendering: icons (the `image://icon` provider is absent),
  shaders/`MultiEffect` (software backend), and PipeWire frames. Tier 2
  (`tests/appium`, a real KWin session) covers these.
- `SettingsDialog.qml` and `settings/*.qml` (Kirigami Addons
  `ConfigurationView`).
- The external file drop in `main.qml` (`DropArea` with URLs). Qt Quick Test
  has no API to synthesize a drag carrying MIME data.
