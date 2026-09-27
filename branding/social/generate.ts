#!/usr/bin/env bun
// Generates Krema social/store images: SVG sources + optimized PNG exports.
// Usage: bun branding/social/generate.ts            (writes SVGs and PNGs)
//        bun branding/social/generate.ts --svg-only (writes SVGs only)
// Requires nix (resvg, oxipng, noto-fonts). Text is rendered with Noto Sans
// passed explicitly to resvg, so output never depends on system fonts.
import { $ } from "bun";
import { join } from "node:path";

const OUT = import.meta.dir;
const NIGHT = "#141C26";
const DEEP = "#0E141B";
const RAISED = "#1D2733";
const BONE = "#F3F0EA";
const BLUE = "#3DAEE9";
const MUTED = "#9AA5B1"; // bone at reduced contrast for secondary copy
const FONT = "Noto Sans";

// ---------- brand primitives (inline copies of the masters) ----------

const iconBody = (tile = true) =>
  (tile
    ? `<rect x="8" y="8" width="240" height="240" rx="56" fill="${NIGHT}"/>` +
      `<rect x="9" y="9" width="238" height="238" rx="55" fill="none" stroke="#FFFFFF" stroke-opacity=".08" stroke-width="2"/>`
    : "") +
  `<rect x="44" y="32" width="32" height="192" rx="16" fill="${BONE}"/>` +
  `<rect x="86" y="98" width="60" height="60" rx="16.2" fill="${BLUE}"/>` +
  `<rect x="151" y="57" width="36" height="36" rx="9.72" fill="${BLUE}" opacity="0.66"/>` +
  `<rect x="151" y="163" width="36" height="36" rx="9.72" fill="${BLUE}" opacity="0.66"/>` +
  `<rect x="192" y="32" width="20" height="20" rx="5.4" fill="${BLUE}" opacity="0.38"/>` +
  `<rect x="192" y="204" width="20" height="20" rx="5.4" fill="${BLUE}" opacity="0.38"/>`;

const iconSmallBody =
  `<rect x="8" y="8" width="240" height="240" rx="56" fill="${NIGHT}"/>` +
  `<rect x="9" y="9" width="238" height="238" rx="55" fill="none" stroke="#FFFFFF" stroke-opacity=".08" stroke-width="2"/>` +
  `<rect x="34" y="32" width="40" height="192" rx="20" fill="${BONE}"/>` +
  `<rect x="86" y="88" width="80" height="80" rx="21.6" fill="${BLUE}"/>` +
  `<rect x="174" y="32" width="48" height="48" rx="12.96" fill="${BLUE}" opacity="0.62"/>` +
  `<rect x="174" y="176" width="48" height="48" rx="12.96" fill="${BLUE}" opacity="0.62"/>`;

/** App icon (256 master) placed at x,y with side `size`. */
const icon = (x: number, y: number, size: number, tile = true) =>
  `<g transform="translate(${r(x)},${r(y)}) scale(${r(size / 256, 5)})">${iconBody(tile)}</g>`;

// Wordmark master: viewBox 0 -10.9 342.98 145.4 (aspect 2.3589)
const WM_W = 342.98;
const WM_H = 145.4;
const wordmarkInner = (ink: string) =>
  `<g fill="none" stroke="${ink}" stroke-linecap="round" stroke-linejoin="round">` +
  `<path transform="translate(6.5,28) scale(0.72)" d="M0,10 V100 M44,40 L4,74 M20,61 L46,100" stroke-width="18.06"/>` +
  `<path transform="translate(65.62,13) scale(0.87)" d="M0,40 V100 M0,64 C0,48 12,40 30,40" stroke-width="14.94"/>` +
  `<path transform="translate(117.72,-16) scale(1.16)" d="M2,70 H58 A28,28 0 1 0 51.4,88" stroke-width="11.21"/>` +
  `<path transform="translate(211,13) scale(0.87)" d="M0,40 V100 M0,60 C0,47 8,40 18,40 C29,40 34,47 34,60 V100 M34,60 C34,47 42,40 52,40 C63,40 68,47 68,60 V100" stroke-width="14.94"/>` +
  `<path transform="translate(296.16,28) scale(0.72)" d="M56,40 V100 M56,70 A28,28 0 1 1 0,70 A28,28 0 1 1 56,70" stroke-width="18.06"/>` +
  `</g><circle cx="151.36" cy="126" r="8.5" fill="${BLUE}"/>`;

/** Wordmark with given height; returns markup and width. */
const wordmark = (x: number, y: number, h: number, ink = BONE) => {
  const w = (h * WM_W) / WM_H;
  return {
    w,
    svg: `<svg x="${r(x)}" y="${r(y)}" width="${r(w)}" height="${r(h)}" viewBox="0 -10.9 ${WM_W} ${WM_H}">${wordmarkInner(ink)}</svg>`,
  };
};

