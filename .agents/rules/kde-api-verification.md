---
description: "Apply when planning a feature or using a KDE/Qt API that is new to the codebase."
---

# KDE API Verification

## Workflow
1. Read the relevant doc in `docs/kde/` first. Use the `docs/kde/README.md` index to pick the one doc you need instead of reading them all.
2. If the information is missing, check the installed headers under `/usr/include/`.
3. Add the verified information to `docs/kde/` and commit it.
4. Only then implement against the verified API.

Record bug-fix lessons in `docs/kde/lessons-learned.md`.

## Planning by size
- **Small** (existing APIs only, 1–2 files, no behavior change): no planning phase needed.
- **Medium** (a new KDE API, or 3+ files changed): check `docs/kde/` and verify headers before implementing. First use of any KDE API is at least medium.
- **Large** (3+ new files, or a new KDE module): before implementing,
  - research the KDE/Qt APIs involved and verify the key ones in `/usr/include/`;
  - check the Plasma reference implementation where one exists;
  - list uncertain assumptions explicitly;
  - create or update the matching `docs/kde/` doc.

Do not implement unverified APIs (M6 lost about four hours to this). If an API behaves unexpectedly, a performance target looks unreachable, or an architecture decision needs revision mid-implementation, go back to verification before continuing.
