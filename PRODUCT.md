# Product

<!-- impeccable:product-schema 1 -->

## Platform

Native desktop app: KDE Plasma 6 on Wayland only (C++23, Qt 6, KDE Frameworks 6, QML with Kirigami and Kirigami Addons). Not web, not mobile.

## Users

Plasma 6 users who want a macOS-style dock, many coming from Latte Dock. They open the settings window occasionally to tune how the dock looks and behaves, usually with the real dock visible on the same screen, then close it.

## Product Purpose

Krema is a dock for KDE Plasma 6: parabolic hover zoom, PipeWire window previews, notification badges, multi-monitor placement. The settings window must let a non-expert understand and change every setting without documentation, and let power users set exact values.

## Positioning

Spiritual successor to Latte Dock for Plasma 6 on Wayland. Never described as a replacement, fork or clone.

## Operating Context

- The settings window is a separate top-level window; every change is saved immediately (KConfigXT) and applied live to the real dock.
- The real dock stays visible while the window is open, so live-apply is itself feedback.
- Colors, fonts and widget style come from the user's Plasma theme (light or dark, any accent). Nothing may assume a specific theme.

## Capabilities and Constraints

- Every setting in `src/config/krema.kcfg` that has a UI today must keep one.
- Theme colors only through `Kirigami.Theme`; spacing and sizes through `Kirigami.Units`; strings through `i18n()`.
- Settings UI is built from Kirigami Addons FormCard plus custom QML where a visual editor is needed.
- Every interactive control must be reachable by keyboard and exposed to AT-SPI with the same accessible name as its visible label (the E2E suite drives the window through AT-SPI).
- Animations run at 60 fps with declarative Qt Quick animations and stop while the window is hidden or minimized.

## Brand Commitments

- User-confirmed reference (2026-10-01, 2026-10-09): the window follows macOS System Settings: searchable sidebar, page title, grouped settings, and previews that look close to the real screen rather than schematic sketches.
- Settings must be explorable by direct manipulation (e.g. drag the light to change the shadow, scroll to change its height); exact numeric values live behind an Advanced disclosure.
- Shadow settings are explained with a real 3D scene (Qt Quick 3D) showing the light, the dock and the shadow it casts; simplified, not photorealistic.

## Evidence on Hand

- Current settings window: `src/qml/SettingsDialog.qml`, `src/qml/settings/*.qml`.
- Design requirements: `docs/settings-redesign-prd.md`.

## Product Principles

1. Show, then tell: every page opens with a live picture of what its settings change.
2. Plasma first: the window must look like it belongs in the user's Plasma theme.
3. Simple by default, exact on request.
4. The real dock is the final preview; settings apply instantly.

## Accessibility & Inclusion

Keyboard-only and screen-reader use are required (AT-SPI names match visible labels). Text contrast at least 4.5:1 against its background in both light and dark Breeze.
