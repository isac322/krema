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

## Token Efficiency

### Read Only What You Need
- docs/kde/: Read ONLY the specific doc relevant to the current task, not all 17 files
- Use docs/kde/README.md index to identify which doc to read
- For source files: use Grep to find relevant sections instead of reading entire files
- Do NOT re-read files that haven't changed since your last read

### Session Discipline
- After completing each logical unit of work (feature, bug fix, refactor): suggest running /compact
- If context is getting large, proactively summarize what's been done and what remains

### Subagent Efficiency
- Use model: "haiku" for Task agents doing simple searches, file reads, or pattern matching
- Reserve default model for complex reasoning tasks only
- Keep subagent prompts focused — avoid dumping entire context into subagents

## Anti-Patterns (MUST AVOID)

### QML ↔ C++ State Boundary
- NEVER use QML animation values (y, x, opacity) in C++ business logic
- C++ must have independent "reference state" separate from animated display state
- If C++ needs position: snapshot at animation START/END only, not during

### Wayland Surface Lifecycle
- ALWAYS null-check screen() — it can be nullptr on virtual compositors
- Layer-shell width=0 means "fill screen", not error
- Popup surfaces intercept pointer events — use in-scene rendering for tooltips
- Unset QT_WAYLAND_SHELL_INTEGRATION after dock initialization

### Surface Sizing
- Surface height MUST include animation overflow (zoom, bounce, etc.)
- Formula: surfaceHeight = panelHeight + ceil(iconSize * (maxZoomFactor - 1.0))
- Input region MUST be explicitly set — empty QRegion = accept ALL input, not none

### Mouse/Input Hierarchy
- ONE authoritative level for mouse tracking (panel level, not item level)
- Hover detection MUST check ALL dimensions (X and Y), never single-axis
- DockItem.hoverEnabled must be false — panel handles all hover

### Icon Resources
- sourceSize MUST use max zoom resolution: iconSize * maxZoomFactor
- Never load at resting size when zoom exists

## Architecture Decisions

- Visibility logic owner: DockVisibilityController (C++)
- Mouse tracking owner: main.qml panel-level MouseArea
- Animation state: QML only — never feeds back to C++ logic
- Input region: WaylandDockPlatform manages exclusively
