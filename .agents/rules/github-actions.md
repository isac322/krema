---
description: "Apply when adding or editing GitHub Actions workflows under .github/workflows/."
---

# GitHub Actions

- Every `uses:` action MUST reference its latest major version. Look it up before writing (e.g. `gh api repos/<owner>/<action>/releases/latest -q .tag_name`); never copy an older version from memory or another workflow.
- When bumping a major version, read its release notes for breaking changes and adapt the workflow inputs or scripts.
- When touching any workflow, bump outdated actions in all workflows in the same change.
- Least privilege: top-level `permissions: {}`, grant per job only what it needs, and use `persist-credentials: false` on checkout unless the job pushes.
- Pass `${{ }}` expressions into shell steps through `env:`, not inline in `run:` scripts.
