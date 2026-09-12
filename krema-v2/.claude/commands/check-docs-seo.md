Check whether documentation needs SEO/positioning updates after code changes. Run manually or invoked automatically after relevant file changes.

## Instructions

### Step 1: Identify what changed

```bash
git diff --name-only HEAD~1 HEAD 2>/dev/null || git diff --name-only --cached
```

### Step 2: Map to trigger labels

Check the changed files against the trigger table in `.claude/agents/docs-seo.md § Code Change → Documentation Update Protocol § Trigger Conditions`. If no triggers match, report no-op and stop.

### Step 3: Run the docs-seo evaluation

Read `.claude/positioning.yml` (canonical source of truth), then follow the docs-seo agent's Evaluation Workflow:

1. Assemble the input set for each active trigger label
2. Evaluate drift per output target
3. If no gaps: report no-op
4. If gaps found: make minimal targeted edits following the Post-Edit Quality Checklist

### Step 4: Quick keyword consistency check

Verify these are in sync:
- `positioning.yml § keywords.metainfo_keywords` ↔ `metainfo.xml <keywords>`
- `positioning.yml § keywords.primary` ↔ `CLAUDE.md § Documentation & SEO § Target Keywords`
- `positioning.yml § product.meta_description` ↔ README.md first paragraph (within reasonable paraphrase)
- Feature counts in README match actual implementation

### Step 5: Report

Summarize:
- Triggers that fired
- Files checked
- Changes made (or no-op with reason)
- Any keyword drift detected
