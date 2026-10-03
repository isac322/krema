# Preview Popup

## Features
- preview-hover-open: Preview popup opens on mouse hover over grouped app
- preview-explicit-click: Group1 opens a popup on click independently of hover
- preview-click-target: The selected thumbnail activates/restores only its window
- preview-pending-hide: Re-entry retargets the pending popup hide
- preview-thumbnail-click: Click thumbnail to activate that window
- preview-close-button: Close button on thumbnail closes the window
- preview-multi-window: Multiple thumbnails shown for grouped windows
- preview-single-window: Single thumbnail for single-window app
- preview-mouse-leave-close: Preview closes when mouse leaves
- preview-live-thumbnail: PipeWire-based live window thumbnails
- preview-fast-pointer-entry: Pointer entering a popup reported visible by AT-SPI keeps it open past the hide delay
- preview-task-index: Preview thumbnails keep their initial displayed order and exact native targets after pinned/running partitioning

## Affected Files
- src/qml/main.qml
- src/config/krema.kcfg
- src/models/dockactions.h
- src/models/dockactions.cpp
- src/qml/PreviewPopup.qml
- src/qml/PreviewThumbnail.qml
- src/shell/previewcontroller.h
- src/shell/previewcontroller.cpp
- src/models/dockmodel.h
- src/models/dockmodel.cpp

**Tier:** Tier 2 (Appium) for real pointer, AT-SPI, KWin, and PipeWire
observations. Tier 1 QML tests cover tooltip suppression and popup lifecycle
seams. Tier 3 reuses the Appium cases against installed packages.
The existing keyboard preview and context-menu paths remain covered by
scenarios 01 and 04; this file adds only the explicit mouse-preview path.

## QA coverage

| Case | Path covered | Automated test |
|---|---|---|
| QA-PREV-01 | Fast entry at AT-SPI visibility, current popup/KWin coordinates, visibility past the existing hide delay | `test_qa_prev01_atspi_visible_popup_accepts_fast_pointer_entry` |
| PREV-003 / PREV-004 | Thumbnail activation and precise close-button input after layout/paint stability | `test_prev003_clicking_a_thumbnail_activates_that_window`, `test_prev004_close_button_closes_that_window` |
| PREV-004 | Keyboard Delete closes the focused window | `test_prev004_delete_key_closes_focused_thumbnail_window` |
| PREV-005 | Pointer leave closes outside the popup; configured hide delay is respected | `test_prev005_preview_closes_when_pointer_leaves`, `test_prev005_close_on_leave_is_delayed` |
| PREV-005 | A neighbouring task row re-centres the dock without reopening a preview under a stale pointer position | `test_prev005_preview_stays_closed_when_a_task_row_appears_while_leaving` |
| PREV-001 / VIS-009 | Reservation ON/OFF and four-edge inward popup geometry relative to the resting dock item, including Floating ON; geometry-only coverage with no DRM thumbnail-pixel assertion | `tests/appium/test_08_reservation.py::test_prev_reservation_hover_popup_stays_inward_of_resting_dock_item` |
| PREV-011 | Two/three default thumbnails fit the native Left/Right surface; last-thumbnail clicks select the exact client rather than the window underneath; live group/single reuse; horizontal control | `test_11_preview_surface.py::test_prev011_grouped_preview_surface_tracks_two_three_and_single_layouts` |

Existing preview behavior tests are in `tests/appium/test_03_preview.py`. The
reservation preservation regression is in `tests/appium/test_08_reservation.py`,
and PREV-011 surface containment is in `tests/appium/test_11_preview_surface.py`.
QA-PREV-01 records both sides: the pre-fix run failed after a 33 ms entry,
while the fixed run passed three fresh opens. Its fast path complements,
rather than replaces, the existing pixel/layout waits.

---

## TC PREV-001: Preview Opens on Hover

**Precondition:** App with 1+ open windows. Dock visible.
**Steps:**
1. Show dock (README "Showing a hidden dock for mouse tests")
2. Move the pointer to the running app's dock item
3. Wait 1000ms (preview trigger delay + animation)
4. Check the krema AT-SPI tree — look for PopupMenu with name "Preview for <AppName>"
5. Screenshot — verify preview popup visible with live PipeWire thumbnails

**Expected:**
- Preview popup appears above dock (dock is bottom-positioned)
- AT-SPI: `[popup menu] "Preview for <AppName>"` with `showing`, `visible` states
- Contains: `[label] "<AppName>"`, `[separator]`, `[button] "<Title>"` per window
- Each thumbnail button contains: `[button] "Close <Title>"`, `[label] "<Title>"]`
- Live PipeWire thumbnails visible in screenshot

**Verification:** AT-SPI (PopupMenu present with correct name), screenshot (popup with thumbnails)

