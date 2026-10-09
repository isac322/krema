# Settings Window Redesign — PRD

> Status: **Implemented** (2026-10-09): 8 pages with live desktop previews, a 3D shadow scene,
> and measurement guides. Interview completed 2026-03-30.
> Ouroboros session: `interview_20260330_013829`
> Purpose: Launch gate milestone. macOS System Settings-level UX.

---

## Success Criteria

1. Every setting has visual context (embedded preview or live-apply to real dock)
2. Live changes at **60fps+** during drag interactions
3. New user can find and change any setting without documentation
4. Progressive disclosure: limited controls per page by default, advanced options 1-click away
5. Previews visually consistent with actual dock rendering (theme icons, panel styles, the dock's own shadow shader)
6. All 35 original settings preserved, plus the settings added since the interview — no functionality lost

## Preview Strategy: Live desktop stage

Each page opens with a hero **stage**: a miniature of the user's primary screen, drawn at its
real proportions with the current Plasma wallpaper (a theme gradient while no wallpaper image is
known), holding a lifelike miniature dock with real theme icons that follows every dock look
setting and zooms under the pointer. Every setting also live-applies to the real dock.

| Setting Type | Preview Method |
|---|---|
| Icon size, zoom, spacing, icon opacity | Desktop stage on the Icons page; measurement guides show the icon size, spacing, or zoomed size while a slider is hovered, dragged, or focused |
| Edge, floating, corner radius | Desktop stage that is itself the screen-edge picker; measurement guides for the corner radius and the floating gap |
| Background Style | Desktop stage plus one desktop thumbnail per style card |
| Shadow | 3D scene of the desktop with the dock and a draggable lamp, plus a "Result" inset drawn with the dock's real shadow shader |
| Animations, badges, visibility, monitor modes, transitions, virtual desktops | Looping previews in the stage and in the selection cards, drawn on desktop or monitor miniatures |
| Window preview size | Miniature preview popup on the stage; measurement guide for the thumbnail width |
| Delays, toggles | No visual preview needed (instant feedback or numeric) |

## Sidebar Structure (10 entries)

```
Sidebar (with "Search settings" field on top):
  1. Icons
  2. Layout & Position
  3. Panel Style
  4. Shadow
  5. Animations & Badges
  6. Behavior
  7. Monitors & Desktops
  8. Window Preview
  About
  9. About Krema
 10. About KDE
```

**Monitors & Desktops** was split out of Behavior during implementation. Behavior grew
with the click actions, screen-space reservation, and pinned/running task separation, so
the monitor mode, selected monitors, follow trigger, screen transition, and virtual
desktop settings moved to their own page.

The search field filters the sidebar by page name and by the names of the settings on
each page.

---

## Per-Setting Visual UX Design

"Previous UI" is the control used before the redesign. Rows 37 and later are settings
added after the interview.

### Page 1: Icons

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 1 | **Icon Size** (24-96px) | SpinBox | Miniature dock with real theme icons on the desktop stage. Slider resizes icons in the stage with a measurement guide. Live-apply. |
| 2 | **Icon Spacing** (0-64px) | SpinBox | Same stage — gap between icons changes, with a measurement guide. Slider + live-apply. Maximum raised from 16 to 64 px. |
| 3 | **Zoom Factor** (1.0-2.0x) | Slider | Stage dock zooms on hover; a "Hover" switch pins a static hover state in the stage. The measurement guide shows the factor and zoomed size. Slider + live-apply. |
| 4 | **Icon Normalization** (on/off) | Switch | Before/After comparison of the same icon with and without padding removal. |
| 5 | **Icon Scale** (50-100%) | Slider | Single enlarged icon showing the cell vs icon ratio. Slider + live-apply. |
| 37-39 | **Icon Opacity**: active window, inactive window and launcher, minimized window (10-100%) | Sliders | Three sliders. The stage dock shows icons in each state at the chosen opacity. |
| 40 | **Zoom Style** (Parabolic / In place) | ComboBox | 2-card picker, each with a desktop thumbnail of neighbors moving aside or icons overlapping. Disabled while zoom factor is 1.0. |
| 41-45 | **Zoom Animation**: preset, zoom-in/zoom-out duration (0-1000 ms), zoom-in/zoom-out easing | Preset radios + spin boxes and combos | "Preset" and "Custom" tabs. Preset: 4 cards (Natural, Quick, Relaxed, Instant), each playing its zoom motion. Custom: easing curve and motion preview above duration spin boxes and easing combos. Disabled while zoom factor is 1.0. |

**Note:** Preview icons are standard theme icons (Dolphin, Konsole, Firefox, and similar, with generic fallbacks), not the user's pinned apps.

### Page 2: Layout & Position

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 6 | **Screen Edge** (T/B/L/R) | ComboBox | **Desktop stage as picker**. Click any of 4 edge zones of the miniature desktop to move the dock; each edge is a radio button for keyboard and screen reader users. The miniature dock sits on the selected edge. Live-apply. |
| 7 | **Floating** (on/off) | Switch | Switch; the stage shows the dock detached from or attached to the edge, and measures the floating gap while the switch is hovered or focused. Live-apply. |
| 8 | **Corner Radius** (0-24) | SpinBox | Slider; the stage's miniature dock reflects the corner radius with a measurement guide. Live-apply. |

### Page 3: Panel Style

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 9 | **Background Style** (4 styles) | ComboBox | **Card picker**. 4 cards (Panel Inherit, Transparent, Tinted, Acrylic), each with a miniature dock on the user's wallpaper in that style. Styles unavailable in the session are disabled with a reason. |
| 10 | **Opacity** (0-100%) | Slider | Slider below the cards (hidden for Transparent). Card previews update in real time. Minimum lowered to 0%. |
| 11 | **Use System/Accent Color** | Switches | Shown per style. Toggling changes the card previews instantly. |
| 12 | **Tint Color** | Color button + dialog | Color swatch button (Tinted, without system color). Opens a color dialog; the previews update immediately. |

### Page 4: Shadow

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 13 | **Shadow Enabled** | Switch | Page-top switch. Off hides the 3D scene and all controls below. |
| 14-16 | **Light X/Y/Height** | 3 Sliders | **3D light scene**: the bottom of the user's desktop (real wallpaper) in perspective, the dock with real icons floating above it, and a glowing lamp whose cast shadow falls on the wallpaper. Drag the lamp to move the light; the mouse wheel changes its height. Arrow keys (Shift for larger steps), Page Up/Down, and +/- give keyboard control. |
| 17 | **Light Radius** (0.5-20) | Slider | Shift+wheel over the scene changes softness; a ring under the lamp shows the spread. |
| 18 | **Panel Elevation** (1-50) | Slider | Drag the dock in the scene, or focus the "Panel elevation" handle over the dock and use Up/Down and Page Up/Down. |
| 19 | **Intensity** (0-100%) | Slider | Slider in the Shadow group below the scene. |
| 20 | **Shadow Color** | Color button + dialog | Color swatch button with alpha channel + dialog. |

A "Result" inset in the scene's corner shows a miniature dock on the wallpaper with the dock's own
projective shadow shader, so the result matches the real dock. Dragging empty space orbits the
camera within limits; double-click resets the view.

**Progressive disclosure:** an "Advanced settings" expander below the Shadow group reveals 5 sliders (Light X, Light Y, Light height, Light radius, Panel elevation) for exact values.

### Page 5: Animations & Badges

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 21 | **Attention Animation** (7 types) | ComboBox | **Card grid**. 7 cards, each with a sample icon on a desktop thumbnail. The selected card loops its animation; others play on hover or focus. |
| 22 | **Attention Duration** (0-60s) | SpinBox | Spin box below the cards (hidden for None). 0 shows "Infinite". |
| 23 | **Badge Display** (3 modes) | ComboBox | **3-card picker**. Each card shows a sample icon with that badge style: Number ("8"), Dot, Off. |

### Page 6: Behavior

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 24 | **Visibility Mode** (3 modes) | ComboBox | **3-card picker with looping animation** on desktop thumbnails. Always visible: static dock. Auto hide: dock slides away and back. Dodge windows: a window descends and the dock hides. |
| 25 | **Dodge Active Only** | Switch | Shown in Dodge windows mode only. |
| 26-27 | **Show/Hide Delay** | SpinBoxes | Shown in Auto hide and Dodge windows modes. Sliders + ms labels. Live-apply. |
| 46 | **Reserve Screen Space** (on/off) | Switch | Switch, shown in Always visible mode only: "Maximized windows avoid the dock". |
| 47 | **Separate Pinned and Running Apps** (on/off) | Switch | Switch in the Tasks section. |
| 48-49 | **Single / Grouped Window Click Action** | ComboBoxes | Combo boxes in the Tasks section (unchanged; the choices are text only). |

### Page 7: Monitors & Desktops

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 28 | **Monitor Mode** (4 modes) | ComboBox | **4-card picker with miniature monitors**. Each card shows the connected screens (up to two in a card, a generic pair with fewer than two outputs) with the real wallpaper and a dock on the screens the mode selects: Primary monitor only, All monitors, Follow active screen (a dock travels between screens), Selected monitors. The stage above shows the user's monitors with the dock where it really is. |
| 50 | **Selected Monitors** (output name list) | Switch list | Shown in Selected monitors mode. One switch per output; disconnected selections stay listed and can be removed. A warning appears while a temporary dock is shown on the primary display. |
| 29 | **Follow Trigger** (3 types) | ComboBox | Shown in Follow active screen mode. 3 radio buttons with icon and description. |
| 30 | **Screen Transition** (3 types) | ComboBox | Shown in Follow active screen mode. 3 cards animating the dock leaving one miniature monitor and appearing on the other. |
| 31 | **Virtual Desktop Mode** (3 modes) | ComboBox | **3-card picker**. Each card shows a desktop thumbnail with a dock whose icons show how windows on other desktops appear. |
| 32 | **Other Desktop Opacity** (10-90%) | Slider | Shown in Dim mode only. Slider + live-apply. |

### Page 8: Window Preview

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 33 | **Preview Enabled** | Switch | Page-top switch. The stage shows a **miniature preview popup** above the miniature dock, with a header and a miniature app window as the thumbnail. A note explains when previews still open on click. |
| 34 | **Thumbnail Width** (120-320px) | SpinBox | Slider. The popup width changes in real time, with a measurement guide. |
| 35-36 | **Hover/Hide Delay** | SpinBoxes | Sliders + ms labels. |

### Keyboard and screen reader access

Every visual picker is a group of accessible radio buttons (cards, screen edges) that
can be focused with Tab and selected with Space or Return. Sliders keep an accessible name
equal to the visible label, color swatches announce the setting and the current color,
the shadow light source is an accessible "Light position" slider with keyboard controls, and
a "Panel elevation" slider handle over the 3D dock adjusts the elevation from the keyboard.

---

## Design Principles

1. **Visual first, numbers second** — every setting shows its effect visually before requiring numeric input
2. **Progressive disclosure** — default view is visual/interactive; "Advanced" expander reveals raw sliders for power users
3. **Live desktop stage** — previews draw the user's own wallpaper and screen proportions, and every setting also live-applies to the real dock
4. **Theme resources** — preview docks use standard theme icons, not the user's installed apps
5. **KDE HIG** — FormCard pattern for simple controls, custom QML for visual editors, Kirigami theming throughout
6. **60fps** — all drag interactions and preview updates must maintain 60fps

## Technical Notes

- Settings window uses its own `QQmlEngine` (not the dock engine)
- `SettingsDialog.qml` is a plain `QQC2.ApplicationWindow` with a custom sidebar
  (`Kirigami.SearchField` + list of `RoundedItemDelegate` entries) and a `Loader` that shows
  the current page's `FormCardPage`. It replaced Kirigami Addons `ConfigurationView`,
  whose drawer sidebar had unreliable AT-SPI coordinates
  (see `docs/kde/settings-window-patterns.md`)
- The window is created per open from a cached `QQmlComponent` and destroyed on close;
  `openModule(moduleId)` switches pages while it is open
- Shared building blocks in `src/qml/settings/`:
  - `SettingsPage.qml`: page root (`FormCardPage`) with a large title, a one-line subtitle,
    an optional hero `stage`, and the FormCard groups in one centered column
  - `DesktopStage.qml`: miniature of the primary screen with the real wallpaper and a
    measurement overlay (target outline, guide lines, value label) driven by
    `measureTarget`/`measureText`
  - `MiniDock.qml`: lifelike miniature dock with real theme icons that follows every
    `DockSettings` look property (overridable per picker tile) and zooms under the pointer
  - `ShadowScene3D.qml`: Qt Quick 3D scene of the desktop, dock, and lamp; the key light is a
    directional light aimed from the configured light position with PCF soft shadows
    (filter radius = light radius) caught on a multiply plane over the wallpaper
  - `ChoiceCard.qml` (card picker option with a thumbnail), `SliderDelegate.qml` (label/value
    slider row that snaps to its step and reports `active` for the measurement overlay),
    `ColorSwatchButton.qml` (color button + dialog)
- `SettingsWindow` exposes `wallpaperUrl` (current Plasma wallpaper image of the primary screen,
  empty if unknown), `screenSize`, and `screenAspect` (1920x1080 and 16/9 when unknown) to the
  stages
- The Shadow page's "Result" inset reuses the dock's projective shadow shader
  (`outer_shadow.frag.qsb`), so it shows the real dock shadow
- Qt Quick 3D (`QtQuick3D` QML module) is a runtime dependency, loaded only while the Shadow
  page shows an enabled shadow
- Animation previews run only while the settings window is shown and not minimized
- All visual editors bind directly to `DockSettings` properties (KConfigXT auto-save)
