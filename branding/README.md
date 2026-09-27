# Krema Brand Guide

Krema's identity is built from one idea: **the dock zooms**. The app icon draws a
"K" out of a dock, and the wordmark sets its letters the way the dock sizes
icons under the pointer.

## Concept

### Zoom K (app icon)

- **Stem** (bone) is the dock panel.
- **Arms** are Breeze-blue rounded tiles — dock icons — that shrink and fade as
  they move away from the largest tile next to the stem. The falloff follows the
  same parabolic zoom curve the dock uses on hover.
- The night tile behind it keeps the mark legible on both light and dark desktops.

### Zoom wordmark

Hand-drawn monoline lowercase `krema`. Each letter is scaled like an icon in a
zooming dock, with the pointer resting on the `e`:

| Letter | k | r | e | m | a |
|--------|------|------|------|------|------|
| Scale  | 0.72 | 0.87 | 1.16 | 0.87 | 0.72 |

The Breeze-blue dot under the `e` is the dock's active-indicator. It is always
the accent color, whichever ink the letters use. Never re-scale letters
individually, even out the sizes, or move the dot to another letter.

## Files

| File | What it is | Use it for |
|------|------------|------------|
| `krema-icon.svg` | Full Zoom K icon (three tile sizes per arm) | App icon at 32 px and larger, avatars, store listings, favicons ≥ 32 px |
| `krema-icon-small.svg` | Simplified Zoom K (two larger tiles per arm) | 16, 22, and 24 px: panels, tray, menus, small favicons |
| `krema-wordmark.svg` | Zoom wordmark, night ink | Light backgrounds |
| `krema-wordmark-dark.svg` | Zoom wordmark, bone ink | Dark backgrounds |
| `krema-lockup.svg` | Horizontal lockup: icon + wordmark, night ink | Headers and banners on light backgrounds (README, website) |
| `krema-lockup-dark.svg` | Horizontal lockup, bone ink | Headers and banners on dark backgrounds |
| `krema-lockup-stacked.svg` | Stacked lockup: icon above wordmark, night ink | Square or portrait spaces on light backgrounds (posters, slides, social avatars with text) |
| `krema-lockup-stacked-dark.svg` | Stacked lockup, bone ink | Square or portrait spaces on dark backgrounds |
| `generate.ts` | Bun script that emits every SVG above | Regenerating the masters — edit this, not the SVGs |
| `social/` | Raster social images (e.g. `social/github-social-preview.png`) | GitHub social preview, link cards, announcement posts |

The icon has a dark tile of its own, so it works on any background. Only the
wordmark and lockups have light/dark variants; pick the one whose ink contrasts
with the surface behind it. On the web, switch between them with
`<picture>` and `prefers-color-scheme` (see the top of the project README).

## Palette

| Swatch | Name | Hex | Role |
|--------|------|-----|------|
| ![](https://img.shields.io/badge/-%20%20%20%20-141C26?style=flat-square) | Night | `#141C26` | Icon tile, wordmark ink on light backgrounds, dark surfaces |
| ![](https://img.shields.io/badge/-%20%20%20%20-F3F0EA?style=flat-square) | Bone | `#F3F0EA` | Icon stem (the dock panel), wordmark ink on dark backgrounds, light surfaces |
| ![](https://img.shields.io/badge/-%20%20%20%20-3DAEE9?style=flat-square) | Breeze Blue | `#3DAEE9` | Accent only: icon tiles, the indicator dot, links and highlights |

Secondary neutrals for layouts (never inside the logo): `#0E141B` (deeper
night), `#1D2733` (raised dark surface), `#EFF0F1` (Breeze light gray).

Breeze Blue is an accent. Use it for small, meaningful marks; do not flood large
areas with it.

## Clear space and minimum sizes

- **Clear space:** keep empty space around the icon equal to at least ¼ of the
  icon's width, and around lockups equal to at least the height of the
  wordmark's `e`. No text, borders, or other logos inside that area.
- **Icon:** 16 px minimum. From 16 to 24 px use `krema-icon-small.svg`; at 32 px
  and above use `krema-icon.svg`. The full icon's smallest tiles disappear
  below 32 px.
- **Wordmark:** at least 80 px wide on screen. Below that, use the icon alone.
- **Horizontal lockup:** at least 48 px tall (the README uses 96 px). Below
  that, use the icon alone.

## Don'ts

- Don't recolor the tiles, the stem, or the indicator dot. The tiles are always
  Breeze Blue, the stem is always Bone, the tile is always Night.
- Don't rotate, skew, mirror, or add shadows, gradients, or outlines to any mark.
- Don't reorder, respace, or re-scale the wordmark letters, or typeset "krema"
  in another font as a stand-in for the wordmark.
- Don't use a dark-ink wordmark on a dark background or a light-ink one on a
  light background.
- Don't redraw the icon's arms with a different number or size of tiles; the
  shrinking sequence is the zoom falloff.
- Don't place the Krema logo next to the Latte Dock logo or the KDE logo in a way
  that implies affiliation or endorsement. Krema is an independent project; its
  only relation to Latte Dock is being its spiritual successor.

## Regenerating

The SVGs are generated; change `generate.ts`, then run from the repository root:

```sh
bun branding/generate.ts branding
```

Rasterize with [resvg](https://github.com/linebender/resvg) and optimize with
[oxipng](https://github.com/shssoichiro/oxipng):

```sh
# Icon at a given pixel size (use the small variant for 16/22/24 px)
nix shell nixpkgs#resvg -c resvg -w 256 branding/krema-icon.svg krema-256.png
nix shell nixpkgs#resvg -c resvg -w 22 branding/krema-icon-small.svg krema-22.png

# Lockup at a fixed height (nested SVGs resolve relative to --resources-dir)
nix shell nixpkgs#resvg -c resvg -h 192 --resources-dir branding \
  branding/krema-lockup.svg krema-lockup.png

# Optimize every PNG before committing
nix shell nixpkgs#oxipng -c oxipng -o 4 --strip safe krema-256.png
```

## License

Brand assets are part of this repository and are distributed with it. Use them
to refer to Krema — in articles, reviews, package listings, and community
posts. Don't use them in a way that suggests your project or product is Krema
or is endorsed by it.
