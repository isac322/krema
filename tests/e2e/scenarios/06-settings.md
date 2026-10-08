# Settings UI

## Features
- settings-appearance: Icon size, icon scale, icon opacity (active, inactive/launcher, minimized), zoom factor, zoom style, zoom animation duration, spacing, opacity, background style
- settings-click-actions: Independent single and grouped left-click choices
- settings-click-persistence: Six policy pairs apply live, save, and restore
- settings-behavior: Visibility mode, dock position, monitor mode, selected monitor switches and temporary fallback
- settings-reserve-screen-space: Independent Always Visible reservation switch applies to maximized windows live and persists
- settings-separate-launchers: Optional pinned/running sections apply live and persist without duplicating running pinned apps
- settings-preview: Preview enable/disable, thumbnail size
- settings-persist: Settings saved to KConfig and restored on restart
- settings-live-preview: Changes apply in real-time without restart
- settings-tint-color: Custom tint color selection
- settings-background-style: Background style selection (Panel Inherit, Transparent, Tinted, Acrylic)

## Affected Files
- src/qml/settings/AppearancePage.qml
- src/qml/settings/BehaviorPage.qml
- src/qml/settings/PreviewPage.qml
- src/qml/SettingsDialog.qml
- src/qml/main.qml
- src/app/application.cpp
- src/models/dockactions.h
- src/models/dockactions.cpp
- src/qml/DockItem.qml
- src/models/dockmodel.h
- src/models/dockmodel.cpp
- src/shell/settingswindow.h
- src/shell/settingswindow.cpp
- src/shell/dockshell.cpp
- src/shell/dockview.h
- src/shell/dockview.cpp
- src/shell/multidockmanager.h
- src/shell/multidockmanager.cpp
- src/shell/outputordermonitor.h
- src/shell/outputordermonitor.cpp
- src/models/taskiconprovider.h
- src/models/taskiconprovider.cpp
- src/config/krema.kcfg
- src/config/krema.kcfgc

**Tier:** Tier 2 (Appium) for real settings controls, persistence, and KWin
effects. Tier 1 QML does not replace these FormCard and multi-dock checks.
Tier 3 reuses the Appium cases against installed packages.
The existing context-menu Settings entry, keyboard paths, and multi-monitor
lifecycle remain covered by their original cases. Click-policy, reservation,
and task-section rows check their real controls and consumer effects.


---

## TC SET-001: Open Settings Dialog

**Precondition:** Dock visible.
**Steps:**
1. Right-click a dock item → context menu appears
2. Choose "Settings..."
3. Wait 1500ms (settings window creation)
4. Check the window list — krema window count increased
5. Check the AT-SPI tree for the "Icon size" control
6. Screenshot — verify settings dialog
7. Right-click the dock again → "Settings..." while the dialog is open
8. Check the window list — there is still exactly one settings window (it is raised, not duplicated). Run this on the oldest supported kirigami-addons (1.7.0, Debian 13 / Ubuntu 25.04) as well

**Expected:**
- Settings dialog opens as separate window ("Settings — Krema" title, localized)
- Left sidebar: Appearance, Behavior, Window Preview, About Krema, About KDE
- Appearance page shown by default
- FormCard layout with spinboxes, sliders, comboboxes
- All controls accessible via AT-SPI (labels, sliders with Increase/Decrease)
- Choosing "Settings..." again raises the same window; the dock stays shown while it is open

