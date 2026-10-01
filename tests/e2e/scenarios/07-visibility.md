# Visibility Control

## Features
- vis-always-visible: Dock always shown regardless of windows
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
- src/qml/main.qml
- src/config/krema.kcfg
**Tier:** Tier 2 (Appium) for real visibility, KWin, and multi-output state.
Tier 3 reuses the Appium cases against installed packages.
Keyboard navigation still locks visibility, and context-menu actions remain
unchanged; VIS-008 checks only release of the explicit-preview hold.



---

## TC VIS-001: Always Visible Mode

**Precondition:** Visibility mode set to AlwaysVisible.
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
- Windows do not cover the dock (layer-shell exclusive zone)

**Verification:** screenshot (dock visible in all states)

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
