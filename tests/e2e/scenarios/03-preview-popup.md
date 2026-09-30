# Preview Popup

## Features
- preview-hover-open: Preview popup opens on mouse hover over grouped app
- preview-thumbnail-click: Click thumbnail to activate that window
- preview-close-button: Close button on thumbnail closes the window
- preview-multi-window: Multiple thumbnails shown for grouped windows
- preview-single-window: Single thumbnail for single-window app
- preview-mouse-leave-close: Preview closes when mouse leaves
- preview-live-thumbnail: PipeWire-based live window thumbnails
- preview-fast-pointer-entry: Pointer entering a popup reported visible by AT-SPI keeps it open past the hide delay

## Affected Files
- src/qml/main.qml
- src/qml/PreviewPopup.qml
- src/qml/PreviewThumbnail.qml
- src/shell/previewcontroller.h
- src/shell/previewcontroller.cpp
- src/models/dockmodel.h
- src/models/dockmodel.cpp

## QA coverage

| Case | Path covered | Automated test |
|---|---|---|
| QA-PREV-01 | Fast entry at AT-SPI visibility, current popup/KWin coordinates, visibility past the existing hide delay | `test_qa_prev01_atspi_visible_popup_accepts_fast_pointer_entry` |
| PREV-003 / PREV-004 | Thumbnail activation and precise close-button input after layout/paint stability | `test_prev003_clicking_a_thumbnail_activates_that_window`, `test_prev004_close_button_closes_that_window` |
| PREV-004 | Keyboard Delete closes the focused window | `test_prev004_delete_key_closes_focused_thumbnail_window` |
| PREV-005 | Pointer leave closes outside the popup; configured hide delay is respected | `test_prev005_preview_closes_when_pointer_leaves`, `test_prev005_close_on_leave_is_delayed` |
| PREV-005 | A neighbouring task row re-centres the dock without reopening a preview under a stale pointer position | `test_prev005_preview_stays_closed_when_a_task_row_appears_while_leaving` |

All tests are in `tests/appium/test_03_preview.py`. The new QA-PREV-01 test
records both sides: the pre-fix run failed after a 33 ms entry, while the fixed
run passed three fresh opens. Its fast path complements, rather than replaces,
the existing pixel/layout waits.

---

## TC PREV-001: Preview Opens on Hover

**Precondition:** App with 1+ open windows. Dock visible.
**Steps:**
1. Show dock (see dock show sequence in README)
2. `mouse_move` to dock item of the running app (screen coordinates)
3. Wait 1000ms (preview trigger delay + animation)
4. `accessibility_tree app_name="krema"` — look for PopupMenu with name "Preview for <AppName>"
5. `screenshot` — verify preview popup visible with live PipeWire thumbnails

**Expected:**
- Preview popup appears above dock (dock is bottom-positioned)
- AT-SPI: `[popup menu] "Preview for <AppName>"` with `showing`, `visible` states
- Contains: `[label] "<AppName>"`, `[separator]`, `[button] "<Title>"` per window
- Each thumbnail button contains: `[button] "Close <Title>"`, `[label] "<Title>"]`
- Live PipeWire thumbnails visible in screenshot

**Verified in PoC:** Mouse hover on KCalc → "Preview for KCalc" popup with live thumbnail.

**Verification:** accessibility_tree (PopupMenu present with correct name), screenshot (popup with thumbnails)

**Automated:** tests/appium/test_03_preview.py::test_prev001_hover_opens_preview_above_dock_with_live_thumbnails

---

## TC PREV-002: Multiple Thumbnails for Grouped Windows

**Precondition:** App with 2+ open windows (e.g., 2 Dolphin windows).
**Steps:**
1. Hover over the grouped app's dock item
2. Wait 800ms
3. `accessibility_tree` — count Button elements inside PopupMenu
4. `screenshot` — verify multiple thumbnails

**Expected:**
- Number of thumbnails matches number of open windows
- Each thumbnail has `Accessible.role: Button` with window title
- Thumbnails arranged horizontally

**Verification:** accessibility_tree (button count = window count), screenshot (layout)

**Automated:** tests/appium/test_03_preview.py::test_prev002_grouped_app_shows_one_thumbnail_per_window_in_a_row

---

## TC PREV-003: Click Thumbnail Activates Window

**Precondition:** Preview popup open with multiple thumbnails (PREV-002).
**Important:** AT-SPI coordinates are surface-local. Convert to screen coordinates:
`screen_y = (600 - 400) + surface_y = 200 + surface_y` (preview surface is 800x400).
**Steps:**
1. Note window title of second thumbnail from accessibility_tree
2. Get thumbnail surface coordinates (e.g., (528, 226, 208x166))
3. Convert center to screen: `screen = (632, 200 + 309) = (632, 509)`
4. `mouse_click(screen_x, screen_y)` on the second thumbnail
5. Wait 500ms
6. `accessibility_tree` — check preview popup closed (0x0 size)

**Expected:**
- Clicked window becomes active/focused
- Preview popup closes after activation (popup reverts to 0x0)
- Dock may auto-hide (SmartHide)

**Limitation:** `list_windows` cannot verify which window is active (D-01).

**Verified in PoC:** Thumbnail click at screen-converted coordinates activated window
and closed preview.

**Verification:** accessibility_tree (popup 0x0), screenshot (correct window in foreground)

**Automated:** tests/appium/test_03_preview.py::test_prev003_clicking_a_thumbnail_activates_that_window

