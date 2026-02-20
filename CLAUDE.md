# Krema Development Rules

## KDE Workflow (Mandatory)

1. Read relevant docs in `docs/kde/` first
2. If info is missing → check `/usr/include/` headers directly
3. Add verified info to `docs/kde/` and commit
4. Only then implement based on verified API

**Never guess KDE APIs — always verify from headers.**

## Knowledge Base

- `docs/kde/README.md` — Full index with milestone mapping
- Add/update docs whenever new KDE knowledge is verified
- Record bug-fix lessons in `docs/kde/lessons-learned.md`

## Code Rules

- C++23, Qt 6, KDE Frameworks 6
- pre-commit hook: clang-format
- English commit messages following Conventional Commits (`feat:`, `fix:`, `docs:`, `refactor:`, etc.)
- Documentation in English

## Build

```sh
cmake --preset default
cmake --build build
```
