// Emits Krema brand master SVGs (Zoom K icon + zoom wordmark + lockups) next to
// this script in <repo>/branding/logo/. Usage: bun branding/logo/generate.ts [dir]
import { mkdirSync, writeFileSync } from "node:fs";

const OUT = process.argv[2] ?? import.meta.dir;
mkdirSync(OUT, { recursive: true });

// Palette A (matches website/css/site.css): ink tile, bone stem, caramel accent.
const INK = "#18110D", BONE = "#F4ECE0", ACCENT = "#D69A5E";
const r = (n: number) => Math.round(n * 100) / 100;
const tile = (cx: number, cy: number, s: number, extra = "") =>
  `<rect x="${r(cx - s / 2)}" y="${r(cy - s / 2)}" width="${s}" height="${s}" rx="${r(s * 0.27)}" fill="${ACCENT}"${extra}/>`;

// Zoom K: stem = dock panel, arms = tiles shrinking away from the hovered (largest) tile.
function iconBody(small: boolean): string {
  const sizes = small ? [80, 48] : [60, 36, 20];
  const opacity = small ? [1, 0.62] : [1, 0.66, 0.38];
  const gap = small ? 8 : 5;
  const stemW = small ? 40 : 32, stemGap = small ? 12 : 10;
  const steps = sizes.slice(1).map((s, i) => (sizes[i] + s) / 2 + gap);
  const reach = steps.reduce((a, b) => a + b, 0) + sizes[sizes.length - 1] / 2;
  const stemX = 128 - (stemW + stemGap + sizes[0] / 2 + reach) / 2;
  const bigCx = stemX + stemW + stemGap + sizes[0] / 2;
  let arms = tile(bigCx, 128, sizes[0]);
  let d = 0;
  steps.forEach((step, i) => {
    d += step;
    const op = ` opacity="${opacity[i + 1]}"`;
    arms += tile(bigCx + d, 128 - d, sizes[i + 1], op) + tile(bigCx + d, 128 + d, sizes[i + 1], op);
  });
  const top = 128 - reach;
  return `<rect x="8" y="8" width="240" height="240" rx="56" fill="${INK}"/>` +
    `<rect x="9" y="9" width="238" height="238" rx="55" fill="none" stroke="#FFFFFF" stroke-opacity=".08" stroke-width="2"/>` +
    `<rect x="${r(stemX)}" y="${r(top)}" width="${stemW}" height="${r(2 * reach)}" rx="${stemW / 2}" fill="${BONE}"/>` + arms;
}

// Monoline glyphs: baseline y=100, x-height 40, ascender 10.
const GLYPHS: Array<{ d: string; w: number }> = [
  { d: "M0,10 V100 M44,40 L4,74 M20,61 L46,100", w: 46 }, // k
  { d: "M0,40 V100 M0,64 C0,48 12,40 30,40", w: 30 }, // r
  { d: "M2,70 H58 A28,28 0 1 0 51.4,88", w: 58 }, // e
  { d: "M0,40 V100 M0,60 C0,47 8,40 18,40 C29,40 34,47 34,60 V100 M34,60 C34,47 42,40 52,40 C63,40 68,47 68,60 V100", w: 68 }, // m
  { d: "M56,40 V100 M56,70 A28,28 0 1 1 0,70 A28,28 0 1 1 56,70", w: 56 }, // a
];
// Parabolic zoom applied to type: the pointer rests on "e".
const ZOOM = [0.72, 0.87, 1.16, 0.87, 0.72];
const STROKE = 13, TRACK = 26, DOT_R = 8.5, DOT_Y = 126;

function wordmarkBody(ink: string) {
  let x = STROKE / 2;
  let paths = "", dot = "";
  GLYPHS.forEach((g, i) => {
    const s = ZOOM[i];
    paths += `<path transform="translate(${r(x)},${r(100 - 100 * s)}) scale(${s})" d="${g.d}" stroke-width="${r(STROKE / s)}"/>`;
    if (i === 2) dot = `<circle cx="${r(x + (g.w * s) / 2)}" cy="${DOT_Y}" r="${DOT_R}" fill="${ACCENT}"/>`;
    x += g.w * s + TRACK;
  });
  const width = x - TRACK + STROKE / 2;
  const top = Math.min(...ZOOM.map((s) => 100 - 90 * s)) - STROKE / 2;
  const bottom = DOT_Y + DOT_R;
  return {
    body: `<g fill="none" stroke="${ink}" stroke-linecap="round" stroke-linejoin="round">${paths}</g>${dot}`,
    vb: { x: 0, y: r(top), w: r(width), h: r(bottom - top) },
  };
}

const svg = (vb: string, body: string, title: string) =>
  `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${vb}" role="img" aria-label="${title}"><title>${title}</title>${body}</svg>\n`;

writeFileSync(`${OUT}/krema-icon.svg`, svg("0 0 256 256", iconBody(false), "Krema"));
writeFileSync(`${OUT}/krema-icon-small.svg`, svg("0 0 256 256", iconBody(true), "Krema"));

for (const [name, ink] of [["", INK], ["-dark", BONE]] as const) {
  const wm = wordmarkBody(ink);
  writeFileSync(`${OUT}/krema-wordmark${name}.svg`, svg(`${wm.vb.x} ${wm.vb.y} ${wm.vb.w} ${wm.vb.h}`, wm.body, "krema"));

  // Horizontal lockup: icon height 256, wordmark x-height optically centred on the icon.
  const wmScale = 1.3, gap = 52;
  const wmW = wm.vb.w * wmScale, wmH = wm.vb.h * wmScale;
  const hW = r(256 + gap + wmW);
  const hY = r(128 - ((70 - wm.vb.y) * wmScale)); // centre of the "e" bowl (y=70) on the icon centre
  writeFileSync(`${OUT}/krema-lockup${name}.svg`, svg(`0 0 ${hW} 256`,
    iconBody(false) +
    `<svg x="${256 + gap}" y="${hY}" width="${r(wmW)}" height="${r(wmH)}" viewBox="${wm.vb.x} ${wm.vb.y} ${wm.vb.w} ${wm.vb.h}">${wm.body}</svg>`,
    "Krema"));

  // Stacked lockup for square formats.
  const sW = Math.max(256, wmW) + 32;
  writeFileSync(`${OUT}/krema-lockup-stacked${name}.svg`, svg(`0 0 ${r(sW)} ${r(256 + 48 + wmH)}`,
    `<g transform="translate(${r((sW - 256) / 2)},0)">${iconBody(false)}</g>` +
    `<svg x="${r((sW - wmW) / 2)}" y="${256 + 48}" width="${r(wmW)}" height="${r(wmH)}" viewBox="${wm.vb.x} ${wm.vb.y} ${wm.vb.w} ${wm.vb.h}">${wm.body}</svg>`,
    "Krema"));
}
console.log("wrote", OUT);
