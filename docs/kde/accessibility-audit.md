# Krema Accessibility Audit

Initial audit: 2026-02-24. Implementation completed same day.

## Summary (Post-Implementation)

Krema now has comprehensive accessibility support across all dock UI components.

## Audit Results

| Area | Status | Notes |
|------|--------|-------|
| QML Accessible properties | :white_check_mark: Complete | Role/name/description on root, DockItem, PreviewPopup, PreviewThumbnail |
| Keyboard navigation | :white_check_mark: Complete | Meta+D focus entry, arrow keys, Enter/Space activate, Escape exit |
| Screen reader support | :white_check_mark: Complete | Accessible.announce() for navigation, launch, preview open |
| Focus management | :white_check_mark: Complete | keyboardNavigating state, forceActiveFocus, dock↔preview focus transfer |
| Visual focus indicator | :white_check_mark: Complete | Focus ring on DockItem and PreviewThumbnail (keyboard nav only) |
| High contrast | :warning: Partial | Uses Kirigami.Theme colors; focus ring uses Kirigami.Theme.focusColor |
| Font scaling | :warning: Partial | Kirigami.Theme.smallFont used; dock icons are fixed-size by design |
| Animation control | :white_check_mark: Complete | Bounce uses Kirigami.Units.longDuration (respects "reduce animations") |
| i18n | :white_check_mark: Complete | All user-facing strings use i18n() |
| Settings UI | :warning: Partial | FormCard provides baseline accessibility; no dedicated accessibility page |

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Meta+D | Focus dock for keyboard navigation |
| Left/Right | Navigate dock items |
| Enter/Space | Activate focused item |
| Down | Open preview for focused item |
| Escape | Exit keyboard navigation / return from preview |
| Menu | Open context menu for focused item |
| Left/Right (in preview) | Navigate preview thumbnails |
| Enter (in preview) | Activate focused window |
| Delete (in preview) | Close focused window |
| Up/Escape (in preview) | Return to dock |

## Files Modified

| File | Changes |
|------|---------|
| `src/qml/main.qml` | Accessible.role/name, keyboardNavigating state, Keys handler, navigateItem(), announce() |
| `src/qml/DockItem.qml` | Accessible.role/name/description, isKeyboardFocused, focusRing, longDuration bounce |
| `src/qml/PreviewPopup.qml` | Accessible.role/name, keyboard nav, announcements, focus transfer |
| `src/qml/PreviewThumbnail.qml` | Accessible.role/name, isKeyboardFocused, focusRing, close button label |
| `src/app/application.cpp` | Meta+D global shortcut registration |
| `src/shell/dockshell.h/.cpp` | focusDock(), preview→dock focus return connection |
| `src/shell/dockvisibilitycontroller.h/.cpp` | setKeyboardActive() — keyboard-aware hide prevention |
| `src/shell/previewcontroller.h/.cpp` | requestKeyboardFocus(), returnFocusToDock(), keyboardFocusReturned signal |
| `CMakeLists.txt` | QT_MIN_VERSION 6.6.0 → 6.8.0 (for Accessible.announce) |
