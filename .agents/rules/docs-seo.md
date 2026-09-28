---
description: "Apply when writing or editing public-facing text: README, metainfo, .desktop, release notes, marketing, or docs describing features or positioning."
---

# Documentation and SEO

- `.claude/positioning.yml` is the single source of truth for positioning and keywords; `marketing/strategy.md` holds the SEO keyword tiers. Read them before writing; never hardcode keywords elsewhere.
- Latte Dock: say "spiritual successor" only — never "replacement", "fork", or "clone".
- Do not repeat the same phrasing across documents (README, metainfo, and .desktop each get their own wording).
- Use concrete numbers (e.g. "6 background styles", "6 attention animations").
- After code changes that affect described features, run `python3 scripts/check_docs_seo.py`.

## Changes that require a docs check

| Changed file | What to check |
|---|---|
| `src/**/*.cpp`, `src/**/*.h` | README feature descriptions |
| `src/qml/*.qml` | README visual features |
| `src/config/krema.kcfg` | Settings-related docs |
| `CMakeLists.txt` | Build dependencies and version |
| `.claude/positioning.yml` | Every SEO-bearing file |
| `marketing/strategy.md` | Keywords (metainfo, README) |
