---
description: "Apply when building settings or other Kirigami UI, choosing colors or spacing, adding configuration options (KConfigXT), exposing config to QML, or adding user-facing strings."
---

# KDE UI and Configuration

## Settings UI
- Settings pages use `FormCard` (`org.kde.kirigamiaddons.formcard`): `FormCardPage` + `FormHeader` + `FormCard` + built-in delegates.
- If no built-in delegate fits, extend `AbstractFormDelegate`. Do not hand-build `GridLayout` + `QQC2.Label` forms.
- New or changed settings follow the principles in `docs/settings-redesign-prd.md` (macOS System Settings-level UX): every setting gets visual context (the page's desktop stage preview or live-apply to the real dock, 60 fps while dragging), common choices are direct-manipulation controls or presets, and exact numeric inputs and fine-tuning go behind a "Custom"/advanced disclosure.
- Previews must look like the real dock (real theme icons, the dock's own shaders; a 3D scene where depth matters, as for shadows). Do not substitute flat 2D diagrams or web-style layouts.

## Color and theming
- No hardcoded colors; use `Kirigami.Theme`.
- Spacing and sizes come from `Kirigami.Units` (`gridUnit`, `smallSpacing`, `largeSpacing`).

## Configuration
- Persist settings with KConfigXT (`.kcfg` schema, generated `KremaSettings` class).
- `KConfigSkeleton` with `Singleton=false` does not call `load()` in its constructor; call `load()` manually.
- `qmlRegisterSingletonInstance` is reachable from one QML engine only (a second engine gets `null`). With multiple engines (dock + settings window) use `qmlRegisterSingletonType` with a factory callback and `CppOwnership`.

## Translation
- Wrap every user-facing string in `i18n()`.
