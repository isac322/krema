---
name: docs
description: "Run docs-writer agent for README, CHANGELOG, release notes, and documentation audit."
user_invocable: true
---

# /docs — Documentation management skill

Run the `docs-writer` agent for various documentation tasks.

## Usage

The user invokes `/docs` with an optional subcommand:

- `/docs readme` — Update README.md based on current feature catalog
- `/docs changelog` — Generate/update CHANGELOG.md from recent git history
- `/docs release {version}` — Write release notes for the specified version
- `/docs audit` — Audit all documents for consistency (version, features, staleness)
- `/docs` (no arguments) — Diagnose stale documents and suggest what needs updating

## Implementation

Based on the subcommand, spawn the `docs-writer` agent with an appropriate prompt:

### `/docs readme`
Spawn `docs-writer` with prompt:
"Update README.md. Read marketing/positioning.md, marketing/briefs/features.md, marketing/strategy.md, and ROADMAP.md first. Rewrite README.md with value-first structure, generate Image Specs for key visuals. Report what was changed and what images are needed."

### `/docs changelog`
Spawn `docs-writer` with prompt:
"Generate or update CHANGELOG.md. Read the current CMakeLists.txt version and git log since the last changelog entry. Transform commits into user-facing language following Keep a Changelog format. Report the changes added."

### `/docs release {version}`
Spawn `docs-writer` with prompt:
"Write release notes for version {version}. Read CHANGELOG.md for this version's changes, ROADMAP.md for milestone context, and marketing/briefs/features.md for feature descriptions. Create docs/releases/v{version}.md with narrative format. Also update metainfo.xml <releases> section. Include Image Specs for major visual changes. Report what was created."

### `/docs audit`
Spawn `docs-writer` with prompt:
"Audit all project documents for consistency. Check: (1) version numbers match CMakeLists.txt across CHANGELOG.md, metainfo.xml, release notes; (2) README features match ROADMAP.md implemented items; (3) metainfo.xml keywords match marketing/strategy.md; (4) screenshots are not stale. Report all inconsistencies found and recommended fixes."

### `/docs` (no arguments)
Spawn `docs-writer` with prompt:
"Diagnose which documents need updating. Check git log for recent changes, compare ROADMAP.md progress against README.md content, check if CHANGELOG.md is up to date with the current version in CMakeLists.txt. Report which documents are stale and what specific updates are needed, prioritized by importance."
