# Context Menu

## Features

- ctx-clear-notifications: Clear watcher unread notifications for the selected app
  without touching another app or a visible Unity LauncherEntry count
- ctx-pin-unpin: Toggle pin/unpin app in dock
- ctx-new-instance: Launch new instance from menu
- ctx-close: Close all windows of the app
- ctx-settings: Open settings dialog from menu
- ctx-quit: Quit Krema from the empty-area dock menu only
- ctx-app-name: App name shown as header in menu
- ctx-task-zones: Pin and unpin live tasks between zones without duplicate dock items
- ctx-desktop-actions: The app's .desktop file actions (e.g. "New Private Window") launch from the menu
- ctx-hover-hold: Hovered item keeps its zoom and its window preview stays closed while the menu is open

## Affected Files
- src/models/dockcontextmenu.h
- src/models/dockcontextmenu.cpp
- src/models/dockactions.h
- src/models/dockactions.cpp
- src/models/desktopactions.h
- src/models/desktopactions.cpp
- src/qml/main.qml
- src/shell/settingswindow.h
- src/shell/settingswindow.cpp
- src/qml/SettingsDialog.qml
- src/config/krema.kcfg
- src/models/dockmodel.h
- src/models/dockmodel.cpp
- src/shell/previewcontroller.h
- src/shell/previewcontroller.cpp
- src/platform/pointerfocustracker.h
- src/platform/pointerfocustracker.cpp

**Tier:** Tier 2 (Appium/KWin). The left-click policy settings do not change
right-click menu contents, entry ordering, Pin/Unpin, New Instance, Close,
Settings, About, or Quit behavior.


---

## TC CTX-001: Right-Click Opens Context Menu

**Precondition:** Dock visible with a running app.
**Note:** The native QMenu is not in the AT-SPI tree (README pattern 7). Choose
entries with the keyboard (Down/Return) or by position on a screenshot.
**Steps:**
1. Show dock and move the pointer to the target item (README "Showing a hidden dock for mouse tests")
2. Right-click the dock item
3. Wait 300ms
4. Screenshot — verify context menu visible

**Expected:**
- Native KDE context menu appears (QMenu / Breeze styled)
- Menu header shows app name
- Menu items (top to bottom): AppName, [the app's .desktop actions, then a
  separator — only when the app declares visible actions, see CTX-008],
  Pin/Unpin, New Instance, Close, separator, Settings..., About Krema
- Neither "Quit" nor "Quit Krema" appears in an application's menu

**Menu item positions** (approximate y-offsets from menu top):
- AppName: +0px
- Pin to Dock / Unpin: +25px
- New Instance: +57px
- Close: +90px
- Settings...: +123px
- About Krema: +158px

**Verification:** screenshot only (menu visible with expected entries)
**Automated:** tests/appium/test_04_context_menu.py::test_ctx001_right_click_opens_native_menu_at_the_item, tests/appium/test_04_context_menu.py::test_ctx001_about_krema_is_the_fifth_entry (entry order is verified by effect; QMenu is outside AT-SPI). tests/integration/test_settings_lifecycle.cpp [context-menu] also checks the native task menu contains neither quit label.

---

## TC CTX-010: Empty Dock Space Opens Dock Actions

**Precondition:** Dock visible; repeat with a running app and an empty dock.
**Steps:**
1. Right-click empty dock padding (or the separator between pinned and running apps).
2. Verify the native menu lists Settings..., About Krema, separator, Quit Krema.
3. Dismiss with Escape; verify the dock resumes its normal hover/visibility behavior.
4. Reopen and select Settings...; verify the settings window opens.
5. Reopen and select Quit Krema.

**Expected:**
- Empty space opens dock actions, even without any app icons.
- The menu keeps the dock visible until dismissed, just like a task menu.
- Quit Krema exits the dock cleanly and leaves application windows untouched.
- App icons still open their own menus, with no quit action.
- Pointer routing works at all four edges, including after keyboard navigation.

**Verification:** Native menu labels, settings window, dock exit status, remaining app windows.
**Automated:** tests/appium/test_04_context_menu.py::test_ctx010_quit_krema_is_in_the_dock_menu (empty/running-app variants); tests/integration/test_settings_lifecycle.cpp [context-menu] (native labels, task/background pointer routing, interaction lock); tests/qml/tst_dock_main.qml::test_rightClickRoutesToTaskOrDock (four edges, keyboard selection), ::test_emptyDockRightClick. Docker E2E and visual evidence remain pending until run/captured.

---

## TC CTX-002: Pin/Unpin Toggle

**Precondition:** Running app that is NOT pinned.
**Steps:**
1. Right-click on the unpinned running app's dock item
2. Click "Pin to Dock" menu item
3. Wait 300ms
4. Close the app (all windows)
5. Wait 1000ms
6. `screenshot` — verify icon remains in dock (pinned, no indicator dot)

