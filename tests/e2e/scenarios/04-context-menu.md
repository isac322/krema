# Context Menu

## Features
- ctx-pin-unpin: Toggle pin/unpin app in dock
- ctx-new-instance: Launch new instance from menu
- ctx-close: Close all windows of the app
- ctx-settings: Open settings dialog from menu
- ctx-quit: Quit Krema from menu
- ctx-app-name: App name shown as header in menu

## Affected Files
- src/models/dockcontextmenu.h
- src/models/dockcontextmenu.cpp
- src/models/dockactions.h
- src/models/dockactions.cpp
- src/qml/main.qml
- src/shell/settingswindow.h
- src/shell/settingswindow.cpp
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
- Menu items (top to bottom): AppName, Pin/Unpin, New Instance, Close,
  separator, Settings..., About Krema, Quit

**Menu item positions** (approximate y-offsets from menu top):
- AppName: +0px
- Pin to Dock / Unpin: +25px
- New Instance: +57px
- Close: +90px
- Settings...: +123px
- About Krema: +158px
- Quit: +180px

**Verification:** screenshot only (menu visible with expected entries)
**Automated:** tests/appium/test_04_context_menu.py::test_ctx001_right_click_opens_native_menu_at_the_item, tests/appium/test_04_context_menu.py::test_ctx001_about_krema_is_the_fifth_entry, tests/appium/test_04_context_menu.py::test_ctx001_quit_is_the_last_entry (entry order is verified by effect for every enabled position; the header label text itself is not read since QMenu is not in AT-SPI — its presence as a disabled row is verified by the keyboard highlight skipping it)

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
- Kirigami-based settings dialog opens as separate window
- Contains pages: Appearance, Behavior, Window Preview, About Krema, About KDE
- FormCard-based layout with sliders, spinboxes, comboboxes
- Settings UI is fully accessible via AT-SPI

**Verification:** window list (krema window count +1), AT-SPI (FormCard widgets), screenshot (dialog layout)

**Automated:** tests/appium/test_04_context_menu.py::test_ctx006_settings_entry_opens_the_settings_window