// Horizontal lockup geometry from the master: icon 256 + gap, wordmark 445.87x189.02 at (308, 22.83)
const LOCKUP_W = 753.87;
const lockupWidth = (h: number) => (LOCKUP_W * h) / 256;
const lockup = (x: number, y: number, h: number) => {
  const s = h / 256;
  return icon(x, y, h) + wordmark(x + 308 * s, y + 22.83 * s, 189.02 * s).svg;
};

// ---------- helpers ----------

function r(n: number, d = 2) {
  return Number(n.toFixed(d)).toString();
}

const text = (
  x: number,
  y: number,
  size: number,
  content: string,
  opts: { fill?: string; weight?: number; anchor?: "start" | "middle" | "end"; spacing?: number; opacity?: number } = {},
) =>
  `<text x="${r(x)}" y="${r(y)}" font-family="${FONT}" font-size="${size}" font-weight="${opts.weight ?? 400}"` +
  ` fill="${opts.fill ?? BONE}" text-anchor="${opts.anchor ?? "start"}"` +
  (opts.spacing ? ` letter-spacing="${opts.spacing}"` : "") +
  (opts.opacity !== undefined ? ` fill-opacity="${opts.opacity}"` : "") +
  `>${content}</text>`;

const FEATURES = "Wayland-native · parabolic zoom · PipeWire previews";
const TAGLINE = "A lightweight dock for KDE Plasma 6";

/** Night background with subtle depth: vertical gradient + soft raised glow. */
const background = (w: number, h: number, glow: { cx: number; cy: number; rx: number; ry: number }) =>
  `<defs>` +
  `<linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${NIGHT}"/><stop offset="1" stop-color="${DEEP}"/></linearGradient>` +
  `<radialGradient id="glow" cx="${glow.cx}" cy="${glow.cy}" r="1" gradientUnits="userSpaceOnUse" gradientTransform="translate(${glow.cx},${glow.cy}) scale(${glow.rx},${glow.ry}) translate(${-glow.cx},${-glow.cy})">` +
  `<stop offset="0" stop-color="${RAISED}" stop-opacity="0.9"/><stop offset="1" stop-color="${RAISED}" stop-opacity="0"/></radialGradient>` +
  `<radialGradient id="accentGlow"><stop offset="0" stop-color="${BLUE}" stop-opacity="0.22"/><stop offset="1" stop-color="${BLUE}" stop-opacity="0"/></radialGradient>` +
  `</defs>` +
  `<rect width="${w}" height="${h}" fill="url(#bg)"/>` +
  `<rect width="${w}" height="${h}" fill="url(#glow)"/>`;

/**
 * A dock: a row of rounded tiles on a panel, with parabolic zoom around the
 * hovered tile. Tiles are bottom-aligned (they grow upward, like the real dock).
 * Hovered tile is Breeze blue; its neighbours carry fading blue, echoing the logo.
 * `cx` centre x, `baseY` panel bottom y, `base` unzoomed tile size.
 */
