# Settings UI

## Features
- settings-appearance: Icon size, icon scale, zoom factor, zoom style, zoom animation duration, spacing, opacity, background style
- settings-behavior: Visibility mode, dock position, monitor mode
- settings-preview: Preview enable/disable, thumbnail size
- settings-persist: Settings saved to KConfig and restored on restart
- settings-live-preview: Changes apply in real-time without restart
- settings-tint-color: Custom tint color selection
- settings-background-style: Background style selection (transparent, semi-transparent, tinted, acrylic, mica)

## Affected Files
- src/qml/settings/AppearancePage.qml
- src/qml/settings/BehaviorPage.qml
- src/qml/settings/PreviewPage.qml
- src/qml/SettingsDialog.qml
- src/qml/DockItem.qml
- src/shell/settingswindow.h
- src/shell/settingswindow.cpp
- src/shell/dockshell.cpp
- src/shell/dockview.cpp
- src/shell/multidockmanager.h
- src/shell/multidockmanager.cpp
- src/models/taskiconprovider.h
- src/models/taskiconprovider.cpp
- src/config/krema.kcfg
- src/config/krema.kcfgc

---

## TC SET-001: Open Settings Dialog

**Precondition:** Dock visible.
**Steps:**
1. Right-click dock item → context menu appears (screenshot)
2. Click "Settings..." at estimated menu coordinates (~+123px from menu top)
3. Wait 1500ms (settings window creation)
4. `list_windows` — verify krema window count increased
5. `find_ui_elements query="Icon size" app_name="krema"` — verify AT-SPI access
6. `screenshot` — verify settings dialog
7. Right-click the dock again → "Settings..." while the dialog is open
8. `list_windows` — verify there is still exactly one settings window (it is raised, not duplicated). Run this on the oldest supported kirigami-addons (1.7.0, Debian 13 / Ubuntu 25.04) as well

**Expected:**
- Settings dialog opens as separate window ("설정 — Krema" title)
- Left sidebar: Appearance, Behavior, Window Preview, About Krema, About KDE
- Appearance page shown by default
- FormCard layout with spinboxes, sliders, comboboxes
- All controls accessible via AT-SPI (labels, sliders with Increase/Decrease)
- Choosing "Settings..." again raises the same window; the dock stays shown while it is open

**Verified in PoC:** Settings opened. Found "Icon size" label and
"Zoom factor" slider with Increase/Decrease actions in AT-SPI.

**Verification:** list_windows (window count +1), find_ui_elements (FormCard widgets), screenshot
**Automated:** tests/appium/test_06_settings.py::test_set001_settings_opens_once_with_formcard_controls_and_keeps_dock_shown (English session: title "Settings — Krema"; the harness image is Fedora 43 with kirigami-addons ≥ 1.8, so the kirigami-addons 1.7.0 / Debian 13 / Ubuntu 25.04 run is out of this harness's scope)

---

## TC SET-002: Change Icon Size (Live Preview)

**Precondition:** Settings dialog open, Appearance page active.
**Steps:**
1. `screenshot` of dock — capture baseline icon size
2. In settings, find icon size slider
3. Change icon size (e.g., increase by moving slider right)
4. Wait 300ms
5. `screenshot` of dock — verify icon size changed

**Expected:**
- Dock icons resize in real-time as slider moves
- No restart required
- Zoom proportions adjust accordingly (the Parabolic zoom style keeps icons separated while scaling)

**Verification:** screenshot comparison (icon size changed)
**Automated:** tests/appium/test_06_settings.py::test_set002_icon_size_spinbox_resizes_dock_live_and_keeps_zoom_proportion (the Icon size control is a spin box: real keyboard Up, dock item width checked after every step)

---

## TC SET-003: Change Visibility Mode

**Precondition:** Settings dialog open, Behavior page.
**Steps:**
1. Current visibility mode: AlwaysVisible
2. Select "Auto Hide" radio button
3. Wait 500ms
4. Move mouse to center of screen (away from dock)
5. Wait for hide timer
6. `screenshot` — verify dock is hidden

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
1. `screenshot` of dock — capture current background
2. Change background style from current to "Acrylic / Frosted Glass"
3. Wait 500ms
4. `screenshot` of dock — verify background changed

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

**Verification:** accessibility_tree (slider values), screenshot (visual match)
**Automated:** tests/appium/test_06_settings.py::test_set005_changed_settings_persist_across_restart

---

## TC SET-006: Dock Position Change

**Precondition:** Settings dialog open, Behavior page. Dock currently at Bottom.
**Steps:**
1. Select "Top" position
2. Wait 500ms
3. `screenshot` — verify dock moved to top of screen

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
5. `screenshot` — verify dock background uses new tint color

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
3. `list_windows` — verify Krema is still running and one dock window exists per output
4. `screenshot` — verify the settings dialog is still open and docks on both outputs are shown
5. Right-click the dock on the second output → "Settings..."
6. `list_windows` — verify there is still exactly one settings window
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

**Verification:** list_windows (window counts), screenshot (dialog + docks)
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

**Verification:** process exit status, `list_windows`
**Automated:** tests/appium/test_06_settings.py::test_set009_quit_while_settings_is_open_exits_cleanly (Fedora 43 image only; the Debian 13 / Ubuntu 25.04 run is out of this harness's scope)

---

## TC SET-010: Zoom Style Combo

**Precondition:** Settings dialog open, Appearance page. Dock visible with multiple items. Zoom factor > 1.0.
**Steps:**
1. `find_ui_elements query="Zoom style" app_name="krema"` — locate the combo box
2. Verify the combo offers exactly two entries, "Parabolic - neighbors move aside" and "In place - icons overlap", and is set to "Parabolic - neighbors move aside" by default
3. `mouse_move` to a middle dock item, wait 200ms, `screenshot` — neighbours are pushed aside and the dock background grows
4. Select "In place - icons overlap", wait 500ms
5. `mouse_move` away and back to the middle dock item, wait 200ms
6. `screenshot` + `accessibility_tree app_name="krema"` — icons magnify in place without moving; bounding-box centres unchanged, magnified icons may overlap
7. `read_file ~/.config/kremarc` (or `accessibility_tree` after reopening) — verify `ZoomStyle=1` persisted
8. Select "Parabolic - neighbors move aside", verify `ZoomStyle=0` persisted (or the key is removed as the default)
9. Set "Zoom factor" slider to 1.0 — verify the combo becomes disabled
10. Restore zoom factor > 1.0

**Expected:**
- Combo defaults to "Parabolic - neighbors move aside" and applies live without restart
- "In place - icons overlap" restores in-place zoom: icons scale in place (positions unchanged, overlap allowed)
- "Parabolic - neighbors move aside" pushes neighbours aside and grows the dock background
- Setting persists to `kremarc` as `ZoomStyle` (0 = Parabolic, 1 = In place)
- Combo is disabled while zoom factor is 1.0 (no zoom to lay out)

**Verification:** find_ui_elements (combo entries/state/enabled), screenshot (Parabolic vs In place zoom), accessibility_tree (item bounding-box centres), kremarc (`ZoomStyle` key)
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

**Verification:** find_ui_elements (spin box range, step, value, and enabled state), screenshot (hover transitions), `kremarc` (`ZoomAnimationDuration` key)

