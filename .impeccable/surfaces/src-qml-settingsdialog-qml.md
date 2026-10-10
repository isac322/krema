---
version: 1
slug: "src-qml-settingsdialog-qml"
primary_target: "src/qml/SettingsDialog.qml"
related_targets: ["src/qml/settings"]
---

Mode: Operate. Surface: Krema settings window (SettingsDialog.qml + settings/*.qml).

## Direction contract

THESIS: Krema's settings read like a page of macOS System Settings drawn with Plasma's own theme: a quiet sidebar, a big page title, and on top of every page a "stage" where a lifelike miniature of the user's desktop and dock reacts live to every control below it.

OWN-WORLD: Plasma theme colors only (Window ground, View-colored rounded groups, accent for selection). Grouped inset boxes (FormCard) with hairline row separators; labels left, controls right. Visual pickers are thumbnail tiles (rounded mini-desktop pictures) with the label under the tile and a 3 px accent ring when selected; no bordered card boxes. Real app icons from the icon theme, never gray squares or flat blue blocks.

STORY: The user picks a page, sees the dock as it is now on the stage, moves a control, and watches the stage and the real dock change together. Exact numbers are one disclosure away.

FIRST VIEWPORT: Left: sidebar 14 gridUnits, search pill on top, entries with 22 px color icons, accent-filled selected row. Right: centered content column (max ~38 gridUnits). Page title (Heading level 1) + one-line subtitle, then the stage (16:9-ish, max height ~11 gridUnits, 12 px radius, theme-derived wallpaper gradient, the dock drawn at the real proportions), then grouped boxes.

FORM: QML/Kirigami. Shared components: SettingsPageScaffold, DesktopStage, MiniDock, ChoiceTile (ChoiceCard), SettingRow/SliderDelegate. Shadow page stage is a Qt Quick 3D scene (desk plane, dock slab, draggable glowing light, real cast shadow) with the dock's actual shader result shown as an inset.

SIGNATURE: Measurement overlay: while a slider that changes geometry (icon size, spacing, zoom, corner radius, elevation) is pressed or hovered, the stage draws thin accent guide lines and the value in px on the miniature, so the number is tied to the thing it measures.