**Expected:**
- After pinning: icon stays in dock even when app is closed
- No indicator dot (app not running)
- Pin state persists (saved to config)

**Verification:** screenshot (icon present without indicator), config file check
**Automated:** tests/appium/test_04_context_menu.py::test_ctx002_pin_keeps_the_app_in_the_dock_after_it_closes

---

## TC CTX-003: Unpin Removes Closed App

**Precondition:** Pinned app that is NOT running.
**Steps:**
1. Right-click on the pinned (not running) app
2. Click "Unpin from Dock"
3. Wait 300ms
4. `screenshot` — verify icon removed from dock

**Expected:**
- Icon disappears from dock immediately
- If app was running, icon stays until app closes

**Verification:** screenshot (icon gone)
**Automated:** tests/appium/test_04_context_menu.py::test_ctx003_unpin_removes_a_closed_app, tests/appium/test_04_context_menu.py::test_ctx003_unpinned_running_app_stays_until_it_closes

---

## TC CTX-004: New Instance Launch

**Precondition:** App already running (e.g., kcalc).
**Steps:**
1. Count the app's windows in the window list
2. Right-click the app's dock item
3. Click "New Instance"
4. Wait 2000ms
5. Check the window list — window count increased

**Expected:**
- New app window launched
- Bounce animation on dock icon

**Verification:** window list (count +1)
**Automated:** tests/appium/test_04_context_menu.py::test_ctx004_new_instance_launches_another_window

---

## TC CTX-005: Close All Windows

**Precondition:** App with 2+ open windows.
**Steps:**
1. Note the app's windows in the window list
2. Right-click the app's dock item
3. Click "Close"
4. Wait 1000ms
5. Check the window list — all windows of that app are closed

**Expected:**
- All windows of the app are closed
- If app was pinned, icon remains (no indicator)
- If app was not pinned, icon removed

**Verification:** window list (no windows for that app)
**Automated:** tests/appium/test_04_context_menu.py::test_ctx005_close_closes_every_window_of_an_unpinned_app, tests/appium/test_04_context_menu.py::test_ctx005_close_keeps_a_pinned_app_without_indicator

---

## TC CTX-006: Settings Opens Dialog

**Precondition:** Dock visible.
**Steps:**
1. Right-click on any dock item (CTX-001 steps)
2. Choose "Settings..." (keyboard, or click its position on a screenshot)
3. Wait 1500ms (settings window creation)
4. Check the window list — a krema settings window appears
5. Screenshot — verify settings dialog
6. Check the AT-SPI tree for the "Icon size" control

**Expected:**
- Settings window titled "Settings" opens as separate window, on the Icons page
- Sidebar pages: Icons, Layout & Position, Panel Style, Shadow, Animations & Badges, Behavior, Monitors & Desktops, Window Preview, About Krema, About KDE
- FormCard-based layout with sliders, card pickers, switches, spin boxes and combo boxes
- Settings UI is fully accessible via AT-SPI

**Verification:** window list (krema window count +1), AT-SPI (FormCard widgets), screenshot (dialog layout)

**Automated:** tests/appium/test_04_context_menu.py::test_ctx006_settings_entry_opens_the_settings_window

---

## TC CTX-007: Pin and Unpin Reconciles the Task Zone

**Precondition:** Enable `Separate pinned and running apps`. Start one unpinned running app and keep at least one pinned running app in the dock.

**Steps:**
1. Record the ordered dock buttons and identify the pinned/running boundary.
2. Right-click the unpinned app and choose `Pin to Dock`.
3. Wait for the dock to settle, then inspect the ordered buttons and the app's accessible description.
4. Right-click the same running app and choose `Unpin from Dock`.
5. Wait for the dock to settle, then close all windows of that app and inspect the dock again.

**Expected:**
- Pinning the running app moves its single existing button into the pinned zone; it does not add a second button or change the app's running state.
- Unpinning moves that same button into the unpinned running zone while the window remains open.
- Closing the now-unpinned app removes its button; no stale pinned launcher or duplicate task remains.
- The divider and every neighboring task retain a stable order after each reconciliation.

**Verification:** AT-SPI ordered button names/descriptions and KWin window state after Pin, Unpin, and Close; persisted pin membership in the configuration.
**Automated (native Tier 2 coverage):** `tests/appium/test_09_task_zones.py::test_tzone003_grouped_instances_keep_one_pinned_slot_and_close_differently`, `tests/appium/test_09_task_zones.py::test_tzone004_pin_and_unpin_use_real_context_menu_and_preserve_membership` (no DRM or painted-divider claim)

---

## TC CTX-008: Desktop File Actions