**Automated:** tests/appium/test_03_preview.py::test_prev001_hover_opens_preview_above_dock_with_live_thumbnails
**Automated (ICON-008 raw fallback):** `tests/appium/test_03_preview.py::test_icon008_minimized_preview_fallback_preserves_raw_artwork` (requires a DRM-capable KWin capture session)
**Automated (reservation regression, native Tier 2 coverage):** `tests/appium/test_08_reservation.py::test_prev_reservation_hover_popup_stays_inward_of_resting_dock_item` (geometry-only; no DRM thumbnail-pixel assertion)

---

## TC PREV-002: Multiple Thumbnails for Grouped Windows

**Precondition:** App with 2+ open windows (e.g., 2 Dolphin windows).
**Steps:**
1. Hover over the grouped app's dock item
2. Wait 800ms
3. Check the AT-SPI tree — count Button elements inside PopupMenu
4. Screenshot — verify multiple thumbnails

**Expected:**
- Number of thumbnails matches number of open windows
- Each thumbnail has `Accessible.role: Button` with window title
- Thumbnails arranged horizontally

**Verification:** AT-SPI (button count = window count), screenshot (layout)

**Automated:** tests/appium/test_03_preview.py::test_prev002_grouped_app_shows_one_thumbnail_per_window_in_a_row

---

## TC PREV-003: Click Thumbnail Activates Window

**Precondition:** Preview popup open with multiple thumbnails (PREV-002).
**Important:** AT-SPI coordinates are surface-local; convert them to screen coordinates (README pattern 5).
**Steps:**
1. Note window title of second thumbnail from the AT-SPI tree
2. Convert the second thumbnail's center to screen coordinates
3. Move the pointer from the dock onto the popup gradually (README pattern 8) and click the second thumbnail
4. Wait 500ms
5. Check the AT-SPI tree — preview popup closed (0x0 size)

**Expected:**
- Clicked window becomes active/focused
- Preview popup closes after activation (popup reverts to 0x0)
- Dock may auto-hide (Auto hide / Dodge windows)

**Verification:** AT-SPI (popup 0x0), active window (clicked window in foreground)

**Automated:** tests/appium/test_03_preview.py::test_prev003_clicking_a_thumbnail_activates_that_window

---

## TC PREV-004: Close Button Closes Window

**Precondition:** Preview popup open with at least 2 thumbnails.
**Note:** Close button is small (22x22); the click needs precise coordinate
conversion. The keyboard path (TC KBD-007) is an alternative.
**Steps (mouse):**
1. Find the "Close <title>" button in the AT-SPI tree (22x22) and convert its center to screen coordinates
2. Move the pointer from the dock onto the preview gradually (README pattern 8)
3. Click the close button center
4. Wait 500ms
5. Check the window list — window count decreased

**Steps (keyboard):**
1. Open preview via keyboard (Down arrow from dock item in keyboard mode)
2. Navigate to target thumbnail (Left/Right arrows)
3. Press Delete
4. Wait 500ms
5. Check the window list — window count decreased
6. Check the AT-SPI tree — thumbnail removed

**Expected:**
- Window is closed
- Thumbnail removed from preview
- If 1+ windows remain, preview stays open with remaining thumbnails
- If 0 windows remain, preview closes and keyboard returns to dock

**Verification:** window list (window count decreased), AT-SPI (thumbnail removed)

**Automated:** tests/appium/test_03_preview.py::test_prev004_close_button_closes_that_window, tests/appium/test_03_preview.py::test_prev004_delete_key_closes_focused_thumbnail_window, tests/appium/test_03_preview.py::test_prev004_closing_last_window_closes_preview_and_returns_to_dock

---

## TC PREV-005: Preview Closes on Mouse Leave

**Precondition:** Preview popup open.
**Steps:**
1. Verify preview popup visible (popup has `showing` state in the AT-SPI tree)
2. Move the pointer far away from dock and preview (e.g., center of screen)
3. Wait 500ms (close delay)
4. Check the AT-SPI tree — popup reverted to 0x0 (hidden state)

**Expected:**
- Preview popup closes after mouse leaves both dock item and preview area
- Close has a small delay (hidePreviewDelayed, not instant)
- Popup element remains in AT-SPI tree but with 0x0 size and no `showing` state

**Note:** Moving the pointer from dock to preview must be gradual (README pattern 8).
A direct jump to a distant point triggers hidePreviewDelayed immediately.

**Verification:** AT-SPI (popup 0x0, no `showing`)

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
3. Check the AT-SPI tree — popup with 1 thumbnail
4. Screenshot

**Expected:**
- Preview shows single thumbnail with window title
- Close button present on the thumbnail
- Click activates the single window

**Verification:** AT-SPI (1 Button in PopupMenu), screenshot

**Automated:** tests/appium/test_03_preview.py::test_prev006_single_window_preview

---

## TC PREV-007: Preview Popup Accessible Announce

**Precondition:** Screen reader support active (Accessible.announce available, Qt >= 6.8).
**Steps:**
1. Hover to trigger preview popup
2. Check the announcement (AT-SPI announcement event or log)

