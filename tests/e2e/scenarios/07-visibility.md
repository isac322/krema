# Visibility Control

## Features
- vis-always-visible: Dock always shown regardless of windows
- vis-reserve-screen-space: Optional Always Visible reservation reflows maximized windows live on all four edges
- vis-explicit-preview-hold: Repeated explicit previews release visibility holds after close
- vis-auto-hide: Dock hides after timeout, shows on mouse approach
- vis-dodge-windows: Dock hides when windows overlap its area
- vis-smart-hide: Dodge windows with "Only dodge active window": dock hides when the active window overlaps its area
- vis-keyboard-lock: Keyboard navigation prevents auto-hide
- vis-screen-edge-trigger: Mouse at screen edge triggers dock show
- vis-show-desktop: Show Desktop (Meta+D) hides app windows but not the dock

## Affected Files
- src/shell/dockvisibilitycontroller.h
- src/shell/dockvisibilitycontroller.cpp
- src/platform/waylanddockplatform.h
- src/platform/waylanddockplatform.cpp
- src/shell/dockview.h
- src/shell/dockview.cpp
- src/app/application.cpp
- src/qml/settings/BehaviorPage.qml
- src/qml/settings/LayoutPage.qml
- src/qml/settings/IconsPage.qml
- src/qml/main.qml
- src/config/krema.kcfg
**Tier:** Tier 2 (Appium) for real visibility, KWin, and multi-output state.
Tier 3 reuses the Appium cases against installed packages.
Keyboard navigation still locks visibility, and context-menu actions remain
unchanged; VIS-008 checks only release of the explicit-preview hold.



---

## TC VIS-001: Always Visible Mode

**Precondition:** Visibility mode set to AlwaysVisible and `Reserve screen space` enabled (the default).
**Steps:**
1. Screenshot — verify dock visible
2. Maximize a window (e.g., kcalc)
3. Wait 500ms
4. Screenshot — verify dock still visible
5. Move the pointer to the center of the screen
6. Wait 2000ms
7. Screenshot — verify dock still visible

**Expected:**
- Dock remains visible at all times
- With reservation enabled, maximized windows stop before the visible panel
  bar. Disabling reservation keeps the dock shown but allows windows to use
  that screen space; see VIS-009.

**Verification:** AT-SPI dock visibility and KWin maximized frame/output
geometry in all states; screenshot when capture is available.

**Automated:** tests/appium/test_07_visibility.py::test_vis001_always_visible_dock_stays_shown_over_a_maximized_window
**Automated:** tests/appium/test_07_visibility.py::test_vis001_always_visible_reserves_the_dock_area_for_maximized_windows

---

## TC VIS-002: Auto-Hide Mode — Hide on Timeout

**Precondition:** Visibility mode set to AutoHide. Mouse NOT on dock.
**Steps:**
1. Move the pointer to the dock area to show it
2. Wait 300ms — dock should be visible
3. Screenshot — verify visible
4. Move the pointer to the center of the screen (away from dock)
5. Wait for hide timeout (typically 1-2 seconds)
6. Screenshot — verify dock hidden (slid off screen)

**Expected:**
- Dock hides with slide animation after timeout
- Dock area is reclaimed (windows can use full screen)

**Verification:** screenshot (dock hidden)

**Automated:** tests/appium/test_07_visibility.py::test_vis002_auto_hide_hides_after_timeout_and_frees_the_screen

---

## TC VIS-003: Auto-Hide Mode — Show on Edge Approach

**Precondition:** VIS-002 completed (dock hidden in AutoHide mode).
**Steps:**
1. Move the pointer to the dock's screen edge (the bottom 1-2 px for a bottom dock)
2. Wait for the show delay
3. Screenshot — verify dock slides back in
4. Move the pointer away from the dock

**Expected:**
- Pointer in the edge trigger strip shows the dock with slide-in animation
- Pointer just above the trigger strip leaves the dock hidden
- Dock remains visible while the pointer is in the dock area (setHovered triggers), and hides again after it leaves

**Verification:** screenshot / AT-SPI `showing` (dock visible after edge approach)

**Automated:** tests/appium/test_07_visibility.py::test_vis003_auto_hide_shows_on_screen_edge_approach (real fake-input pointer to the bottom-edge trigger strip)

---

## TC VIS-004: Dodge Windows Mode