**Verification:** window list (window count +1), AT-SPI (FormCard widgets), screenshot
**Automated:** tests/appium/test_06_settings.py::test_set001_settings_opens_once_with_formcard_controls_and_keeps_dock_shown (English session: title "Settings — Krema"; the harness image is Fedora 43 with kirigami-addons ≥ 1.8, so the kirigami-addons 1.7.0 / Debian 13 / Ubuntu 25.04 run is out of this harness's scope)

---

## TC SET-002: Change Icon Size (Live Preview)

**Precondition:** Settings dialog open, Appearance page active.
**Steps:**
1. Screenshot of dock — capture baseline icon size
2. In settings, find the "Icon size" spin box
3. Change icon size (e.g., increase it)
4. Wait 300ms
5. Screenshot of dock — verify icon size changed

**Expected:**
- Dock icons resize in real-time as the value changes
- No restart required
- Zoom proportions adjust accordingly (the Parabolic zoom style keeps icons separated while scaling)

**Verification:** screenshot comparison (icon size changed)
**Automated:** tests/appium/test_06_settings.py::test_set002_icon_size_spinbox_resizes_dock_live_and_keeps_zoom_proportion (the Icon size control is a spin box: real keyboard Up, dock item width checked after every step)

---

## TC SET-003: Change Visibility Mode

**Precondition:** Settings dialog open, Behavior page.
**Steps:**
1. Current visibility mode: AlwaysVisible
2. Select "Auto hide" in the "Visibility mode" combo
3. Wait 500ms
4. Move mouse to center of screen (away from dock)
5. Wait for hide timer
6. Screenshot — verify dock is hidden

**Expected:**
- Dock hides when mouse moves away
- Visibility mode change applies immediately
- Setting persists in KConfig

**Verification:** screenshot (dock hidden)
**Automated:** tests/appium/test_06_settings.py::test_set003_auto_hide_applies_immediately_and_persists (the open Settings dialog holds the dock shown by design, see SET-008, so hiding is checked after the dialog is closed with Alt+F4, in the same krema process)

---

## TC SET-004: Change Background Style

**Precondition:** Settings dialog open, Appearance page.
**Steps:**
1. Screenshot of dock — capture current background
2. Change background style from current to "Acrylic"
3. Wait 500ms
4. Screenshot of dock — verify background changed

**Expected:**
- Dock background changes to acrylic/blur effect
- Change applies in real-time

**Verification:** screenshot comparison (background style changed)
**Automated:** tests/appium/test_06_settings.py::test_set004_acrylic_background_applies_live (pixel oracle: the Panel Inherit panel is flat, the Acrylic panel shows the acrylic shader's per-pixel noise grain over the same black background)

---

## TC SET-005: Settings Persist After Restart

**Precondition:** Changed multiple settings (icon size, visibility mode, background style).
**Steps:**
1. Note current settings values
2. Close settings dialog
3. Restart krema
4. Wait 2000ms
5. Open settings dialog
6. Verify all settings match previously set values

**Expected:**
- All settings restored from KConfig
- Dock appearance matches the saved settings

**Verification:** AT-SPI (control values), screenshot (visual match)
**Automated:** tests/appium/test_06_settings.py::test_set005_changed_settings_persist_across_restart

---

## TC SET-006: Dock Position Change

**Precondition:** Settings dialog open, Behavior page. Dock currently at Bottom.
**Steps:**
1. Select "Top" in the "Screen edge" combo
2. Wait 500ms
3. Screenshot — verify dock moved to top of screen

**Expected:**
- Dock repositions to top edge of screen
- Layer-shell anchor updates correctly
- All items render correctly in new position

**Verification:** screenshot (dock at top)
**Automated:** tests/appium/test_06_settings.py::test_set006_screen_edge_top_moves_dock_to_top ("Screen edge" row on the Behavior page)

---

## TC SET-007: Tint Color Selection

**Precondition:** Settings dialog open, Appearance page. Background style set to "Tinted".
**Steps:**
1. Find tint color button (Accessible.name contains "Tint color")
2. Click to open color dialog
3. Select a different color
4. Confirm selection
5. Screenshot — verify dock background uses new tint color

**Expected:**
- Dock background tint color changes to selected color
- Color saved to KConfig

**Verification:** screenshot (tint color changed)
**Automated:** tests/appium/test_06_settings.py::test_set007_custom_tint_color_is_applied_and_saved ("Use system color" must be switched off first for the Tint color button to appear; the colour is entered in the dialog's Hex field)

---

## TC SET-008: Change Monitor Mode From Settings

**Precondition:** Two outputs, monitor mode "Primary monitor only", settings dialog open, Behavior page. Visibility mode "Auto hide".
**Steps:**
1. Select "All monitors" in "Monitor mode"
2. Wait 500ms
3. Check the window list — Krema is still running and one dock window exists per output
4. Screenshot — the settings dialog is still open and docks on both outputs are shown
5. Right-click the dock on the second output → "Settings..."
6. Check the window list — there is still exactly one settings window
7. Select "Primary monitor only", then close the settings dialog
8. Move the mouse away from the dock and wait for the hide delay

**Expected:**
- Krema does not crash (issue #16); the mode is applied immediately
- The same settings dialog stays open across the change
- Docks created by the change stay visible while the dialog is open and auto-hide after it closes
- All docks open the same settings dialog
- Each output shows its own dock (not two docks stacked on the primary output)
- Reopening Settings after switching back to "Primary monitor only" works
- In "Follow active screen" mode with the mouse trigger, opening Settings does not move the dock to another screen (an open dialog, context menu, preview, drag or keyboard navigation holds the dock on its screen)
- In "Follow active screen" mode with the mouse trigger (dialog closed), moving the pointer to the dock edge of another output moves the dock there
- The Toggle Dock / Focus Dock / Meta+N shortcuts act on the currently shown dock, not the hidden primary-screen dock

**Automated:** `tests/integration/test_settings_lifecycle.cpp` (ctest `krema_integration_tests`)

**Verification:** window list (window counts), screenshot (dialog + docks)
**Automated (E2E, run with `KREMA_E2E_OUTPUT_COUNT=2 tests/appium/run-e2e.sh test_06_settings.py`):** tests/appium/test_06_settings.py::test_set008_monitor_mode_all_monitors_from_open_settings, ::test_set008_follow_active_mouse_opening_settings_keeps_dock_on_its_screen, ::test_set008_follow_active_shortcuts_act_on_the_shown_dock (Toggle Dock and Focus Dock; the dock is moved with the Focus trigger), ::test_set008_follow_active_mouse_trigger_moves_dock_to_the_pointer_screen (pointer at the second output's bottom edge). Meta+N is not checked end to end: activating entry N has no per-dock observable result and remains covered by the integration test.

---

## TC SET-009: Quit While Settings Is Open

**Precondition:** Dock visible. Run on Debian 13 or Ubuntu 25.04 as well (Qt 6.8, KF 6.13).
**Steps:**
1. Right-click the dock → "Settings...", then immediately right-click the dock → "Quit"
2. Start Krema again, open "Settings...", wait until the window is drawn, then right-click the dock → "Quit"

**Expected:**
- Krema exits normally both times (exit status 0, no crash report)
- No Settings window remains after exit

**Verification:** process exit status, window list
**Automated:** tests/appium/test_06_settings.py::test_set009_quit_while_settings_is_open_exits_cleanly (Fedora 43 image only; the Debian 13 / Ubuntu 25.04 run is out of this harness's scope)

---

## TC SET-010: Zoom Style Combo

**Precondition:** Settings dialog open, Appearance page. Dock visible with multiple items. Zoom factor > 1.0.
**Steps:**
1. Find the "Zoom style" combo box in the AT-SPI tree
2. Verify the combo offers exactly two entries, "Parabolic - neighbors move aside" and "In place - icons overlap", and is set to "Parabolic - neighbors move aside" by default
3. Move the pointer to a middle dock item, wait 200ms, take a screenshot — neighbours are pushed aside and the dock background grows
4. Select "In place - icons overlap", wait 500ms
5. Move the pointer away and back to the middle dock item, wait 200ms
6. Screenshot and AT-SPI bounding boxes — icons magnify in place without moving; bounding-box centres unchanged, magnified icons may overlap
7. Check `~/.config/kremarc` (or the combo after reopening Settings) — `ZoomStyle=1` persisted
8. Select "Parabolic - neighbors move aside", verify `ZoomStyle=0` persisted (or the key is removed as the default)
9. Set "Zoom factor" slider to 1.0 — verify the combo becomes disabled
10. Restore zoom factor > 1.0

**Expected:**
- Combo defaults to "Parabolic - neighbors move aside" and applies live without restart
- "In place - icons overlap" restores in-place zoom: icons scale in place (positions unchanged, overlap allowed)
- "Parabolic - neighbors move aside" pushes neighbours aside and grows the dock background
- Setting persists to `kremarc` as `ZoomStyle` (0 = Parabolic, 1 = In place)
- Combo is disabled while zoom factor is 1.0 (no zoom to lay out)

**Verification:** AT-SPI (combo entries/state/enabled, item bounding-box centres), screenshot (Parabolic vs In place zoom), kremarc (`ZoomStyle` key)
**Automated:** tests/appium/test_06_settings.py::test_set010_zoom_style_combo_switches_zoom_live_and_persists

---

## TC SET-011: Zoom Animation Duration

**Precondition:** Settings dialog open, Appearance page. Dock visible with multiple items. Zoom factor > 1.0 and Zoom style set to Parabolic.
**Steps:**
1. Find the "Zoom animation duration (ms)" spin box and verify it is enabled, shows 100, and accepts values from 0 to 1000 in steps of 25
2. Set the duration to 500 ms, move the pointer onto a middle dock item, wait 250ms, and capture a screenshot; move the pointer away, wait 250ms, and capture another screenshot
3. Select "In place - icons overlap" and repeat the hover check with the 500 ms duration
4. Set the duration to 0 ms and verify hover zoom snaps immediately
5. Set "Zoom factor" to 1.0 and verify the duration control is disabled
6. Restore zoom factor > 1.0, set the duration to 250 ms, close Settings, restart Krema, and reopen Settings

**Expected:**
- Duration changes apply to hover zoom immediately in both Parabolic and In place styles; the value is an unscaled baseline, so Plasma animation scaling remains active
- A duration of 0 ms disables the hover transition, so zoom changes snap instantly; Instant/reduced-motion behavior remains unchanged
- The duration control is disabled while zoom factor is 1.0
- The selected duration remains 250 ms after restart

**Verification:** AT-SPI (spin box range, step, value, and enabled state), screenshot (hover transitions), `kremarc` (`ZoomAnimationDuration` key)
**Automated:** `tests/appium/test_06_settings.py::test_set001_settings_opens_once_with_formcard_controls_and_keeps_dock_shown`, `test_set005_changed_settings_persist_across_restart`, and `test_set010_zoom_style_combo_switches_zoom_live_and_persists` cover the default, 25 ms steps, persistence, zero value, and disabled state. `tests/qml/tst_dockitem_zoom.qml::test_zeroDurationSnapsInAndOut` and `test_customDurationUsesConfiguredTimeline` cover snapping and animation timing in both styles. The live-dock 500 ms screenshot checks remain manual.

---

## TC SET-012: Selected Monitors, Fallback, and Shortcut Routing

**Precondition:** Three usable outputs A, B, and C, with A primary. Krema uses Auto hide. Open two windows of one app for preview checks. The two-output automation covers switches, fallback, and persistence; the three-output automation covers a subset that excludes the primary.

**Steps:**
1. Confirm "Primary monitor only" shows one dock on A and "All monitors" shows one dock on each output. Open Settings → Behavior and record the focused Settings window.
2. Select "Selected monitors". Enable C, then B, leaving A off. Verify docks and their preview surfaces appear only on B and C, and `kremarc` saves `MonitorMode=3` with `SelectedOutputs` in the exact chosen order C, B.
3. Turn B off and on. Verify C's dock and preview surfaces keep their window identities. The same Settings window stays open and focused; the newly created B dock stays visible while Settings is open. Close Settings and verify both docks auto-hide normally.
4. Put the pointer on unselected A and invoke Focus Dock. Verify it targets B when compositor order is A, B, C, despite saved order C, B. Check Toggle Dock, Meta+N, and Meta+Shift+N use B. Make C primary and verify those actions target C without recreating the retained B/C dock or preview surfaces. Restore A as primary.
5. Disconnect and reconnect unselected A while B and C remain usable. Verify their retained dock and preview surfaces stay on their outputs with the same window identities.
6. Open Settings again. Disconnect B, then C. Verify one temporary dock appears on primary A, the warning appears, and saved names remain C, B. Reconnect B, then C; verify the selected subset returns and the temporary dock and warning disappear without closing Settings.
7. Disconnect C again and turn off its disconnected switch. Verify C is removed from `SelectedOutputs` and its disconnected row disappears. Reconnect C and verify it remains unselected, with no dock.
8. Turn B off so the selection is empty. Verify one temporary primary dock and the warning, with an empty saved list rather than A being added. Select B again and verify the warning clears and only B has a dock.
9. Switch through the other monitor modes. Verify the selected switches and warning are hidden without clearing the saved B name. Return to Selected monitors, close Settings, restart Krema, and verify mode 3, the exact B name, its dock placement, and its output-aligned preview restore.

**Expected:**
- Mode 3 selects only exact saved output names that are connected and have non-empty geometry; modes 0, 1, and 2 retain their behavior, and the default remains 0
- Primary changes and unselected topology changes preserve retained selected dock and preview surfaces
- Empty or wholly unavailable selections use one temporary primary dock without changing the saved names; a returning selected output replaces it
- Disconnected selections remain removable, and removing one prevents its dock from returning
- Selection changes preserve the shared, focused Settings window; new docks inherit its interaction lock until the dialog closes
- Shortcut order is selected primary, adopted compositor order, then saved selection order; Focus Dock on an unselected output uses the same fallback
- Mode 3 and exact selected names persist across restart; previews stay aligned with their own output

**Verification:** AT-SPI (native switches, disconnected rows, warning, focused Settings), KWin (mapped dock/preview output and window identity, keyboard focus), `kremarc` (mode and exact name list), KGlobalAccel actions, and output enable/disable or cable reconnection for topology steps.
**Automated:** `tests/appium/test_06_settings.py::test_set012_selected_monitors_toggle_keeps_settings_open` and `test_set012_selected_monitors_fallback_warns_and_keeps_saved_names[empty]` / `[disconnected]` run with two outputs. `test_set012_selected_subset_preserves_docks_and_routes_shortcuts` runs with three outputs. `tests/kwin/test_selected_outputs.cpp` (ctest `krema_selected_output_tests`) covers primary changes, actual virtual-output disable/enable, retained shells, and deterministic routing; `tests/integration/test_settings_lifecycle.cpp` covers the shared Settings lifecycle. Physical cable reconnection remains a manual check.

---

## TC SET-013: Click actions apply live and persist independently

**Precondition:** Settings open on Behavior. The controls show the exact
labels `Single window click action` and `Grouped window click action`.

**Steps:**
1. Exercise all six pairs of `Activate window`/`Minimize active window` and
   `Cycle through windows`/`Show window previews`/`Minimize active window`.
2. Change both controls from their initial values and observe single and
   grouped windows without restarting.
3. Restart Krema, reopen Settings, and repeat the state checks.
4. In a two-output session, choose `All monitors`, recreate the second dock,
   and verify the choices remain shared.

**Expected:**
- Each control applies independently and immediately.
- The saved pair survives restart and remains available to recreated docks.
- Single and grouped consumer effects match the selected pair.

**Verification:** AT-SPI FormComboBoxDelegate values, `kremarc`, KWin
active/minimized state, popup state, and two-output dock state.
**Automated (Tier 2):** `tests/appium/test_06_settings.py::test_clk002_click_action_combinations_apply_live_persist_and_restore`
(six cases) and
`tests/appium/test_06_settings.py::test_clk002_all_screens_share_live_click_choices_and_recreated_dock_restores_them`
(`outputs(2)`).

Run the multi-screen cases with:
`KREMA_E2E_OUTPUT_COUNT=2 tests/appium/run-e2e.sh -m outputs`.

---

## TC SET-014: Hover controls stay independent from explicit preview

**Precondition:** Settings open on Window Preview. The controls show the exact
labels `Show window previews on hover`, `Thumbnail width (px)`, `Hover delay
(ms)`, and `Hide delay (ms)`.

**Steps:**
1. Toggle hover previews on and off with each grouped action choice.
2. Observe which controls are enabled and change their values.
3. Close Settings, then open the popup by hover or explicit click according
   to the selected controls.

**Expected:**
- `Thumbnail width (px)` and `Hide delay (ms)` are enabled when hover is
  enabled or the grouped action is `Show window previews`.
- `Hover delay (ms)` is enabled only when hover previews are enabled.
- Hover-off group1 still opens an explicit popup on click.
- Changed values are saved to `kremarc` and affect popup width/hide behavior
  without restarting.

**Verification:** AT-SPI control enabled state and values, `kremarc`, popup
visibility, labels, and thumbnail width. Visual thumbnail checks require
DRM/vgem capture.
**Automated (Tier 2):** `tests/appium/test_06_settings.py::test_clk011_preview_controls_follow_hover_and_explicit_group_choice`
(six cases).

---

## TC SET-015: Reserve screen space applies live and survives restart

**Precondition:** A fresh configuration, one output without another panel,
and a real fixture window maximized by KWin. Open Settings → Behavior with
visibility mode `Always visible`. Record Krema's process and the fixture
window's identity and frame geometry.

**Steps:**
1. Find the `Reserve screen space` switch. Verify it is checked by default
   and its description explains that maximized windows avoid the dock.
2. Turn it off using the actual switch. Wait for the already-maximized
   fixture's frame to reach the output edge occupied by the dock; do not
   restore/remaximize the fixture or restart Krema.
3. Turn it on. Verify the same fixture frame moves inward to leave the
   panel-bar space while the dock stays visible and the process is unchanged.
4. Turn it off again, close Settings, and restart Krema. Reopen Behavior
   and verify the switch remains off and the maximized fixture uses the
   full output. Check `ReserveScreenSpace=false` in `[General]` in `kremarc`.
5. Select `Auto hide`, then `Dodge windows`. Verify the reservation row is
   hidden in each mode, without clearing the saved value.
6. Return to `Always visible`, turn the switch on, and restart again.
   Verify the checked state and the reserved maximized geometry return.
   A missing `ReserveScreenSpace` key is valid when the default is true.

**Expected:**
- Reservation changes independently of visibility mode; turning it off
  does not hide the Always Visible dock.
- Existing maximized windows reflow in the same session for both directions
  of the toggle; restarting is needed only to check persistence.
- Auto Hide and Dodge Windows do not reserve space, regardless of the saved
  switch value.
- The switch state and observed window bounds agree after restart.

**Verification:** AT-SPI switch state/visibility, `kremarc`, process/window
identity, and KWin output/frame geometry. Record the actual frame bounds,
not only the maximize command or an exclusive-zone setter call. Screenshot
capture is supplementary; missing DRM must not skip geometry checks.
**Automated (Tier 2 native coverage):**
`tests/appium/test_06_settings.py::test_set015_reservation_switch_is_native_conditional_and_autosaves`
checks the native switch, autosave, mode-dependent row visibility, and
retained preference without restarting the process. VIS-009 maps the
maximized geometry and restart checks.

---

## TC SET-016: Separate pinned and running apps applies live and persists

**Precondition:** A fresh configuration with two pinned fixture apps and
two unpinned running fixture apps. At least one pinned app is running.
Open Settings → Behavior.

**Steps:**
1. Find `Separate pinned and running apps`. Verify the switch is off by
   default and its description states that pinned apps, including running
   ones, stay together, with unpinned running apps after the divider.
2. Turn it on using the actual switch. Close Settings and read dock items
   in visual order from AT-SPI item centres.
3. Verify pinned apps form the leading section and unpinned running apps
   follow. Verify each running pinned app has one icon in its pinned slot.
   Locate the accessible separator named `Pinned and running apps separator`.
4. Reopen Settings and turn it off. Verify the separator disappears without
   losing apps or changing their pinned membership.
5. Turn it on again, close Settings, and restart Krema. Reopen Settings;
   verify the switch is checked, `[General]` saves `SeparateLaunchers=true`,
   and the real dock restores its two sections.
6. Turn it off and restart again. Verify the switch is unchecked and the
   ordinary free-reorder behavior returns. An absent key is valid for the
   default false value.
7. Run the pin/unpin, launch/close/group, empty-section, and drag-boundary
   checks linked from the context-menu, mouse, and drag scenarios.

**Expected:**
- The setting changes the current dock without a restart and survives one.
- Running pinned apps stay in the pinned section with one icon per app.
- The divider is shown only when separation is on and both sections contain
  items; the setting does not itself pin an unpinned app.
- With separation off, existing manual ordering remains available.

**Verification:** AT-SPI switch, ordered app names and separator role/state,
KWin fixture windows, `kremarc` membership and setting, and actual input.
Divider paint, hover movement, and hit-testing require the additional
interaction scenarios; a checked switch alone is not their proof.
**Automated (Tier 2 native coverage):**
`tests/appium/test_09_task_zones.py::test_tzone001_live_toggle_orders_visible_tasks_and_persists`
checks the live switch, ordered sections, saved values, and ON/OFF restarts
on top, bottom, left, and right edges.
`test_tzone002_separator_accessibility_geometry_and_single_zone_visibility`,
`test_tzone003_grouped_instances_keep_one_pinned_slot_and_close_differently`,
and `test_tzone004_pin_and_unpin_use_real_context_menu_and_preserve_membership`
map the separator and app lifecycle checks.
`test_tzone007_fresh_default_separation_is_off` checks the unchecked native
switch with a fresh configuration.
The task-section checks remain geometry- and AT-SPI-focused; divider paint,
hover movement, hit-testing, preview image/RHI content, installed-package Tier
3, and drop-ghost appearance require their dedicated environments and remain
unverified here. The PREV-010 vertical fixture intentionally uses two grouped
windows for strict index/pin-transition coverage while the separate
preview-surface fix owns whole-popup/thumbnail containment for wider vertical
previews.
