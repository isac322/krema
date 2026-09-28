---
description: "Apply when building settings or other Kirigami UI, choosing colors or spacing, adding configuration options (KConfigXT), exposing config to QML, or adding user-facing strings."
---

# KDE UI and Configuration

## Settings UI
- Settings pages use `FormCard` (`org.kde.kirigamiaddons.formcard`): `FormCardPage` + `FormHeader` + `FormCard` + built-in delegates.
- If no built-in delegate fits, extend `AbstractFormDelegate`. Do not hand-build `GridLayout` + `QQC2.Label` forms.

## Color and theming
- No hardcoded colors; use `Kirigami.Theme`.
- Spacing and sizes come from `Kirigami.Units` (`gridUnit`, `smallSpacing`, `largeSpacing`).

## Configuration
- Persist settings with KConfigXT (`.kcfg` schema, generated `KremaSettings` class).
- `KConfigSkeleton` with `Singleton=false` does not call `load()` in its constructor; call `load()` manually.
- `qmlRegisterSingletonInstance` is reachable from one QML engine only (a second engine gets `null`). With multiple engines (dock + settings window) use `qmlRegisterSingletonType` with a factory callback and `CppOwnership`.

## Translation
- Wrap every user-facing string in `i18n()`.
