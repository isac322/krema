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

---

## TC CTX-001: Right-Click Opens Context Menu

**Precondition:** Dock visible with a running app.
**Limitation:** QMenu is NOT exposed in AT-SPI (kwin-mcp D-06). All menu interactions
use screenshot-based coordinate estimation.
**Steps:**
1. Show dock and move mouse to target item (see dock show sequence in README)
2. `mouse_click(x, y, button="right")` on a dock item (screen coordinates)
3. Wait 300ms
4. `screenshot` — verify context menu visible

**Expected:**
- Native KDE context menu appears (QMenu / Breeze styled)
- Menu header shows app name
- Menu items (top to bottom): AppName, Pin/Unpin, New Instance, Close,
  separator, Settings..., About Krema, Quit

**Menu item coordinate estimation** (from PoC, approximate y-offsets from menu top):
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
1. `list_windows` — count app windows
2. Right-click on the app's dock item
3. Click "New Instance"
4. Wait 2000ms
5. `list_windows` — verify window count increased

**Expected:**
- New app window launched
- Bounce animation on dock icon

**Verification:** list_windows (count +1)
**Automated:** tests/appium/test_04_context_menu.py::test_ctx004_new_instance_launches_another_window

---

## TC CTX-005: Close All Windows

**Precondition:** App with 2+ open windows.
**Steps:**
1. `list_windows` — note app windows
2. Right-click on the app's dock item
3. Click "Close"
4. Wait 1000ms
5. `list_windows` — verify all windows of that app are closed

**Expected:**
- All windows of the app are closed
- If app was pinned, icon remains (no indicator)
- If app was not pinned, icon removed

**Verification:** list_windows (no windows for that app)
**Automated:** tests/appium/test_04_context_menu.py::test_ctx005_close_closes_every_window_of_an_unpinned_app, tests/appium/test_04_context_menu.py::test_ctx005_close_keeps_a_pinned_app_without_indicator

---

## TC CTX-006: Settings Opens Dialog

**Precondition:** Dock visible.
**Steps:**
1. Right-click on any dock item (CTX-001 steps)
2. `screenshot` — identify "Settings..." position in menu
3. `mouse_click` at "Settings..." y-offset (~+123px from menu top)
4. Wait 1500ms (settings window creation)
5. `list_windows` — find settings window (krema window count increases)
6. `screenshot` — verify settings dialog
7. `find_ui_elements query="Icon size"` — verify FormCard elements in AT-SPI

**Expected:**
- Kirigami-based settings dialog opens as separate window
- Contains pages: Appearance, Behavior, Window Preview, About Krema, About KDE
- FormCard-based layout with sliders, spinboxes, comboboxes
- Settings UI is fully accessible via AT-SPI (PoC verified)

**Verified in PoC:** Settings opened successfully. `find_ui_elements` found
"Icon size" label and "Zoom factor" slider with Increase/Decrease actions.

**Verification:** list_windows (krema window count +1), find_ui_elements (FormCard widgets), screenshot (dialog layout)

**Automated:** tests/appium/test_04_context_menu.py::test_ctx006_settings_entry_opens_the_settings_window
