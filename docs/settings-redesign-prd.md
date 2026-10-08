# Settings Window Redesign — PRD

> Status: **Implemented** (2026-10-08). Interview completed 2026-03-30.
> Ouroboros session: `interview_20260330_013829`
> Purpose: Launch gate milestone. macOS System Settings-level UX.

---

## Success Criteria

1. Every setting has visual context (embedded preview or live-apply to real dock)
2. Live changes at **60fps+** during drag interactions
3. New user can find and change any setting without documentation
4. Progressive disclosure: limited controls per page by default, advanced options 1-click away
5. Canvas previews visually consistent with actual dock rendering
6. All 35 original settings preserved, plus the settings added since the interview — no functionality lost

## Preview Strategy: Hybrid

| Setting Type | Preview Method |
|---|---|
| Shadow, Background Style | Embedded canvas inside settings window (renders dock geometry only) |
| Icon size, zoom, spacing, icon opacity | Embedded sample dock at the top of the Icons page, plus live-apply to the real dock |
| Edge, position, floating | Monitor schematic in the page, plus live-apply (real dock moves instantly) |
| Animations, badges, visibility, monitor modes, transitions, virtual desktops | Self-contained looping preview within selection cards |
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
| 1 | **Icon Size** (24-96px) | SpinBox | Sample dock with bundled icons at page top. Slider resizes icons in the sample. Live-apply. |
| 2 | **Icon Spacing** (0-64px) | SpinBox | Same sample dock — gap between icons changes. Slider + live-apply. Maximum raised from 16 to 64 px. |
| 3 | **Zoom Factor** (1.0-2.0x) | Slider | Sample dock zooms on hover; a "Hover" switch pins a static hover state in the sample. Slider + live-apply. |
| 4 | **Icon Normalization** (on/off) | Switch | Before/After comparison of the same icon with and without padding removal. |
| 5 | **Icon Scale** (50-100%) | Slider | Single enlarged icon showing the cell vs icon ratio. Slider + live-apply. |
| 37-39 | **Icon Opacity**: active window, inactive window and launcher, minimized window (10-100%) | Sliders | Three sliders. The sample dock shows icons in each state at the chosen opacity. |
| 40 | **Zoom Style** (Parabolic / In place) | ComboBox | 2-card picker, each with a sketch of neighbors moving aside or icons overlapping. Disabled while zoom factor is 1.0. |
| 41-45 | **Zoom Animation**: preset, zoom-in/zoom-out duration (0-1000 ms), zoom-in/zoom-out easing | Preset radios + spin boxes and combos | "Preset" and "Custom" tabs. Preset: 4 cards (Natural, Quick, Relaxed, Instant), each playing its zoom motion. Custom: easing curve and motion preview above duration spin boxes and easing combos. Disabled while zoom factor is 1.0. |

**Note:** Preview icons are bundled app resources, not from user's system.

### Page 2: Layout & Position

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 6 | **Screen Edge** (T/B/L/R) | ComboBox | **Monitor schematic**. Click any of 4 edges to move the dock; each edge is a radio button for keyboard and screen reader users. Mini dock shown on the selected edge. Live-apply. |
| 7 | **Floating** (on/off) | Switch | Switch; the schematic shows the dock detached from or attached to the edge. Live-apply. |
| 8 | **Corner Radius** (0-24) | SpinBox | Slider; the schematic's mini dock reflects the corner radius. Live-apply. |

### Page 3: Panel Style

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 9 | **Background Style** (4 styles) | ComboBox | **Card picker**. 4 cards (Panel Inherit, Transparent, Tinted, Acrylic), each with a mini dock over a sample wallpaper in that style. Styles unavailable in the session are disabled with a reason. |
| 10 | **Opacity** (0-100%) | Slider | Slider below the cards (hidden for Transparent). Card previews update in real time. Minimum lowered to 0%. |
| 11 | **Use System/Accent Color** | Switches | Shown per style. Toggling changes the card previews instantly. |
| 12 | **Tint Color** | Color button + dialog | Color swatch button (Tinted, without system color). Opens a color dialog; the previews update immediately. |

### Page 4: Shadow

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 13 | **Shadow Enabled** | Switch | Page-top switch. Off hides all controls below. |
| 14-16 | **Light X/Y/Height** | 3 Sliders | **2D light-source editor** (top-down view). Dock = center rectangle, light = draggable circle; scrolling over it changes the height. Shadow preview below the editor. Arrow keys, Page Up/Down, and +/- give keyboard control. |
| 17 | **Light Radius** (0.5-20) | Slider | Drag the light's outer ring (or Shift+scroll) to change softness. Real-time preview. |
| 18 | **Panel Elevation** (1-50) | Slider | Drag the dock in the editor or use the vertical slider beside it. |
| 19 | **Intensity** (0-100%) | Slider | Slider below the editor. Shadow darkness changes in the preview. |
| 20 | **Shadow Color** | Color button + dialog | Color swatch button with alpha channel + dialog. |