**Expected:**
- `Accessible.announce` fires with preview count message
  (e.g., "2 windows for Dolphin")

**Verification:** AT-SPI (announcement text), logs

**Automated:** tests/appium/test_03_preview.py::test_prev007_opening_preview_announces_window_count

---

## TC PREV-008: Explicit group click shows all thumbnails

**Precondition:** Grouped-window action `Show window previews`, hover previews
disabled, and a grouped app with at least three child windows.

**Steps:**
1. Click the grouped dock item.
2. Confirm every child title appears in the popup without activating the group.
3. Click a minimized child thumbnail.
4. Inspect the selected and unselected children after the popup closes.

**Expected:**
- One popup shows all child thumbnails.
- The selected thumbnail activates and restores only its target.
- Other children keep their existing minimized state.
- Repeated explicit clicks keep one popup.

**Verification:** AT-SPI popup and thumbnail state, KWin active/minimized
state, and live thumbnail pixels. PipeWire/Screenshot2 checks require DRM/vgem.
**Automated (Tier 2):** `tests/appium/test_03_preview.py::test_prev008_explicit_group_click_shows_all_thumbnails_and_selected_child_closes`.

---

## TC PREV-009: Re-entering retargets a pending explicit-preview hide

**Precondition:** Grouped-window action `Show window previews`; two grouped
apps; the first popup is open and its hide delay is pending.

**Steps:**
1. Move from the first group toward the second group and let the first hide
   remain pending.
2. Re-enter the second group and click it.
3. Wait beyond the first hide delay, then select a thumbnail in the second
   popup.

**Expected:**
- The second explicit click retargets the visible popup.
- The first pending hide does not close the second popup.
- The selected second-group child activates and the popup closes.

**Verification:** AT-SPI popup visibility, thumbnail titles, and KWin active
state. PipeWire/Screenshot2 checks require DRM/vgem.
**Automated (Tier 2):** `tests/appium/test_03_preview.py::test_prev009_explicit_group_pending_hide_retargets_after_reenter`.

---

## TC PREV-010: Grouped Preview Order and Native Targets Survive Partitioning

**Precondition:** Enable `Separate pinned and running apps`. Use a grouped app with three uniquely titled open windows for the bottom dock, or two for the left dock. Keep another pinned running app and another unpinned running app present so both task zones are populated.

**Steps:**
1. Use `list_windows` and the fixture records to identify the grouped app's exact window count and unique `(PID, internal_id, title)` identities. KWin enumeration supplies the identity set, not the preview order; its stacking order and fixture creation order are not a TasksModel child-order oracle.
2. Open the grouped app's preview after the dock has settled. Check that its thumbnail count and unique titles match the independently recorded windows, then record the displayed left-to-right identity sequence from `accessibility_tree` as the initial baseline.
3. Hover the unpinned app across the divider, then return to the group. Require the same full baseline sequence without recapturing or sorting it.
4. For every displayed slot, make the unpinned app active and verify its exact native identity. Click its dock item only if that window is not already active, to avoid toggling minimization. Reopen the grouped preview, retarget across the divider and back, and require the unchanged baseline on each grouped reopen or return. Move the real pointer to that slot's thumbnail center, including the last displayed slot, and click.
5. Unpin the other pinned running app and wait for the dock partition to reconcile. Repeat every slot selection against the original baseline.
6. Repin that other app, wait for reconciliation, and repeat every slot selection against the same original baseline.

**Expected:**
- The preview has exactly one thumbnail for each independently identified group window, with no missing or duplicate title or native identity.
- Every grouped reopen and return preserves the initial displayed identity sequence, even when another app's pin transition changes the group's dock slot.
- Every physical thumbnail click activates that slot's exact `(PID, internal_id, title)` identity and closes the popup; it never activates an adjacent task or a different child in the group.
- Every selected thumbnail center lies inside both the native input surface and the interactive popup. The last displayed slot is selected by its baseline identity, not by fixture creation order.
- Pin/unpin transitions preserve the group identities, correct pinned launcher membership, expected dock order, and populated divider.

**Verification:** Independent fixture/KWin identity sets and counts, AT-SPI thumbnail names and measured left-to-right order, unchanged full baseline identities on every grouped reopen or return, real pointer coordinates, KWin active identities after every slot selection, and popup closure.
**Automated (strict feature coverage):** `tests/appium/test_10_task_zone_input.py::test_prev010_last_thumbnail_and_other_app_pin_transitions` exercises all three horizontal slots and both vertical slots before and after other-app unpin/repin transitions. It retains strict single-action vertical hover and physical last-center input reachability. The native preview surface follows the laid-out popup extent within available output space. PREV-010 checks partition order and exact native activation with center-point input reachability; PREV-011 separately verifies whole-popup and whole-thumbnail containment. Image/RHI/DRM and default timing remain unverified.
