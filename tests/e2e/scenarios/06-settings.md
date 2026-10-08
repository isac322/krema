# Settings UI

## Features
- settings-window: Sidebar of 8 pages plus About (Icons, Layout & Position, Panel Style, Shadow, Animations & Badges, Behavior, Monitors & Desktops, Window Preview, About Krema, About KDE), settings search, one window per open
- settings-icons: Icon size, spacing, scale, icon opacity (active, inactive/launcher, minimized), zoom factor, zoom style cards, zoom animation preset cards and custom zoom-in/zoom-out timing, with a sample dock preview
- settings-layout: Screen edge picker on a monitor schematic, floating, corner radius
- settings-visual-pickers: Card pickers (ChoiceCard) and schematic radios that work by mouse, keyboard, and AT-SPI
- settings-click-actions: Independent single and grouped left-click choices
- settings-click-persistence: Six policy pairs apply live, save, and restore
- settings-behavior: Visibility mode cards, reservation, dodge and delay controls
- settings-monitors: Monitor mode cards, selected monitor switches and temporary fallback, follow trigger, screen transition, virtual desktop display mode (Monitors & Desktops page)
- settings-reserve-screen-space: Independent Always Visible reservation switch applies to maximized windows live and persists
- settings-separate-launchers: Optional pinned/running sections apply live and persist without duplicating running pinned apps
- settings-preview: Preview enable/disable, thumbnail size
- settings-persist: Settings saved to KConfig and restored on restart
- settings-live-preview: Changes apply in real-time without restart
- settings-tint-color: Custom tint color selection through a color swatch button
- settings-background-style: Background style cards (Panel Inherit, Transparent, Tinted, Acrylic) with live previews
- settings-shadow: Light-source editor, advanced exact values, shadow color

## Affected Files
- src/qml/settings/ChoiceCard.qml
- src/qml/settings/SliderDelegate.qml
- src/qml/settings/ColorSwatchButton.qml
- src/qml/settings/FormSection.qml
- src/qml/settings/IconsPage.qml
- src/qml/settings/LayoutPage.qml
- src/qml/settings/PanelStylePage.qml
- src/qml/settings/ShadowPage.qml
- src/qml/settings/AnimationsPage.qml
- src/qml/settings/MonitorsPage.qml
- src/qml/settings/BehaviorPage.qml
- src/qml/settings/PreviewPage.qml
- src/qml/SettingsDialog.qml
- src/qml/main.qml
- src/qml/ZoomAnimationProfile.qml
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
5. Check the AT-SPI tree: frame "Settings", "Search settings" field, list "Settings pages" whose items are, in order, "Icons", "Layout & Position", "Panel Style", "Shadow", "Animations & Badges", "Behavior", "Monitors & Desktops", "Window Preview", "About Krema", "About KDE"; "Icons" is checked. The Icons page shows the "Icon size" slider and the "Zoom animation" card: the "Preset" tab is selected and the "Natural" preset card is checked
6. Screenshot — verify settings dialog
7. Right-click the dock again → "Settings..." while the dialog is open
8. Check the window list — there is still exactly one settings window (it is raised, not duplicated). Run this on the oldest supported kirigami-addons (1.7.0, Debian 13 / Ubuntu 25.04) as well
9. Click "Monitors & Desktops" in the sidebar — the item becomes checked and the page shows the "Monitor mode" cards. Click the checked item again — it stays checked
10. Type "corner" in "Search settings" — the list shows "Layout & Position" (matched by the "Corner radius" setting) and keeps the current page's item; clear the field and all entries return
11. Press Escape, then choose "Settings..." again — a new window opens on the Icons page

**Expected:**
- Settings dialog opens as separate window ("Settings" title, localized)
- Left sidebar: search field, then Icons, Layout & Position, Panel Style, Shadow, Animations & Badges, Behavior, Monitors & Desktops, Window Preview, and an "About" section with About Krema and About KDE
- Icons page shown by default
- FormCard layout with sliders, card pickers, switches, spin boxes and combo boxes
- All controls accessible via AT-SPI (labels, sliders with Increase/Decrease, cards as radio buttons)
- The search matches page names and setting names; the selected page never disappears from the list
- Choosing "Settings..." again raises the same window; the dock stays shown while it is open
- Escape closes the window; the next open creates a fresh window