**Progressive disclosure:** an "Advanced settings" expander below the editor reveals 6 sliders (Light X/Y, Light height, Light radius, Panel elevation, Shadow intensity) for exact values.

### Page 5: Animations & Badges

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 21 | **Attention Animation** (7 types) | ComboBox | **Card grid**. 7 cards, each with a sample icon. The selected card loops its animation; others play on hover or focus. |
| 22 | **Attention Duration** (0-60s) | SpinBox | Spin box below the cards (hidden for None). 0 shows "Infinite". |
| 23 | **Badge Display** (3 modes) | ComboBox | **3-card picker**. Each card shows a sample icon with that badge style: Number ("8"), Dot, Off. |

### Page 6: Behavior

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 24 | **Visibility Mode** (3 modes) | ComboBox | **3-card picker with looping animation**. Always visible: static dock. Auto hide: dock slides away and back. Dodge windows: a window descends and the dock hides. |
| 25 | **Dodge Active Only** | Switch | Shown in Dodge windows mode only. |
| 26-27 | **Show/Hide Delay** | SpinBoxes | Shown in Auto hide and Dodge windows modes. Sliders + ms labels. Live-apply. |
| 46 | **Reserve Screen Space** (on/off) | Switch | Switch, shown in Always visible mode only: "Maximized windows avoid the dock". |
| 47 | **Separate Pinned and Running Apps** (on/off) | Switch | Switch in the Tasks section. |
| 48-49 | **Single / Grouped Window Click Action** | ComboBoxes | Combo boxes in the Tasks section (unchanged; the choices are text only). |

### Page 7: Monitors & Desktops

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 28 | **Monitor Mode** (4 modes) | ComboBox | **4-card picker with monitor schematics**. Two mini screens per card: Primary monitor only, All monitors, Follow active screen (a dock travels between screens with an arrow), Selected monitors. |
| 50 | **Selected Monitors** (output name list) | Switch list | Shown in Selected monitors mode. One switch per output; disconnected selections stay listed and can be removed. A warning appears while a temporary dock is shown on the primary display. |
| 29 | **Follow Trigger** (3 types) | ComboBox | Shown in Follow active screen mode. 3 radio buttons with icon and description. |
| 30 | **Screen Transition** (3 types) | ComboBox | Shown in Follow active screen mode. 3 cards animating the dock leaving one screen and appearing on the other. |
| 31 | **Virtual Desktop Mode** (3 modes) | ComboBox | **3-card picker**. Two mini desktops above a dock; each card shows how icons of windows on the other desktop appear. |
| 32 | **Other Desktop Opacity** (10-90%) | Slider | Shown in Dim mode only. Slider + live-apply. |

### Page 8: Window Preview

| # | Setting | Previous UI | Visual UX |
|---|---------|-----------|---------------|
| 33 | **Preview Enabled** | Switch | Page-top switch. Below: **sample preview popup mockup** with a placeholder thumbnail. A note explains when previews still open on click. |
| 34 | **Thumbnail Width** (120-320px) | SpinBox | Slider. Mockup width changes in real time. |
| 35-36 | **Hover/Hide Delay** | SpinBoxes | Sliders + ms labels. |

### Keyboard and screen reader access

Every visual picker is a group of accessible radio buttons (cards, schematic edges) that
can be focused with Tab and selected with Space or Return. Sliders keep an accessible name
equal to the visible label, color swatches announce the setting and the current color, and
the shadow light source is an accessible slider with keyboard controls.

---

## Design Principles

1. **Visual first, numbers second** — every setting shows its effect visually before requiring numeric input
2. **Progressive disclosure** — default view is visual/interactive; "Advanced" expander reveals raw sliders for power users
3. **Hybrid preview** — shadow/background use embedded canvas, everything else live-applies to real dock
4. **Bundled resources** — preview dock uses bundled sample icons, not user's installed apps
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
- Shared building blocks in `src/qml/settings/`: `ChoiceCard.qml` (card picker option),
  `SliderDelegate.qml` (labelled slider row), `ColorSwatchButton.qml` (color button +
  dialog), `FormSection.qml`
- Canvas previews render simplified dock geometry (not full dock replica)
- Animation previews use lightweight QML animations on sample icons and run only while
  their page is shown
- All visual editors bind directly to `DockSettings` properties (KConfigXT auto-save)
- Monitor/desktop schematics are pure QML drawings (Rectangle/Shape)
