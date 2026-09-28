#!/usr/bin/env bun
// Emits the Krema crema palette as a swatch sheet (palette.svg, text baked to
// paths via usvg/Noto Sans) plus palette.json and palette.css tokens.
// Usage: bun branding/palette/generate.ts        requires nix (resvg, noto-fonts)
import { $ } from "bun";
import { join } from "node:path";

const OUT = import.meta.dir;
const FONT = "Noto Sans";

// Single source of truth; keep in sync with website/css/site.css :root tokens.
const colors: Array<{ name: string; hex: string; role: string }> = [
  { name: "night", hex: "#110C09", role: "deepest background" },
  { name: "ink", hex: "#18110D", role: "surfaces, logo tile, ink on light" },
  { name: "ink-2", hex: "#231A14", role: "raised surfaces" },
  { name: "bone", hex: "#F4ECE0", role: "light surfaces, ink on dark" },
  { name: "bone-dim", hex: "#CDBFAE", role: "body copy on dark" },
  { name: "muted", hex: "#A89684", role: "secondary copy" },
  { name: "caramel", hex: "#D69A5E", role: "accent: tiles, links, highlights" },
  { name: "caramel-deep", hex: "#A8682F", role: "pressed/hover accent" },
];

await Bun.write(
  join(OUT, "palette.json"),
  JSON.stringify(
    {
      name: "Krema crema",
      colors: Object.fromEntries(colors.map((c) => [c.name, { hex: c.hex, role: c.role }])),
    },
    null,
    2,
  ) + "\n",
);

await Bun.write(
  join(OUT, "palette.css"),
  "/* Krema crema palette tokens. Mirrors website/css/site.css :root\n" +
    "   (where --caramel/--caramel-deep appear as the legacy names --blue/--blue-deep). */\n" +
    ":root {\n" +
    colors.map((c) => `  --${c.name}: ${c.hex.toLowerCase()}; /* ${c.role} */`).join("\n") +
    "\n}\n",
);

// ---------- swatch sheet ----------
// Bone page, 2 columns of cards: color chip + name/hex/role in contrasting ink.
const r = (n: number) => Number(n.toFixed(2)).toString();
const text = (x: number, y: number, size: number, s: string, o: { fill?: string; spacing?: number } = {}) =>
  `<text x="${r(x)}" y="${r(y)}" font-family="${FONT}" font-size="${size}"` +
  ` fill="${o.fill ?? "#18110D"}"${o.spacing ? ` letter-spacing="${o.spacing}"` : ""}>${s}</text>`;

const CW = 440, CH = 148, GX = 40, GY = 40, MX = 48, MT = 56;
const W = MX * 2 + CW * 2 + GX, H = MT + 96 + CH * 4 + GY * 3 + 24;
let body = `<rect width="${W}" height="${H}" fill="#F4ECE0"/>`;
body += text(MX, MT + 8, 44, "Krema — crema palette", { spacing: 0.5 });
body += text(MX, MT + 44, 20, "Palette A · shared with the website tokens", { fill: "#A89684" });
colors.forEach((c, i) => {
  const x = MX + (i % 2) * (CW + GX);
  const y = MT + 96 + Math.floor(i / 2) * (CH + GY);
  const [cr, cg, cb] = [1, 3, 5].map((k) => parseInt(c.hex.slice(k, k + 2), 16));
  const ink = cr * 0.299 + cg * 0.587 + cb * 0.114 > 140 ? "#18110D" : "#F4ECE0";
  body +=
    `<rect x="${x}" y="${y}" width="${CW}" height="${CH}" rx="16" fill="${c.hex}"/>` +
    `<rect x="${x + 0.5}" y="${y + 0.5}" width="${CW - 1}" height="${CH - 1}" rx="15.5" fill="none" stroke="#18110D" stroke-opacity="0.12"/>` +
    text(x + 24, y + 52, 26, c.name, { fill: ink }) +
    text(x + 24, y + 88, 22, c.hex, { fill: ink, spacing: 1 }) +
    text(x + 24, y + 122, 17, c.role, { fill: ink });
});

const fontDir = (await $`nix build nixpkgs#noto-fonts --no-link --print-out-paths`.text()).trim();
const font = join(fontDir, "share/fonts/noto/NotoSans.ttf");
const raw =
  `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" ` +
  `role="img" aria-label="Krema crema palette"><title>Krema — crema palette</title>${body}</svg>\n`;
const baked = await $`echo ${raw} | nix shell nixpkgs#resvg -c usvg --use-font-file ${font} --font-family ${FONT} --quiet - -c`.text();
await Bun.write(
  join(OUT, "palette.svg"),
  baked
    .replace("<svg ", `<svg role="img" aria-label="Krema crema palette" `)
    .replace(/(<svg[^>]*>)/, "$1<title>Krema — crema palette</title>"),
);
console.log(`wrote ${OUT}/{palette.svg,palette.json,palette.css}`);
