# Documentation Standards

## Document Inventory

| Document | Owner Agent | Update Trigger | Source of Truth |
|----------|-------------|----------------|-----------------|
| `README.md` | docs-writer | Milestone completion, major feature | `marketing/briefs/features.md` + `ROADMAP.md` |
| `CHANGELOG.md` | docs-writer (Unreleased: all agents) | Feature/fix impl + version bump | `git log` + `CMakeLists.txt` version |
| `docs/releases/v*.md` | docs-writer | Version bump / release | `CHANGELOG.md` + feature highlights |
| `metainfo.xml` | docs-writer | Version bump, keyword changes | `CMakeLists.txt` version + `marketing/strategy.md` keywords |
| `.desktop` Comment | docs-writer | Positioning changes | `marketing/positioning.md` |
| `marketing/blog/*` | content-creator | Launch / announcement | `marketing/briefs/` |
| `marketing/social/*` | content-creator | Launch / announcement | `marketing/briefs/` |
| `marketing/briefs/features.md` | feature-analyst | Milestone completion | Source code analysis |
| `marketing/briefs/competitive.md` | competitive-analyst | Quarterly / before announcements | Web research |

## Document Pipeline

Documents flow in sequence — each stage feeds the next:

```
feature-analyst → marketing/briefs/features.md
       ↓
competitive-analyst → marketing/briefs/competitive.md
       ↓
docs-writer → README.md, CHANGELOG.md, release notes, metainfo.xml
       ↓
content-creator → marketing/blog/, marketing/social/
       ↓
distribution-manager → marketing/distribution/
```

Always run docs-writer BEFORE content-creator — marketing content references the official docs.

## Version Consistency

`CMakeLists.txt` `project(krema VERSION x.y.z)` is the **single source of truth** for version.

All documents must match:
- `CHANGELOG.md` latest `## [x.y.z]` header
- `docs/releases/vx.y.z.md` filename and content
- `metainfo.xml` `<release version="x.y.z">`
- `packaging/arch/PKGBUILD` `pkgver=x.y.z`
- GitHub release tag and body (`gh release create`)
- Any version references in README.md

## CHANGELOG Format

Follow [Keep a Changelog](https://keepachangelog.com/):

```markdown
## [Unreleased]

### Added
- New feature description (past tense, user-facing)

## [x.y.z] - YYYY-MM-DD

### Added
- Released feature description

### Changed
- Modified behavior description

### Fixed
- Bug fix description

### Removed
- Removed feature description
```

### Unreleased Workflow (CRITICAL)

`## [Unreleased]` must ALWAYS exist at the top of the changelog (below the header).

**When to add entries:**
- Every user-facing code change (feature, bug fix, behavior change) MUST add an entry
- Internal-only changes (refactoring, CI, agent config) are NOT recorded
- Any agent (main or sub) that implements a feature/fix is responsible for updating Unreleased

**When releasing a version:**
1. Move ALL entries from `## [Unreleased]` to a new `## [x.y.z] - YYYY-MM-DD` section
2. Leave `## [Unreleased]` empty (with no categories) — ready for the next cycle
3. The new version section goes directly below `## [Unreleased]`

**docs-writer** is the primary owner, but ALL agents that modify user-facing behavior must update Unreleased.

### General Rules
- User-facing language, not commit messages
- Past tense ("Added", not "Add")
- Newest version first
- Link to release notes for details

## GitHub Release Notes

GitHub release pages are indexed by search engines — treat them as SEO and value communication opportunities, not just developer changelogs.

### Structure

```markdown
{1-2 sentence intro with SEO keywords and user-facing value summary}

### Added
- {User benefit first} — {technical detail as backup}

### Changed
...

### Fixed
...
```

### Rules

- **Lead with value, not implementation**: "Live window previews on hover" not "Implemented PipeWire stream capture"
- **SEO keyword integration**: Naturally include primary keywords from `marketing/strategy.md` in the intro sentence (e.g., "KDE Plasma 6 dock", "Latte Dock", "parabolic zoom", "Wayland")
- **First/major releases**: Include a brief project intro ("Krema is a lightweight dock for KDE Plasma 6...")
- **Internal refactors**: Reframe as user benefit ("Faster startup" or "More reliable settings") or omit if no user-facing impact
- **Developer-only items** (CI, agent config, code style): Omit from release notes entirely
- **Consistency**: Keep a Changelog categories (Added/Changed/Fixed/Removed), same as CHANGELOG.md
- **Language**: English, past tense, user-facing

## Language

All official documents are written in **English**:
- README.md, CHANGELOG.md, release notes, metainfo.xml, .desktop files
- Code comments and commit messages (already enforced in CLAUDE.md)

## SEO Rules

- Naturally include primary keywords from `marketing/strategy.md`
- No keyword stuffing — every keyword must read naturally in context
- **No duplicate phrasing** across documents (README vs metainfo vs .desktop must each use unique wording)
- Alt text on all images includes relevant SEO keywords

## Latte Dock References

- ONLY use "spiritual successor" — never "replacement", "fork", or "clone"
- Always acknowledge Latte Dock's legacy with respect
- Credit the Latte Dock community where appropriate

## Image Rules

### Directory Structure
- Screenshots: `docs/screenshots/` (PNG, descriptive filenames)
- Design images (icons, banners): `docs/images/`
- Image specifications: `docs/image-specs/` (markdown files with capture instructions or AI prompts)

### Alt Text
- Every image must have alt text
- Alt text includes relevant SEO keywords naturally
- Descriptive: what the image shows, not just "screenshot"

### Freshness
- After any visual change (theme, layout, animation), re-capture affected screenshots
- Stale screenshots are worse than no screenshots — they erode trust

### Design Images
- Store Nano Banana (or other AI tool) prompts alongside the image spec in `docs/image-specs/`
- Format: `docs/image-specs/{image-name}.md` with the full prompt and usage context

## Content Quality

- Claims must be verifiable from implemented features (check ROADMAP.md checkboxes)
- Planned features explicitly labeled as "Planned" or "Coming soon"
- Screenshots must be current — re-capture after visual changes
- All marketing content reviewed by human before publishing
