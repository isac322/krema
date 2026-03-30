# Settings Window Redesign — PRD

> Status: **Interview complete** (2026-03-30)
> Ouroboros session: `interview_20260330_013829`
> Purpose: Launch gate milestone. macOS System Settings-level UX.

---

## Success Criteria

1. Every setting has visual context (embedded preview or live-apply to real dock)
2. Live changes at **60fps+** during drag interactions
3. New user can find and change any setting without documentation
4. Progressive disclosure: limited controls per page by default, advanced options 1-click away
5. Canvas previews visually consistent with actual dock rendering
6. All 35 existing settings preserved — no functionality lost

## Preview Strategy: Hybrid

| Setting Type | Preview Method |
|---|---|
| Shadow, Background Style | Embedded canvas inside settings window (renders dock geometry only) |
| Icon size, zoom, spacing | Live-apply to real dock immediately |
| Edge, position, floating | Live-apply (real dock moves instantly) |
| Animations, badges | Self-contained preview within selection cards |
| Delays, toggles | No visual preview needed (instant feedback or numeric) |

## Tab Structure (9 tabs)

```
Sidebar:
  1. Icons
  2. Layout & Position
  3. Panel Style
  4. Shadow
  5. Animations & Badges
  6. Behavior
  7. Window Preview
  8. About Krema
  9. About KDE
```

---

## Per-Setting Visual UX Design

### Tab 1: Icons

| # | Setting | Current UI | New Visual UX |
|---|---------|-----------|---------------|
| 1 | **Icon Size** (24-96px) | SpinBox | Mini dock preview (3-4 bundled sample icons) at page top. Slider resizes icons in preview. Live-apply. |
| 2 | **Icon Spacing** (0-16px) | SpinBox | Same mini dock preview — gap between icons changes. Slider + live-apply. |
| 3 | **Zoom Factor** (1.0-2.0x) | Slider | Mini dock preview with center icon in zoomed state by default. When user hovers over preview, real hover simulation activates. Slider + live-apply. |
| 4 | **Icon Normalization** (on/off) | Switch | Before/After comparison preview. Toggle shows same icon with/without padding removal. Description: "Remove internal icon padding for consistent sizing." |
| 5 | **Icon Scale** (50-100%) | Slider | Single icon enlarged preview showing cell vs icon ratio. 100%=fills cell, 50%=half of cell. Slider + live-apply. |

**Note:** Preview icons are bundled app resources, not from user's system.

### Tab 2: Layout & Position

| # | Setting | Current UI | New Visual UX |
|---|---------|-----------|---------------|
| 6 | **Screen Edge** (T/B/L/R) | ComboBox | **Monitor schematic diagram** (rectangle). Click any of 4 edges to move dock. Mini dock shown on selected edge. Live-apply. |
| 7 | **Floating** (on/off) | Switch | Same schematic — toggle shows dock detached (floating) vs attached to edge. Live-apply. |
| 8 | **Corner Radius** (0-24) | SpinBox | Schematic's mini dock reflects actual corner radius. Slider + live-apply. |

### Tab 3: Panel Style

| # | Setting | Current UI | New Visual UX |
|---|---------|-----------|---------------|
| 9 | **Background Style** (4 styles) | ComboBox | **Card-based visual picker**. 4 cards, each with mini dock preview in that style. Click to select. Selected card has border highlight. |
| 10 | **Opacity** (0-100%) | Slider | Appears below selected style card. Adjusting changes card preview transparency in real-time. |
| 11 | **Use System/Accent Color** | Switches | Conditional display per style. Toggle changes card preview color instantly. |
| 12 | **Tint Color** | Color swatch + dialog | Color circle button next to card preview. Click opens color dialog. Selection reflects in preview immediately. |

### Tab 4: Shadow