**Precondition:** An app whose .desktop file declares actions
(`Actions=new-draft;hidden;_SEPARATOR_;open-inbox;`, where `hidden` has
`NoDisplay=true`, `new-draft` has an icon and `open-inbox` has none, and the
`[Desktop Action …]` groups are written in another order), installed in
`~/.local/share/applications` before Krema starts. Each action launches the
app with a distinct window title.
**Steps:**
1. Pin the app (kremarc `PinnedLaunchers`) with no window running
2. Right-click its dock item — screenshot the menu
3. Choose "Open Inbox"
4. Check the window list — one new window of the app, titled by that action
5. Right-click the now-running item and choose "Unpin from Dock"
6. Unpinned, open two windows of the app (one grouped item), right-click the
   item and choose "New Draft"
7. Check the window list — one more window, titled by that action; the item
   reports 3 windows

**Expected:**
- Below the app-name header: "New Draft" (with its icon), a separator,
  "Open Inbox", then a separator before Pin/Unpin; the existing entries keep
  their order below
- Labels are the localized `Name` of each action; hidden (`NoDisplay`)
  actions are not shown; the order is the `Actions=` key's
- Choosing an action launches exactly that action's command (window title),
  for a pinned launcher, a running window and a grouped app alike
- Pin/Unpin and the other existing entries keep working after the actions
- An app without actions (the CTX-001..006 fixture) shows the unchanged menu
- A failed launch is reported as a KDE notification; Krema keeps running

**Verification:** window list (new window's app id and title), kremarc
(pinned membership), AT-SPI (item name and window count), screenshot (labels and icons)
**Automated:** tests/appium/test_04_context_menu.py::test_ctx008_pinned_launcher_launches_the_chosen_desktop_action, tests/appium/test_04_context_menu.py::test_ctx008_grouped_windows_launch_the_chosen_desktop_action (order and NoDisplay are verified by effect: a wrong position launches a window with another title; labels, icons and the launch-error notification are screenshot/manual only). Resolution (Actions= order, NoDisplay, separators, localized names, applications:/local/absolute-path desktop files) is unit-tested in tests/unit/test_desktop_actions.cpp.

## TC CTX-013: Clear Notifications Is App-Scoped

**Precondition:** The real integration harness has pinned Konsole and Dolphin
launchers. Seed one watcher-unread notification for each app and a visible
Unity LauncherEntry count for Konsole.
**Steps:**
1. Open Konsole's native context menu and inspect its actions.
2. Trigger `Clear Notifications`.
3. Read both watcher unread counts and the Unity count.
4. Reopen Konsole's menu, then open Dolphin's menu.

**Expected:**
- `Clear Notifications` is present for an app with watcher unread and absent
  after that app's watcher unread is cleared.
- Triggering the action clears Konsole's watcher unread count only; Dolphin's
  watcher unread count remains and the visible Unity count remains unchanged.
- Dolphin's menu still offers `Clear Notifications` while Dolphin has watcher
  unread.

**Tier:** Tier 2 (KWin/native QMenu integration).
**Verification:** real `QMenu::actions()` lookup and `QAction::trigger()`;
watcher unread counts and Unity LauncherEntry count.
**Automated:** `tests/integration/test_launcher_entry.cpp` —
`Context menu clears only watcher unread notifications`.

---

## TC CTX-009: Menu Holds the Hovered Item's Zoom and Preview

**Precondition:** `MaxZoomFactor=1.6`, window preview enabled with
`PreviewHoverDelay=300`, seven pinned launchers; the middle one has an open
window, so it is a task with a window preview.
**Steps:**
1. Hover the middle item and wait until its window preview is open; measure
   the item's magnified width.
2. Right-click without moving the pointer and wait for the menu.
3. Watch the item and the preview for about 1.5 s while the menu is open.
4. Press Escape without moving the pointer.
5. Repeat steps 1-2, then click "Settings..." in the menu with the pointer, so
   the menu closes with the pointer off the dock.

**Expected:**
- The preview closes within about 1 s of the menu opening and does not come
  back, and no tooltip appears, while the menu is open.
- The hovered item stays magnified (within 2 px of its hover width) for as long
  as the menu is open.
- After Escape with the pointer still on the item, the item stays magnified and
  its preview reopens after the normal hover delay.
- After the menu closes with the pointer off the dock, every item returns to
  its rest size and the preview stays closed, like a normal pointer exit.
- Variant: Krema starts while no mouse is connected (the seat has no pointer)
  and the mouse is connected afterwards. Already the first menu closed with
  Escape over a hovered item keeps its zoom, like the second one.

**Verification:** KWin (menu window), AT-SPI (preview `showing`, painted item
widths through `painted_rect`), screenshot (menu entry highlight), krema's
`WAYLAND_DEBUG` log (`wl_seat.capabilities` without pointer at start)
**Automated:** tests/appium/test_04_context_menu.py::test_ctx009_zoom_and_hover_hold_while_menu_is_open, tests/appium/test_04_context_menu.py::test_ctx009_zoom_releases_when_menu_closes_away_from_the_dock (skip without ScreenShot2 capture; the tooltip is not asserted), tests/appium/test_04_context_menu.py::test_ctx009_zoom_holds_on_the_first_menu_after_starting_without_a_mouse (variant)