function dock(cx: number, baseY: number, base: number, count: number, hovered: number, zoom = 1.9, radius = 2.6) {
  const gap = base * 0.26;
  const pad = base * 0.24;
  const sizes = Array.from({ length: count }, (_, i) => {
    const d = Math.abs(i - hovered);
    const t = Math.max(0, 1 - (d / radius) ** 2);
    return base * (1 + (zoom - 1) * t);
  });
  const rowW = sizes.reduce((a, b) => a + b, 0) + gap * (count - 1);
  // Panel widens with the zoomed row, as the real dock does.
  const panelW = rowW + pad * 2;
  const panelH = base + pad * 2 + base * 0.12;
  const panelX = cx - panelW / 2;
  const panelY = baseY - panelH;
  const tileBottom = baseY - pad - base * 0.12;
  let out =
    `<rect x="${r(panelX)}" y="${r(panelY)}" width="${r(panelW)}" height="${r(panelH)}" rx="${r(panelH * 0.32)}" fill="${RAISED}" fill-opacity="0.85"/>` +
    `<rect x="${r(panelX + 1)}" y="${r(panelY + 1)}" width="${r(panelW - 2)}" height="${r(panelH - 2)}" rx="${r(panelH * 0.32 - 1)}" fill="none" stroke="#FFFFFF" stroke-opacity="0.07" stroke-width="2"/>`;
  let x = cx - rowW / 2;
  const blueAlpha = [1, 0.5, 0.22];
  sizes.forEach((s, i) => {
    const d = Math.abs(i - hovered);
    const y = tileBottom - s;
    const rx = s * 0.27;
    out += `<rect x="${r(x)}" y="${r(y)}" width="${r(s)}" height="${r(s)}" rx="${r(rx)}" fill="#2A3645"/>`;
    if (d < blueAlpha.length)
      out += `<rect x="${r(x)}" y="${r(y)}" width="${r(s)}" height="${r(s)}" rx="${r(rx)}" fill="${BLUE}" fill-opacity="${blueAlpha[d]}"/>`;
    out += `<rect x="${r(x + 0.75)}" y="${r(y + 0.75)}" width="${r(s - 1.5)}" height="${r(s - 1.5)}" rx="${r(rx - 0.75)}" fill="none" stroke="#FFFFFF" stroke-opacity="0.08" stroke-width="1.5"/>`;
    // running-app indicator dots: hovered = blue accent, a few others = bone
    const dotY = baseY - pad * 0.62;
    const dotR = Math.max(1.5, base * 0.045);
    if (i === hovered) out += `<circle cx="${r(x + s / 2)}" cy="${r(dotY)}" r="${r(dotR)}" fill="${BLUE}"/>`;
    else if (i % 3 === 0) out += `<circle cx="${r(x + s / 2)}" cy="${r(dotY)}" r="${r(dotR)}" fill="${BONE}" fill-opacity="0.55"/>`;
    x += s + gap;
  });
  return { svg: out, top: tileBottom - Math.max(...sizes) };
}

const doc = (w: number, h: number, body: string, label = "Krema") =>
  `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}" role="img" aria-label="${label}"><title>${label}</title>${body}</svg>\n`;

// ---------- compositions ----------

type Asset = { name: string; w: number; h: number; svg: string };
const assets: Asset[] = [];
const add = (name: string, w: number, h: number, body: string, label?: string) =>
  assets.push({ name, w, h, svg: doc(w, h, body, label) });

// 1. GitHub social preview 1280x640 — centred lockup, tagline, features, dock along the bottom.
{
  const W = 1280, H = 640;
  const lh = 150;
  const lw = lockupWidth(lh);
  const d = dock(W / 2, 580, 46, 11, 5);
  add(
    "github-social-preview", W, H,
    background(W, H, { cx: W / 2, cy: 250, rx: 720, ry: 380 }) +
      lockup((W - lw) / 2, 58, lh) +
      text(W / 2, 290, 50, TAGLINE, { weight: 600, anchor: "middle" }) +
      text(W / 2, 345, 28, FEATURES, { fill: MUTED, anchor: "middle" }) +
      d.svg,
    "Krema — A lightweight dock for KDE Plasma 6",
  );
}

// 2. Open Graph 1200x630 — left-aligned copy column, large icon with accent glow on the right.
{
  const W = 1200, H = 630;
  const wm = wordmark(80, 118, 132);
  const iconSize = 380;
  const ix = W - 80 - iconSize, iy = (H - iconSize) / 2;
  add(
    "og-image", W, H,
    background(W, H, { cx: ix + iconSize / 2, cy: H / 2, rx: 520, ry: 460 }) +
      `<circle cx="${ix + iconSize / 2}" cy="${H / 2}" r="${iconSize * 0.72}" fill="url(#accentGlow)"/>` +
      wm.svg +
      text(84, 340, 44, "A lightweight dock", { weight: 600 }) +
      text(84, 396, 44, "for KDE Plasma 6", { weight: 600 }) +
      `<rect x="84" y="436" width="56" height="6" rx="3" fill="${BLUE}"/>` +
      text(84, 492, 26, "Wayland-native · parabolic zoom", { fill: MUTED }) +
      text(84, 530, 26, "PipeWire window previews", { fill: MUTED }) +
      icon(ix, iy, iconSize),
    "Krema — A lightweight dock for KDE Plasma 6",
  );
}

// 3. X / Mastodon / Bluesky card 1600x900 — hero dock mid-frame with zoom, lockup above, copy below.
{
  const W = 1600, H = 900;
  const lh = 150;
  const lw = lockupWidth(lh);
  const d = dock(W / 2, 600, 68, 11, 5, 1.95);
  add(
    "x-card", W, H,
    background(W, H, { cx: W / 2, cy: 470, rx: 900, ry: 420 }) +
      `<ellipse cx="${W / 2}" cy="${d.top + 70}" rx="260" ry="160" fill="url(#accentGlow)"/>` +
      lockup((W - lw) / 2, 90, lh) +
      d.svg +
      text(W / 2, 720, 58, TAGLINE, { weight: 600, anchor: "middle" }) +
      text(W / 2, 790, 32, FEATURES, { fill: MUTED, anchor: "middle" }),
    "Krema — A lightweight dock for KDE Plasma 6",
  );
}

