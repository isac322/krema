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

## KDE Plasma-First (Mandatory)

Krema는 KDE Plasma 전용 앱이다. 다른 데스크톱 환경은 고려하지 않는다.

### 원칙
- Qt API와 KDE API가 모두 존재할 때, 항상 KDE API를 우선 사용
- KDE HIG(Human Interface Guidelines)를 따를 것
- Kirigami / Kirigami Addons 컴포넌트를 plain QQC2보다 우선 사용

### Settings UI
- 설정 다이얼로그는 반드시 `FormCard` (`org.kde.kirigamiaddons.formcard`) 사용
- `FormCardPage` + `FormHeader` + `FormCard` + 빌트인 delegates 패턴
- 빌트인 delegate가 없는 컨트롤(e.g. slider)은 `AbstractFormDelegate` 확장
- 수동 `GridLayout + QQC2.Label` 조합 금지

### Color & Theming
- 하드코딩된 색상 금지 — 항상 `Kirigami.Theme` 색상 사용
- 간격/크기는 `Kirigami.Units` 사용 (gridUnit, smallSpacing, largeSpacing)

### Configuration
- 설정 저장: KConfig (현재) → 향후 KConfigXT(.kcfg) 마이그레이션 예정
- 번역: 모든 사용자 대면 문자열에 `i18n()` 사용

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
- Formula: surfaceHeight = panelHeight + max(ceil(iconSize * (maxZoomFactor - 1.0)), tooltipReserve) + floatingPadding
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
