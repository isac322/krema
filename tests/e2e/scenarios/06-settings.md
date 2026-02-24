# Settings UI

## Features
- settings-appearance: Icon size, zoom factor, spacing, opacity, background style
- settings-behavior: Visibility mode, dock position
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
- src/shell/settingswindow.h
- src/shell/settingswindow.cpp
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

**Expected:**
- Settings dialog opens as separate window ("설정 — Krema" title)
- Left sidebar: Appearance, Behavior, Window Preview, About Krema, About KDE
- Appearance page shown by default
- FormCard layout with spinboxes, sliders, comboboxes
- All controls accessible via AT-SPI (labels, sliders with Increase/Decrease)

**Verified in PoC:** Settings opened. Found "Icon size" label and
"Zoom factor" slider with Increase/Decrease actions in AT-SPI.

**Verification:** list_windows (window count +1), find_ui_elements (FormCard widgets), screenshot

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
- Zoom proportions adjust accordingly

**Verification:** screenshot comparison (icon size changed)

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