**Precondition:** Visibility mode set to DodgeWindows. One app window open but not overlapping dock.
**Steps:**
1. Screenshot — verify dock visible (no overlap)
2. Move/resize window to overlap dock area
3. Wait 500ms
4. Screenshot — verify dock hidden
5. Move window away from dock area
6. Wait 500ms
7. Screenshot — verify dock visible again

**Expected:**
- Dock hides when ANY window overlaps its area
- Dock shows when no windows overlap

**Verification:** screenshot (hide/show based on window overlap)

**Automated:** tests/appium/test_07_visibility.py::test_vis004_dodge_windows_hides_while_a_window_overlaps_the_dock, tests/appium/test_07_visibility.py::test_vis004_dodge_windows_hides_for_an_inactive_overlapping_window

---

## TC VIS-005: Dodge Active Window Only (Smart Hide)

**Precondition:** Visibility mode set to Dodge windows with "Only dodge active window" on. Two app windows open.
**Steps:**
1. Activate window that does NOT overlap dock
2. Wait 500ms
3. Screenshot — verify dock visible
4. Activate window that DOES overlap dock area
5. Wait 500ms
6. Screenshot — verify dock hidden

**Expected:**
- Dock hides only when the ACTIVE window overlaps its area
- Inactive windows overlapping dock do not trigger hide

**Verification:** screenshot (hide only for active overlap)

**Automated:** tests/appium/test_07_visibility.py::test_vis005_smart_hide_hides_only_for_the_active_overlapping_window (SmartHide = VisibilityMode=2 + DodgeActiveOnly=true)

---

## TC VIS-006: Keyboard Navigation Locks Visibility

**Precondition:** Visibility mode set to AutoHide. Dock currently hidden.
**Steps:**
1. Press Meta+Alt+D (keyboard navigation entry)
2. Wait 500ms
3. Screenshot — verify dock is visible
4. Wait 5 seconds (longer than auto-hide timeout)
5. Screenshot — verify dock STILL visible (keyboard lock active)
6. Press Escape (exit keyboard nav)
7. Move the pointer away from dock
8. Wait for hide timeout
9. Screenshot — verify dock hides after keyboard nav ends

**Expected:**
- Dock forced visible during keyboard navigation regardless of visibility mode
- After keyboard nav ends, normal auto-hide behavior resumes

**Verification:** screenshot (visible during keyboard, hidden after escape + timeout)

**Automated:** tests/appium/test_07_visibility.py::test_vis006_keyboard_navigation_keeps_auto_hide_dock_visible

---

## TC VIS-007: Show Desktop Keeps the Dock

**Precondition:** Visibility mode set to AlwaysVisible. One app window open (e.g., kcalc).
**Steps:**
1. Screenshot — verify dock and app window visible
2. Press Meta+D (or call `org.kde.KWin.showDesktop true` on `/KWin`, the same path)
3. Wait 500ms
4. Screenshot — verify app window hidden, dock still visible
5. Leave Show Desktop (Meta+D again, or `showDesktop false`)