**Verification:** window list (window count +1), AT-SPI (sidebar list, FormCard widgets), screenshot
**Automated:** tests/appium/test_06_settings.py::test_set001_settings_opens_once_with_formcard_controls_and_keeps_dock_shown checks the 10 sidebar entries in order, the Icons default, page switching (to Animations & Badges, which shows 7 "Attention animation" and 3 "Badge display" radio cards), and the single window (English session: title "Settings"; the harness image is Fedora 43 with kirigami-addons ≥ 1.8, so the kirigami-addons 1.7.0 / Debian 13 / Ubuntu 25.04 run is out of this harness's scope). A fresh window after close is checked by `test_set005_changed_settings_persist_across_restart` and `test_set008_monitor_mode_all_monitors_from_open_settings` (closed with Alt+F4). The search (step 10) and Escape (step 11) remain manual checks.

---

## TC SET-002: Change Icon Size (Live Preview)

**Precondition:** Settings dialog open, Icons page active.
**Steps:**
1. Screenshot of dock — capture baseline icon size
2. In settings, find the "Icon size" slider (its label shows the value, e.g. "48 px")
3. Change icon size (e.g., increase it)
4. Wait 300ms
5. Screenshot of dock — verify icon size changed

**Expected:**
- Dock icons resize in real-time as the value changes
- No restart required
- The sample dock at the top of the Icons page resizes too
- Zoom proportions adjust accordingly (the Parabolic zoom style keeps icons separated while scaling)

**Verification:** screenshot comparison (icon size changed)
**Automated:** tests/appium/test_06_settings.py::test_set002_icon_size_slider_resizes_dock_live_and_keeps_zoom_proportion (the Icon size control is a slider: real keyboard Right/Up, dock item width checked after every step)

---

## TC SET-003: Change Visibility Mode

**Precondition:** Settings dialog open, Behavior page.
**Steps:**
1. Current visibility mode: AlwaysVisible ("Always visible" card checked)
2. Click the "Auto hide" card in the "Visibility mode" group (or focus it with Tab and press Space)
3. Wait 500ms
4. Move mouse to center of screen (away from dock)
5. Wait for hide timer
6. Screenshot — verify dock is hidden

**Expected:**
- Only the "Auto hide" card is checked; the "Reserve screen space" switch is hidden and the show/hide delay sliders appear
- Dock hides when mouse moves away
- Visibility mode change applies immediately
- Setting persists in KConfig

**Verification:** screenshot (dock hidden)
**Automated:** tests/appium/test_06_settings.py::test_set003_auto_hide_applies_immediately_and_persists (the open Settings dialog holds the dock shown by design, see SET-008, so hiding is checked after the dialog is closed with Alt+F4, in the same krema process)

---

## TC SET-004: Change Background Style

**Precondition:** Settings dialog open, Panel Style page.
**Steps:**
1. Screenshot of dock — capture current background
2. Click the "Acrylic" card in the "Style" group
3. Wait 500ms
4. Screenshot of dock — verify background changed

**Expected:**
- Dock background changes to acrylic/blur effect
- Change applies in real-time
- Only the "Acrylic" card is checked; each card's mini dock previews its own style

**Verification:** screenshot comparison (background style changed), AT-SPI (card checked state)
**Automated:** tests/appium/test_06_settings.py::test_set004_acrylic_background_applies_live (pixel oracle: the Panel Inherit panel is flat, the Acrylic panel shows the acrylic shader's per-pixel noise grain over the same black background)

---

## TC SET-005: Settings Persist After Restart

**Precondition:** Changed multiple settings (icon size, zoom animation preset, visibility mode, background style).
**Steps:**
1. Note current settings values
2. Close settings dialog
3. Restart krema
4. Wait 2000ms
5. Open settings dialog
6. Verify all settings match previously set values on the Icons, Behavior, and Panel Style pages

**Expected:**
- All settings restored from KConfig
- Sliders, checked cards, and switches show the saved values
- Dock appearance matches the saved settings

**Verification:** AT-SPI (control values), screenshot (visual match)
**Automated:** tests/appium/test_06_settings.py::test_set005_changed_settings_persist_across_restart

---

## TC SET-006: Dock Position Change

**Precondition:** Settings dialog open, Layout & Position page. Dock currently at Bottom.
**Steps:**
1. Click the top edge of the monitor schematic in the "Screen edge" group (AT-SPI: radio button "Top"; also selectable with Tab + Space/Return)
2. Wait 500ms
3. Screenshot — verify dock moved to top of screen

**Expected:**
- Dock repositions to top edge of screen
- Layer-shell anchor updates correctly
- All items render correctly in new position
- The schematic shows the mini dock on the top edge and only the "Top" radio is checked

**Verification:** screenshot (dock at top), AT-SPI (radio checked state)
**Automated:** tests/appium/test_06_settings.py::test_set006_screen_edge_top_moves_dock_to_top ("Screen edge" schematic on the Layout & Position page)

---

## TC SET-007: Tint Color Selection

**Precondition:** Settings dialog open, Panel Style page. Background style set to "Tinted", "Use system color" off.
**Steps:**
1. Find the tint color swatch button (AT-SPI button named "Tint color: <current color>")
2. Click it (or focus it and press Space) to open the color dialog
3. Select a different color
4. Confirm selection
5. Screenshot — verify dock background uses new tint color

**Expected:**
- Dock background tint color changes to selected color
- Color saved to KConfig
- The swatch and its accessible name show the new color; the style card previews use it

**Verification:** screenshot (tint color changed)
**Automated:** tests/appium/test_06_settings.py::test_set007_custom_tint_color_is_applied_and_saved ("Use system color" must be switched off first for the Tint color swatch to appear; the colour is entered in the dialog's Hex field)

---

## TC SET-008: Change Monitor Mode From Settings

**Precondition:** Two outputs, monitor mode "Primary monitor only", settings dialog open, Monitors & Desktops page. Visibility mode "Auto hide".
**Steps:**
1. Click the "All monitors" card in "Monitor mode"
2. Wait 500ms
3. Check the window list — Krema is still running and one dock window exists per output
4. Screenshot — the settings dialog is still open and docks on both outputs are shown
5. Right-click the dock on the second output → "Settings..."
6. Check the window list — there is still exactly one settings window
7. Click the "Primary monitor only" card, then close the settings dialog
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

## TC SET-010: Zoom Style Cards

**Precondition:** Settings dialog open, Icons page. Dock visible with multiple items. Zoom factor > 1.0.
**Steps:**
1. Find the "Zoom style" group in the AT-SPI tree
2. Verify it holds exactly two radio cards, "Parabolic - neighbors move aside" and "In place - icons overlap", and "Parabolic - neighbors move aside" is checked by default
3. Move the pointer to a middle dock item, wait 200ms, take a screenshot — neighbours are pushed aside and the dock background grows
4. Click the "In place - icons overlap" card, wait 500ms
5. Move the pointer away and back to the middle dock item, wait 200ms
6. Screenshot and AT-SPI bounding boxes — icons magnify in place without moving; bounding-box centres unchanged, magnified icons may overlap
7. Check `~/.config/kremarc` (or the cards after reopening Settings) — `ZoomStyle=1` persisted
8. Click the "Parabolic - neighbors move aside" card, verify `ZoomStyle=0` persisted (or the key is removed as the default)
9. Set "Zoom factor" slider to 1.0 — verify the zoom style cards and the "Zoom animation" card controls become disabled
10. Restore zoom factor > 1.0

**Expected:**
- Cards default to "Parabolic - neighbors move aside", exactly one card is checked, and the choice applies live without restart
- "In place - icons overlap" restores in-place zoom: icons scale in place (positions unchanged, overlap allowed)
- "Parabolic - neighbors move aside" pushes neighbours aside and grows the dock background
- Setting persists to `kremarc` as `ZoomStyle` (0 = Parabolic, 1 = In place)
- Zoom style cards and "Zoom animation" controls are disabled while zoom factor is 1.0 (no zoom to lay out); the selected zoom animation preset is preserved

**Verification:** AT-SPI (card checked/enabled state, item bounding-box centres), screenshot (Parabolic vs In place zoom), kremarc (`ZoomStyle` key)
**Automated:** tests/appium/test_06_settings.py::test_set010_zoom_style_cards_switch_zoom_live_and_persists

---

## TC SET-011: Zoom Animation Presets and Custom Timing

**Precondition:** Settings dialog open, Icons page. Dock visible with multiple items. Zoom factor > 1.0 and Zoom style set to Parabolic. Fresh configuration (no `ZoomAnimationPreset` key).
**Steps:**
1. Find the "Zoom animation" section and verify it has two tabs, "Preset" and "Custom"; "Preset" is selected and the "Natural" card is checked. The preset cards are "Natural", "Quick", "Relaxed", and "Instant", each with a description, a looping zoom motion preview, and (except Instant) a "Zoom in … ms, zoom out … ms" summary
2. Move the pointer onto a middle dock item, wait 100ms, capture a screenshot; wait another 150ms and capture again; move the pointer away and repeat. Natural accelerates then decelerates into the peak (180 ms in, 240 ms out at normal animation speed)
3. Select "Quick" and repeat the hover check: the zoom starts fast and decelerates (100 ms ease-out in both directions, the Krema 0.10 behaviour). Verify `ZoomAnimationPreset=1`
4. Select "Relaxed" (300 ms in, 400 ms out, gentle ease in and out) and then "Instant" (hover zoom snaps), checking `ZoomAnimationPreset=2` and `3`
5. Select "Quick", then click the "Custom" tab. Verify `ZoomAnimationPreset=4` and the tab shows an easing curve and motion preview with a "Zoom in … ms, zoom out … ms" summary, then "Zoom-in duration (ms)", "Zoom-in easing", "Zoom-out duration (ms)", and "Zoom-out easing" with the Natural defaults (180 ms / 240 ms, "Ease in and out")
6. Raise "Zoom-in duration (ms)" by two steps (10 ms each) to 200 and choose "Linear" for "Zoom-out easing". Verify `ZoomInDuration=200` and `ZoomOutEasing=0`; the easing combos offer exactly "Linear", "Ease in", "Ease out", "Ease in and out", "Gentle ease in and out"
7. Hover a dock item: zoom-in and zoom-out follow the custom durations and curves without a restart
8. Click the "Preset" tab: "Quick" is restored (`ZoomAnimationPreset=1`) and its card is checked; "Natural", "Relaxed", and "Instant" are unchecked
9. Select "Relaxed" (`ZoomAnimationPreset=2`), then "Quick" again (`ZoomAnimationPreset=1`): each time only the selected card is checked. Click the already-checked "Quick" card once more: it stays checked and `ZoomAnimationPreset` stays 1
10. Scroll the mouse wheel down one step with the pointer over the "Preset"/"Custom" tab bar, then back up: the Settings page scrolls (the tab bar moves with the page and returns), the "Preset" tab stays selected, "Quick" stays checked, and `ZoomAnimationPreset` stays 1
11. Click "Custom" again: the custom values are unchanged
12. Click the "In place - icons overlap" zoom style card and repeat a hover check: moving the pointer along the dock tracks it directly without lag; only entering and leaving the dock animate
13. Set "Zoom factor" to 1.0 and verify the whole section is disabled; restore zoom factor > 1.0
14. With the "Custom" tab selected, close Settings, restart Krema, and reopen Settings. Verify the "Custom" tab and values are restored. Click "Preset": with no earlier preset in this Settings session, "Natural" is selected

**Expected:**
- Preset and custom changes apply to hover zoom immediately in both Parabolic and In place styles; durations are unscaled baselines, so Plasma animation scaling remains active and Plasma "Instant" animation speed still snaps
- Only entering and leaving the dock animate; pointer movement along the dock never lags
- Zoom-in and zoom-out use independent durations and easing; a 0 ms direction snaps
- The "Custom" tab stores `ZoomAnimationPreset=4`; the "Preset" tab restores the last preset chosen in the current Settings session, otherwise Natural
- Card checked state always mirrors the stored preset, including after a Custom → Preset round trip and after clicking the already-checked card
- The mouse wheel over the tab bar scrolls the page; it never switches the tab or changes the preset
- The section is disabled while zoom factor is 1.0
- Preset, custom durations, and custom easing persist across restart

**Verification:** AT-SPI (tab and card checked/enabled state, spin box value and step, combo choice), screenshot (hover transitions), `kremarc` (`ZoomAnimationPreset`, `ZoomInDuration`, `ZoomOutDuration`, `ZoomInEasing`, `ZoomOutEasing` keys)
**Automated:** `tests/appium/test_06_settings.py::test_set011_zoom_animation_preset_and_custom_tabs_apply_and_persist` covers the tabs, custom spin box and easing combo, Preset restore with exactly one checked card (steps 8–9, including re-clicking the checked card), the tab-bar wheel regression (step 10), and restart; its hover check after the custom edits is only a smoke check (hover still zooms, no restart), not a timing check. `tests/qml/tst_dock_main.qml::test_customZoomTimingDrivesProductionDock` covers the custom zoom-in/zoom-out timing driving the production `main.qml` dock (step 7). `test_set001_settings_opens_once_with_formcard_controls_and_keeps_dock_shown`, `test_set005_changed_settings_persist_across_restart`, and `test_set010_zoom_style_cards_switch_zoom_live_and_persists` cover the Natural default, preset persistence, the Instant preset, and the disabled state. `tests/qml/tst_dockitem_zoom.qml::test_scaleEasesInAndSettlesBackOnExit`, `test_zeroDurationSnapsInAndOut`, `test_customInOutDurationsUseConfiguredTimelines`, `test_presetsResolveTimingAndEasing`, `test_naturalEasesInSlowerThanQuick`, `test_zeroZoomInDurationSnapsInButAnimatesOut`, and `test_inPlaceTracksPointerWithoutLag` cover timing, easing, and pointer tracking in both styles. `tests/unit/test_settings_migration.cpp` covers migrating a legacy duration to the Custom preset. The live-dock screenshot checks of the curve shape remain manual.

---

## TC SET-012: Selected Monitors, Fallback, and Shortcut Routing

**Precondition:** Three usable outputs A, B, and C, with A primary. Krema uses Auto hide. Open two windows of one app for preview checks. The two-output automation covers switches, fallback, and persistence; the three-output automation covers a subset that excludes the primary.

**Steps:**
1. Confirm "Primary monitor only" shows one dock on A and "All monitors" shows one dock on each output. Open Settings → Monitors & Desktops and record the focused Settings window.
2. Click the "Selected monitors" card. Enable C, then B, leaving A off. Verify docks and their preview surfaces appear only on B and C, and `kremarc` saves `MonitorMode=3` with `SelectedOutputs` in the exact chosen order C, B.
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
4. In a two-output session, choose the `All monitors` card on the Monitors &
   Desktops page, recreate the second dock, return to Behavior, and verify
   the choices remain shared.

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

---

## TC SET-017: Visual pickers work from the keyboard and AT-SPI

**Precondition:** Settings dialog open on the Behavior page, keyboard focus in
the window, Orca or an AT-SPI inspector available.

**Steps:**
1. Press Tab until a "Visibility mode" card has focus. Verify the cards are
   AT-SPI radio buttons named "Always visible", "Auto hide", and "Dodge
   windows", with `checkable` and exactly one `checked`.
2. Focus "Auto hide" and press Space; focus "Dodge windows" and press Return.
   Verify each choice applies (`VisibilityMode` in `kremarc`) and only the
   chosen card is checked. Return to "Always visible".
3. Open Layout & Position. Tab to the "Screen edge" group: only the checked
   edge is a Tab stop. Press Up, then Down: the dock moves to the top and
   back to the bottom edge, and the "Top"/"Bottom" radio's checked state
   follows.
4. Open Panel Style with "Tinted" selected and "Use system color" off. Tab to
   the tint swatch: AT-SPI reports a button named "Tint color: <color>".
   Press Return: the color dialog opens; cancel it.
5. Open Shadow. Tab to the light source: AT-SPI reports a slider named "Light
   position" whose description lists X, Y, height, and radius. Press Right
   arrow, Shift+Right, Page Up, and plus; verify the description and the
   "Advanced settings" values change accordingly.
6. Tab to a slider such as "Shadow intensity": its accessible name equals the
   visible label and Increase/Decrease actions change the value.

**Expected:**
- Every card picker and the edge schematic can be operated without a mouse,
  announce their option names, and expose one checked option per group.
- Space or Return selects a card; the arrow keys pick schematic edges.
- Color swatches announce the setting and its current color.
- The light source can be moved and resized from the keyboard.

**Verification:** AT-SPI (roles, names, `checked`/`focused` states,
descriptions), `kremarc`, screenshot of the dock edge.
**Automated:** none; pointer clicks on the same cards and on the "Top" edge
radio are covered by SET-003, SET-004, SET-006, and SET-010. The keyboard
paths remain manual checks.

---

## TC SET-018: Shadow light-source editor and advanced values

**Precondition:** Settings dialog open on the Shadow page, "Enable shadow"
on, dock visible.

**Steps:**
1. Drag the light circle in the editor to the upper left. Verify the shadow
   preview below the editor and the real dock shadow move away from the
   light, and the value line ("X … · Y … · Height … · Radius … · Elevation …")
   updates.
2. Drag the light's outer ring outward, then scroll over the light. Verify
   the radius (softness) and height values change.
3. Drag the dock rectangle, or use the vertical "Panel elevation" slider beside
   it. Verify the elevation value changes.
4. Click "Advanced settings". Verify the expander reports "Expanded" and shows
   the "Light X", "Light Y", "Light height", "Light radius", "Panel
   elevation", and "Shadow intensity" sliders with the values set above.
   Change "Light X" and verify the light circle moves.
5. Turn "Enable shadow" off. Verify the editor and the controls below it are
   hidden and the dock shadow disappears.

**Expected:**
- Editor drags, advanced sliders, and the real dock shadow stay in sync live.
- Values persist in `kremarc` (`ShadowLightX`, `ShadowLightY`,
  `ShadowLightZ`, `ShadowLightRadius`, `ShadowElevation`,
  `ShadowIntensity`).

**Verification:** AT-SPI (slider values, expander description), screenshot
(editor, preview, dock shadow), `kremarc`.
**Automated:** none (manual).

---

## TC SET-019: Monitors & Desktops follow, transition, and virtual desktop pickers

**Precondition:** Settings dialog open on the Monitors & Desktops page.

**Steps:**
1. Click the "Follow active screen" card. Verify the "Follow trigger" radio
   buttons ("Mouse position", "Active window focus", "Composite (focus +
   mouse)") and the "Screen transition" cards ("Fade", "Slide", "Instant")
   appear.
2. Select "Mouse position" and "Slide". Verify `FollowActiveTrigger=0`
   and `ScreenTransition=1` in `kremarc`, and only those options are checked.
3. Click "Primary monitor only". Verify the follow trigger and transition
   controls are hidden.
4. In the "Display mode" group, click "Dim other desktops". Verify the "Other
   desktop opacity" slider appears and moving it changes the opacity of
   icons for windows on other virtual desktops.
5. Click "Current desktop only", then "Show all windows". Verify the slider
   hides and the dock content follows each mode.

**Expected:**
- Each card group has exactly one checked card and applies live.
- Conditional controls appear only for the mode they belong to.
- Card previews animate only while the page is shown.

**Verification:** AT-SPI (checked state, control visibility), `kremarc`,
dock item opacity/visibility.
**Automated:** none; monitor mode cards and the selected monitor switches are
covered by SET-008 and SET-012.