| # | Setting | Current UI | New Visual UX |
|---|---------|-----------|---------------|
| 13 | **Shadow Enabled** | Switch | Page-top toggle. Off hides all controls below. |
| 14-16 | **Light X/Y/Z** | 3 Sliders | **2D canvas** (top-down view). Dock = center rectangle, Light = draggable circle. Drag to adjust X/Y. Z = circle size (farther=smaller, closer=larger) or separate vertical slider. Shadow preview below canvas. |
| 17 | **Light Radius** (0.5-20) | Slider | Drag light circle's border to resize. Larger = softer shadow, smaller = sharper shadow. Real-time preview. |
| 18 | **Elevation** (1-50) | Slider | Vertical slider on dock rectangle (or drag dock up/down in canvas). Higher = larger shadow offset. |
| 19 | **Intensity** (0-100%) | Slider | Below canvas. Shadow darkness changes in real-time in preview. |
| 20 | **Shadow Color** | Color swatch + dialog | Color circle button + dialog. |

**Progressive disclosure:** "Advanced Settings v" expander below canvas reveals all 6 raw sliders (Light X/Y/Z, Radius, Elevation, Intensity) for precise numeric control.

### Tab 5: Animations & Badges

| # | Setting | Current UI | New Visual UX |
|---|---------|-----------|---------------|
| 21 | **Attention Animation** (7 types) | ComboBox | **Visual grid picker**. 7 cards, each showing sample icon with that animation playing in loop. Click to select. Selected card plays animation. (macOS screensaver picker style) |
| 22 | **Attention Duration** (0-60s) | SpinBox | Spinner below selected animation card. 0 = "Infinite" label. |
| 23 | **Badge Display** (3 modes) | ComboBox | **3-card picker**. Each card shows sample icon with that badge style: Number ("8"), Dot ("●"), Off (none). |

### Tab 6: Behavior

| # | Setting | Current UI | New Visual UX |
|---|---------|-----------|---------------|
| 24 | **Visibility Mode** (3 modes) | ComboBox | **3-card picker + looping animation**. Always: static dock. AutoHide: dock appears/disappears. Dodge: window descends, dock hides. |
| 25 | **Dodge Active Only** | Switch | Conditional (Dodge only). Switch + description "Only dodge active window". |
| 26-27 | **Show/Hide Delay** | SpinBoxes | Conditional (AutoHide/Dodge). Sliders + ms labels. Live-apply. |
| 28 | **Monitor Mode** (3 modes) | ComboBox | **Dual monitor schematic**. 2 rectangles + dock position per mode. Primary=left only, All=both, Follow=arrow animation. |
| 29 | **Follow Trigger** (3 types) | ComboBox | Conditional (Follow only). 3 radio buttons + icon/description. |
| 30 | **Screen Transition** (3 types) | ComboBox | Conditional (Follow only). 3 mini animation cards. |
| 31 | **Virtual Desktop Mode** (3 modes) | ComboBox | **Mini desktop grid**. 2 virtual desktops shown. Each mode demonstrates how other-desktop icons appear. |
| 32 | **Other Desktop Opacity** (10-90%) | Slider | Conditional (Dim only). Slider + preview shows dimmed icons changing opacity. |

### Tab 7: Window Preview

| # | Setting | Current UI | New Visual UX |
|---|---------|-----------|---------------|
| 33 | **Preview Enabled** | Switch | Top toggle. Below: **sample preview popup mockup** using sample thumbnail images. |
| 34 | **Thumbnail Width** (120-320px) | SpinBox | Slider. Mockup width changes in real-time. |
| 35-36 | **Hover/Hide Delay** | SpinBoxes | Sliders + ms labels. Simple form controls sufficient. |

---

## Design Principles

1. **Visual first, numbers second** — every setting shows its effect visually before requiring numeric input
2. **Progressive disclosure** — default view is visual/interactive; "Advanced" expander reveals raw sliders for power users
3. **Hybrid preview** — shadow/background use embedded canvas, everything else live-applies to real dock
4. **Bundled resources** — preview dock uses bundled sample icons, not user's installed apps
5. **KDE HIG** — FormCard pattern for simple controls, custom QML for visual editors, Kirigami theming throughout
6. **60fps** — all drag interactions and preview updates must maintain 60fps

## Technical Notes

- Settings window uses separate `QQmlApplicationEngine` (not dock engine)
- ConfigurationView sidebar navigation with 9 modules
- Canvas previews render simplified dock geometry (not full dock replica)
- Animation previews use lightweight QML animations on sample icons
- All visual editors bind directly to `DockSettings` properties (KConfigXT auto-save)
- Monitor/desktop schematics are pure QML drawings (Rectangle/Canvas)
