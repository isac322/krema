# Competitive Analysis Brief

> Run the `competitive-analyst` agent to update this document with fresh web research.
> Last updated: 2026-02-24 (initial skeleton)

## Competitor Status Matrix

| Dock | Plasma 6 Support | Active Development | Wayland | Key Weakness |
|------|------------------|--------------------|---------|--------------|
| Latte Dock | Dead (no Plasma 6) | Discontinued 2022 | Partial | No longer maintained |
| Plank | No (GTK-based) | Minimal maintenance | No | Wrong toolkit, no Wayland |
| Cairo-Dock | No | Nearly dead | No | Legacy codebase |
| DockbarX | No | Minimal | No | XFCE/MATE focused |
| KDE Default Panel | Yes | Yes (KDE team) | Yes | No parabolic zoom, limited dock UX |

## Market Position

Krema occupies a **unique niche**: the only actively developed dock application
with parabolic zoom animations for KDE Plasma 6.

### Opportunity
- Latte Dock's discontinuation left a large gap in the KDE ecosystem
- KDE Plasma 6's Wayland-first approach broke many older dock alternatives
- No competitor offers both parabolic zoom AND Plasma 6 support

### Threat
- KDE Plasma's default panel continues to improve
- Wayland compatibility requirements block legacy docks from adapting

## Krema Advantages

1. **Only dock with parabolic zoom for Plasma 6** — direct Latte Dock migration path
2. **Wayland-native architecture** — competitors would need full rewrites
3. **Deep KDE integration** — uses same libraries as Plasma itself
4. **Modern tech stack** — C++23, PipeWire, QRhi hardware rendering
5. **Active solo development** — consistent progress through 7 milestones

## Messaging Recommendations

- Lead with "Latte Dock successor" — immediately resonates with target audience
- Emphasize Wayland-native — eliminates entire categories of competitors
- Show visual quality (screenshots/GIFs) — parabolic zoom and acrylic background
- Highlight KDE-native integration — distinguishes from generic Linux docks

## TODO: Deep Research Needed

Run `competitive-analyst` agent to research:
- [ ] Current community sentiment about Latte Dock alternatives (Reddit, KDE forums)
- [ ] Any new dock projects that have emerged recently
- [ ] Plank/Cairo-Dock latest release dates and activity
- [ ] KDE Panel recent improvements that might reduce dock demand