// 4. Square 1080x1080 — stacked lockup, tagline, dock at the bottom.
{
  const W = 1080, H = 1080;
  const is = 300;
  const wm = wordmark(0, 0, 150);
  const d = dock(W / 2, 980, 50, 9, 4);
  add(
    "square", W, H,
    background(W, H, { cx: W / 2, cy: 360, rx: 700, ry: 560 }) +
      icon((W - is) / 2, 110, is) +
      wordmark((W - wm.w) / 2, 440, 150).svg +
      text(W / 2, 680, 50, "A lightweight dock", { weight: 600, anchor: "middle" }) +
      text(W / 2, 742, 50, "for KDE Plasma 6", { weight: 600, anchor: "middle" }) +
      text(W / 2, 810, 28, FEATURES, { fill: MUTED, anchor: "middle" }) +
      d.svg,
    "Krema — A lightweight dock for KDE Plasma 6",
  );
}

// 5. Avatars — flat night background, icon glyph with generous padding (circle-crop safe).
for (const [name, S] of [["avatar", 512], ["avatar-400", 400]] as const) {
  // Glyph occupies x 44..212, y 32..224 of the 256 master; at 0.72 its farthest
  // corner sits ~0.6 of the radius from centre, well inside a circle crop.
  const size = S * 0.72;
  add(name, S, S, `<rect width="${S}" height="${S}" fill="${NIGHT}"/>` + icon((S - size) / 2, (S - size) / 2, size, false));
}

// 6. Launchpad
add("launchpad-icon-14", 14, 14, `<g transform="scale(${14 / 256})">${iconSmallBody}</g>`);
add("launchpad-logo-64", 64, 64, icon(0, 0, 64));
add("launchpad-brand-192", 192, 192, icon(0, 0, 192));

// 7. KDE Store logo + banner
add("kde-store-logo", 256, 256, iconBody());
{
  const W = 1920, H = 480;
  const lh = 200;
  const d = dock(1420, 322, 64, 9, 4);
  add(
    "store-banner", W, H,
    background(W, H, { cx: 1420, cy: 240, rx: 700, ry: 320 }) +
      `<ellipse cx="1420" cy="250" rx="220" ry="150" fill="url(#accentGlow)"/>` +
      lockup(120, 80, lh) +
      text(126, 368, 44, TAGLINE, { weight: 600 }) +
      text(126, 414, 26, FEATURES, { fill: MUTED }) +
      d.svg,
    "Krema — A lightweight dock for KDE Plasma 6",
  );
}

// 8. Release notes header 1280x320
{
  const W = 1280, H = 320;
  const lh = 140;
  const lw = lockupWidth(lh);
  add(
    "release-banner", W, H,
    background(W, H, { cx: W / 2, cy: H / 2, rx: 800, ry: 260 }) +
      lockup(90, (H - lh) / 2, lh) +
      `<rect x="${r(90 + lw + 60)}" y="${(H - 120) / 2}" width="3" height="120" rx="1.5" fill="${BONE}" fill-opacity="0.18"/>` +
      text(90 + lw + 108, 170, 56, "Release notes", { weight: 600 }) +
      text(90 + lw + 110, 214, 26, "KDE Plasma 6 dock", { fill: MUTED }) +
      `<rect x="${r(90 + lw + 110)}" y="232" width="48" height="5" rx="2.5" fill="${BLUE}"/>` +
      dock(1135, 206, 18, 5, 2).svg,
    "Krema — Release notes",
  );
}

// ---------- write + render ----------

for (const a of assets) await Bun.write(join(OUT, `${a.name}.svg`), a.svg);
if (process.argv.includes("--svg-only")) process.exit(0);

const fontDir = (await $`nix build nixpkgs#noto-fonts --no-link --print-out-paths`.text()).trim();
const font = join(fontDir, "share/fonts/noto/NotoSans.ttf");
for (const a of assets) {
  const svg = join(OUT, `${a.name}.svg`);
  const png = join(OUT, `${a.name}.png`);
  await $`nix shell nixpkgs#resvg -c resvg --skip-system-fonts --use-font-file ${font} --font-family ${FONT} -w ${a.w} -h ${a.h} ${svg} ${png}`;
}
const pngs = assets.map((a) => join(OUT, `${a.name}.png`));
await $`nix shell nixpkgs#oxipng -c oxipng -o 4 --strip safe ${pngs}`.quiet();
console.log(assets.map((a) => `${a.name}.png ${a.w}x${a.h}`).join("\n"));