**Expected:**
- KWin treats the dock surface as a Dock (layer-shell namespace `dock`), so Show Desktop leaves it on screen like a Plasma panel (#16)

**Verification:** screenshot (dock visible while the desktop is shown). Automated: `krema_showdesktop_tests` (tests/kwin).

---

## TC VIS-008: Explicit previews release visibility holds

**Precondition:** Use each of AutoHide, DodgeWindows, and SmartHide. In a
two-output session also use Follow active screen with its existing mouse and
focus triggers.

**Steps:**
1. Start with the dock hidden because of the selected visibility mode.
2. Open the explicit group preview three times.
3. Close it by leaving or by selecting a thumbnail.
4. Wait for the existing hide delay, then reveal and leave the dock again.

**Expected:**
- The popup and its pointer hold keep the dock usable while visible.
- Closing or leaving the popup releases the hold.
- AutoHide, DodgeWindows, SmartHide, and follow-screen interaction resume
  their existing hide/reveal behavior.
- The repeated popup does not leave a dock or hidden secondary output stuck
  visible.

**Verification:** AT-SPI showing state, KWin dock/window geometry, and
two-output screen placement. Visual popup checks require DRM/vgem capture.
**Automated (Tier 2):** `tests/appium/test_07_visibility.py::test_clk012_repeated_explicit_preview_releases_visibility_hold`
(six cases) and
`tests/appium/test_07_visibility.py::test_clk012_repeated_explicit_preview_releases_follow_active_screen_hold`
(four `outputs(2)` cases).

Run the multi-screen cases with:
`KREMA_E2E_OUTPUT_COUNT=2 tests/appium/run-e2e.sh -m outputs`.

---

## TC VIS-009: Optional reservation reflows maximized windows on every edge

**Precondition:** A real KWin session with one output and no other panel
reserving space. Launch a fixture window, maximize it in KWin, and verify
its maximized state and frame geometry. Use `Always visible`. Keep this
same maximized window alive throughout each toggle.

**Steps:**
1. Open Settings → Behavior. At the bottom edge, toggle `Reserve screen
   space` off and on. Record the output bounds and the existing fixture's
   settled frame bounds after each toggle, with the window identity and
   maximized state.
2. With reservation off, verify the frame reaches the output's bottom edge.
   With it on, verify the frame ends before the resting panel bar, including
   the floating gap when enabled. Verify the dock remains visible in both
   states; do not accept a maximize-command echo as the result.
3. Repeat on the top, left, and right edges using the real `Screen edge`
   picker on the Layout & Position page. For top/left, compare the fixture frame's leading edge with the
   output's leading edge; for bottom/right, compare its trailing edge.
4. Repeat each edge with floating mode off and on. Change icon size on the
   Icons page while reservation is enabled and verify the existing maximized
   frame updates to the current panel-bar thickness. Move the pointer away
   before sampling; hover zoom/tooltip surface space is not reserved.
5. Disable reservation, restart Krema, and observe the same maximized
   fixture using the full output. Enable reservation, restart again, and
   verify the saved setting restores reserved geometry. Check the Behavior
   switch state after each restart.
6. Save each reservation value in turn and switch through `Auto hide` and
   `Dodge windows`. Close Settings and move the pointer away. Verify the
   maximized frame can use the full output in both modes; reveal the dock
   and verify visibility/overlap behavior remains unchanged.
7. Return to `Always visible`. Verify the saved reservation value applies
   immediately and the switch reappears with the same state.

**Expected:**
- Turning reservation off/on expands/shrinks an already-maximized window
  in the live session; no window restore/remaximize or Krema restart is
  needed for the toggle itself.
- All four edges reserve only the current resting panel bar and floating
  gap, not the full zoom/tooltip surface.
- The chosen reservation setting survives restart independently of edge,
  floating mode, icon size, and visibility mode.
- Auto Hide and Dodge Windows reserve no space with either saved value.

**Verification:** KWin maximized state, window identity, output/frame bounds,
AT-SPI settings/dock state, and `kremarc`. Save geometry samples and assert
their actual differences; source calls, sent commands, and configuration
values alone are insufficient. Screenshots may supplement these checks.
Missing DRM/ScreenShot2 capture must not skip the geometry/input/persistence
path; report only the pixel checks as unavailable.
**Automated (Tier 2, native run passed):**
`tests/appium/test_08_reservation.py::test_vis009_reservation_off_maximized_window_uses_full_output`,
`test_vis009_already_maximized_window_reflows_on_live_reservation_toggle`,
and `test_vis009_live_icon_size_and_floating_update_maximized_consumer`
each run on top, bottom, left, and right edges.
`test_vis009_reservation_off_and_on_persist_and_reflow_existing_window_after_restart`
covers restart in both states on the bottom edge.
`test_vis009_visibility_policy_ignores_and_retains_reservation_preference`
covers Auto Hide/Dodge Windows on all four edges with both saved reservation
values, including full-output geometry while Settings is open, hidden-dock
geometry after it closes, and restored Always Visible geometry.
`test_vis009_fresh_config_default_reserves_screen_space_for_maximized_consumer`
covers the initially absent reservation key, checked native control,
positive reserved workarea, both hidden-mode rows, and restored Always
Visible behavior on the bottom edge.
`reservation-geometry.jsonl` records actual fixture/window identities,
frame/workarea/output bounds, and resting icon/surface bounds for reserved
states. No test requires screenshots. SET-015 maps the native control;
four-edge restart combinations beyond the bottom-edge node remain manual
acceptance checks.

These checks establish the frame/workarea/input/AT-SPI contract without
requiring screenshots or live PipeWire thumbnails. Installed-package Tier 3,
other-edge restart combinations beyond the bottom-edge node, and the user's
original desktop root cause remain unverified.
