# Marketing & SEO Workflow

## Key Documents

| Document | Purpose | Update Timing |
|----------|---------|---------------|
| `marketing/strategy.md` | SEO keywords, channels, launch checklist | Major strategy changes |
| `marketing/positioning.md` | Key messages, value propositions, tone | New features or positioning shifts |
| `marketing/briefs/features.md` | Feature catalog from codebase | Each milestone completion |
| `marketing/briefs/competitive.md` | Competitive landscape analysis | Quarterly or before announcements |

## Agent Pipeline

Marketing agents work in sequence — each stage feeds the next:

```
feature-analyst → marketing/briefs/features.md
       ↓
competitive-analyst → marketing/briefs/competitive.md
       ↓
content-creator → marketing/blog/, marketing/social/
       ↓
distribution-manager → marketing/distribution/
```

Always run feature-analyst and competitive-analyst BEFORE content-creator.

## When to Use Marketing Agents

| Trigger | Action |
|---------|--------|
| Milestone completed | Run `feature-analyst` to update feature catalog |
| Before launch / announcement | Run full pipeline (all 4 agents) |
| New competitor appears | Run `competitive-analyst` |
| Content needed for specific channel | Run `content-creator` with channel target |
| Planning content schedule | Run `distribution-manager` |

## SEO Rules

- README.md must naturally include primary SEO keywords (see `marketing/strategy.md`)
- metainfo.xml `<keywords>` must stay in sync with strategy
- GitHub topics updated when major new features ship
- All public-facing text (README, metainfo, descriptions) in English
- When adding screenshots to README, use descriptive alt text with keywords

## Content Quality Standards

- All marketing content must be reviewed by human before publishing
- Claims must be verifiable from actual implemented features
- Screenshots must be current — re-capture after visual changes
- Never claim planned features as existing unless clearly marked "planned"
- Respect Latte Dock legacy — use "spiritual successor", not "replacement"