---

## TC PREV-004: Close Button Closes Window

**Precondition:** Preview popup open with at least 2 thumbnails.
**Note:** Close button is very small (22x22). Mouse click requires precise coordinate
conversion. **Preferred alternative: use keyboard Delete key (TC KBD-007).**
**Steps (mouse — difficult):**
1. `accessibility_tree` — find "Close <title>" button surface coordinates (22x22)
2. Convert to screen: `screen_y = 200 + surface_y`
3. Move mouse from dock → preview gradually (20px steps, avoid triggering hidePreviewDelayed)
4. `mouse_click` at exact close button center
5. Wait 500ms
6. `list_windows` — verify window count decreased

**Steps (keyboard — preferred):**
1. Open preview via keyboard (Down arrow from dock item in keyboard mode)
2. Navigate to target thumbnail (Left/Right arrows)
3. `keyboard_key Delete`
4. Wait 500ms
5. `list_windows` — verify window count decreased
6. `accessibility_tree` — verify thumbnail removed

**Expected:**
- Window is closed
- Thumbnail removed from preview
- If 1+ windows remain, preview stays open with remaining thumbnails
- If 0 windows remain, preview closes and keyboard returns to dock

**Verification:** list_windows (window count decreased), accessibility_tree (thumbnail removed)

**Automated:** tests/appium/test_03_preview.py::test_prev004_close_button_closes_that_window, tests/appium/test_03_preview.py::test_prev004_delete_key_closes_focused_thumbnail_window, tests/appium/test_03_preview.py::test_prev004_closing_last_window_closes_preview_and_returns_to_dock

---

## TC PREV-005: Preview Closes on Mouse Leave

**Precondition:** Preview popup open.
**Steps:**
1. Verify preview popup visible (accessibility_tree — popup has `showing` state)
2. `mouse_move` far away from dock and preview (e.g., center of screen y=300)
3. Wait 500ms (close delay)
4. `accessibility_tree` — verify popup reverted to 0x0 (hidden state)

**Expected:**
- Preview popup closes after mouse leaves both dock item and preview area
- Close has a small delay (hidePreviewDelayed, not instant)
- Popup element remains in AT-SPI tree but with 0x0 size and no `showing` state

**Note:** Moving mouse from dock to preview must be gradual (20px steps).
Direct jump to a distant point will trigger hidePreviewDelayed immediately.

**Verification:** accessibility_tree (popup 0x0, no `showing`)

**Automated:** tests/appium/test_03_preview.py::test_prev005_preview_closes_when_pointer_leaves, tests/appium/test_03_preview.py::test_prev005_close_on_leave_is_delayed

---

## QA-PREV-01: Fast Pointer Entry After AT-SPI Visibility

**Precondition:** A running app with one or more windows. Dock visible.
**Steps:**
1. Before hovering, look up the in-process AT-SPI popup and its already mapped
   KWin preview surface, avoiding webdriver tree lookups in the timed path.
2. Hover the running app's dock item and poll the popup every 5 ms until it has
   `showing` and `visible` states and a non-zero rect inside that surface.
3. Convert the current popup centre to screen coordinates using the KWin
   surface origin; do not wait for settled geometry or painted pixels.
4. Move there immediately, followed by one short motion within the same rect.
   Both input events must finish within 190 ms of the first observed visibility
   to exercise the recorded visibility-to-first-frame race window.
5. Keep the pointer there and assert that the popup remains visible for another
   500 ms, beyond the hide delay.
6. Repeat three fresh preview opens.

**Expected:**
- The current popup/surface coordinate is valid when derived from AT-SPI and
  KWin geometry.
- Fast pointer entry from the dock reaches the popup's active input region.
- The popup remains visible past the hide delay while the pointer rests inside.

**Verification:** AT-SPI `showing`/`visible` states and geometry, KWin preview
surface geometry, real pointer input, and a bounded visibility hold. This
covers the fast AT-SPI-visible-to-pointer-entry path only; it does not claim
stationary-pointer recovery or that every timing race is eliminated.

**Note:** This regression intentionally does not call
`preview.wait_on_screen`: the existing thumbnail-click, close-button, and
outside-close tests retain that wait for layout and painted-pixel stability.

**Automated:** tests/appium/test_03_preview.py::test_qa_prev01_atspi_visible_popup_accepts_fast_pointer_entry

---

## TC PREV-006: Single Window Preview

**Precondition:** App with exactly 1 open window.
**Steps:**
1. Hover over that app's dock item
2. Wait 800ms
3. `accessibility_tree` — verify popup with 1 thumbnail
4. `screenshot`

**Expected:**
- Preview shows single thumbnail with window title
- Close button present on the thumbnail
- Click activates the single window

**Verification:** accessibility_tree (1 Button in PopupMenu), screenshot

**Automated:** tests/appium/test_03_preview.py::test_prev006_single_window_preview

---

## TC PREV-007: Preview Popup Accessible Announce

**Precondition:** Screen reader support active (Accessible.announce available, Qt >= 6.8).
**Steps:**
1. Hover to trigger preview popup
2. Check announcement via accessibility_tree or log

**Expected:**
- `Accessible.announce` fires with preview count message
  (e.g., "2 windows for Dolphin")

**Verification:** accessibility_tree (announcement text), logs

**Automated:** tests/appium/test_03_preview.py::test_prev007_opening_preview_announces_window_count
