@.claude/work-state.md
@.claude/rules/token-efficiency.md

# Krema Development Rules

## Session Protocol (Mandatory)

### 세션 시작
1. work-state.md에서 현재 작업 상태 확인 (이미 @-import로 로딩됨)
2. ROADMAP.md에서 현재 마일스톤 확인

### 작업 완료 전 (필수)
1. product-quality 에이전트에 테스트 시나리오 생성 요청 (제품/UX 관점 + 리그레션)
2. kwin-mcp로 모든 시나리오 실행 → 실패 시 수정 후 재테스트
3. 모든 시나리오 통과 후에만 작업 완료로 간주
4. `tests/e2e/` 시나리오 영향 확인 (기능 완료 후 1회):
   - `git diff --name-only HEAD` 로 변경된 `src/` 파일 목록 확인
   - 해당 파일이 어떤 시나리오의 Affected Files에 있는지 `tests/e2e/README.md` 매핑 테이블 참조
   - 기능 동작이 변경되었으면 해당 시나리오의 스텝/검증 기준 갱신
   - 새 `src/` 파일 추가 시 관련 시나리오의 Affected Files에 추가

### 세션 종료 시
1. work-state.md 갱신 (진행 중인 작업, 알려진 이슈, 다음 작업)
2. ROADMAP.md 체크박스 갱신 (해당 시)

### 사용자 버그 제보 시 (필수)
1. product-quality 호출 → 놓친 시나리오 분석 → `.claude/product-quality-lessons.md`에 기록
2. 버그 수정 → 추가 리그레션 포함 전체 재테스트

## KDE Workflow (Mandatory)

For feature planning: consult `kde-researcher` agent (see Feature Development Workflow).
For quick lookups during implementation:
1. Read relevant docs in `docs/kde/` first
2. If info is missing → check `/usr/include/` headers directly
3. Add verified info to `docs/kde/` and commit
4. Only then implement based on verified API

**Never guess KDE APIs — always verify from headers.**

## Knowledge Base

- `docs/kde/README.md` — Full index with milestone mapping
- Add/update docs whenever new KDE knowledge is verified
- Record bug-fix lessons in `docs/kde/lessons-learned.md`

## Roadmap-Driven Development (Mandatory)

- Always read `ROADMAP.md` at the start of each session to understand current progress
- Work based on the current milestone (marked with ⬅️ 현재)
- After completing a feature/milestone, update ROADMAP.md checkboxes and milestone markers
- When all items in a milestone are done, mark it ✅ and move ⬅️ to the next milestone
- Keep the tech stack table current when new dependencies are added

## Feature Development Workflow (Mandatory)

Every feature goes through 3 phases with specialized agent participation:

### Phase 1: Planning (규모별 차등)

**규모 판단 기준:**
- **소규모**: 기존 API만 사용, 파일 1-2개 수정, 동작 변경 없음 → Phase 1 생략 가능
- **중규모**: 새 KDE API 사용 또는 파일 3개 이상 수정 → docs/kde/ 확인 + 헤더 검증 필수
- **대규모**: 새 파일 3개 이상 생성 또는 새 KDE 모듈 도입 → 전문가 자문 필수 (kde-researcher, product-quality, arch-auditor)

대규모 Phase 1 체크리스트:
- [ ] kde-researcher에게 KDE/Qt API 조사 의뢰
- [ ] 주요 API 헤더 직접 검증 (`/usr/include/`)
- [ ] Plasma 참조 구현 확인 (해당 시)
- [ ] 불확실한 가정 명시적 목록화
- [ ] docs/kde/ 문서 생성/업데이트

**새 KDE API 첫 사용 = 최소 중규모. 새 파일 3개 이상 생성 = 대규모.**
검증 없이 구현 진행 금지 (실증: M6에서 ~4시간 낭비).

### Phase 2: Implementation
Implement based on the expert-informed plan. Consult specialists again if:
- Unexpected API behavior discovered
- Performance target seems unachievable
- Architecture decision needs revision

### Phase 3: Process Review
After feature completion, run **process-reviewer** to:
- Identify what was missed or done wrong
- Update CLAUDE.md rules, agent definitions, and documentation
- Ensure lessons are captured for future features

## CHANGELOG (Mandatory)

기능 추가, 버그 수정, 동작 변경 등 **사용자에게 영향이 있는 코드 변경** 시 반드시 `CHANGELOG.md`의 `## [Unreleased]` 섹션에 항목을 추가해야 한다.

- 카테고리: `### Added`, `### Changed`, `### Fixed`, `### Removed`
- 언어: 영어, 과거형, 사용자 관점 (커밋 메시지가 아닌 사용자 혜택 중심)
- 내부 리팩토링, CI 변경, 에이전트 설정 등 사용자에게 보이지 않는 변경은 기록하지 않음
- **버전 릴리즈 시**: `## [Unreleased]`의 모든 항목을 `## [x.y.z] - YYYY-MM-DD` 섹션으로 이동하고, `## [Unreleased]`를 비운 채로 유지

## Version Release Checklist (Mandatory)

`/release` 커맨드가 전체 릴리즈 파이프라인을 자동화한다. 수동으로 릴리즈할 경우 아래 순서를 반드시 따른다.

**Phase 1: 릴리즈 커밋 (PKGBUILD 제외)**
1. `CHANGELOG.md` [Unreleased] 카테고리로 semver 자동 판단 (Added→minor, Fixed only→patch)
2. `CMakeLists.txt` 버전 업데이트
3. `CHANGELOG.md`: `## [Unreleased]` → `## [x.y.z - YYYY-MM-DD]`
4. `ROADMAP.md`: 완료 항목 체크, 마일스톤 마커 이동
5. `metainfo.xml`: 릴리즈 엔트리 추가
6. `work-state.md`: 상태 갱신
7. 커밋 (`chore: release vx.y.z`) → 태그 → push

**Phase 2: GitHub Release**
8. `gh release create vx.y.z` — SEO 최적화 릴리즈 노트 (documentation.md 규칙)

**Phase 3: PKGBUILD + AUR (GitHub release 완료 후)**
9. PKGBUILD 의존성 동기화 검증 (`CMakeLists.txt` ↔ PKGBUILD)
10. `pkgver` + `sha256sums` 업데이트 (릴리즈 tarball sha256sum)
11. `.SRCINFO`: `makepkg --printsrcinfo > .SRCINFO` (절대 수동 편집 금지)
12. 커밋 (`chore: update PKGBUILD and .SRCINFO for vx.y.z`) → push
13. AUR 업로드: `git subtree push --prefix=packaging/arch aur master`

## Code Rules

- C++23, Qt 6, KDE Frameworks 6
- pre-commit hook: clang-format
- English commit messages following Conventional Commits (`feat:`, `fix:`, `docs:`, `refactor:`, etc.)
- Documentation in English
- **의존성 추가/제거 시**: `CMakeLists.txt`에 `find_package()`/`target_link_libraries()` 변경 시 `packaging/arch/PKGBUILD`의 `depends`/`makedepends`도 반드시 함께 업데이트

## Performance (Mandatory)

Krema is a dock — visual smoothness and responsiveness are non-negotiable.

- Always use GPU-accelerated APIs (PipeWire: `allowDmaBuf: true`, Qt Quick: QRhi hardware backend)
- Avoid CPU→GPU texture copies; prefer GPU-native paths
- Fallback chain: GPU → CPU → placeholder (never crash/freeze)
- Release GPU resources when not visible; cap concurrent GPU streams (max 4 PipeWire)
- Lazy initialization: don't allocate until first needed
- All animations: 60fps, declarative Qt Quick animations, no JS per-frame updates

## KDE Plasma-First (Mandatory)

Krema는 KDE Plasma 전용 앱이다. 다른 데스크톱 환경은 고려하지 않는다.

- Qt API와 KDE API가 모두 존재할 때, 항상 KDE API를 우선 사용
- KDE HIG를 따를 것. Kirigami / Kirigami Addons를 plain QQC2보다 우선 사용

### Settings UI
- 설정: `FormCard` (`org.kde.kirigamiaddons.formcard`) 필수
- `FormCardPage` + `FormHeader` + `FormCard` + 빌트인 delegates 패턴
- 빌트인 delegate 없으면 `AbstractFormDelegate` 확장. 수동 `GridLayout + QQC2.Label` 금지

### Color & Theming
- 하드코딩 색상 금지 → `Kirigami.Theme` 사용
- 간격/크기: `Kirigami.Units` (gridUnit, smallSpacing, largeSpacing)

### Configuration
- 설정 저장: KConfigXT (.kcfg 스키마, 자동 생성 KremaSettings 클래스)
- 번역: 모든 사용자 대면 문자열에 `i18n()` 사용

## Anti-Patterns (MUST AVOID)

> 전체 교훈 및 상세 분석: `docs/kde/lessons-learned.md` 참조

### Wayland Surface Lifecycle
- ALWAYS null-check screen() — nullptr on virtual compositors
- Layer-shell width=0 = "fill screen", not error
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

### QML Repeater Model Lifecycle
- Repeater.model에 JS 배열 할당 후 교체하면 delegate가 전부 파괴/재생성됨
- GPU 리소스(PipeWire 스트림) delegate에는 ListModel + set()/append()/remove() 사용
- JS 배열 교체가 허용되는 경우: delegate에 비싼 리소스가 없을 때만

### Input Region / Hit Box
- 입력 영역(QRegion mask, MouseArea, HoverHandler 등)은 반드시 실제 UI 컴포넌트의 시각적 크기와 일치해야 함
- "편의를 위해" 입력 영역을 UI보다 크게 설정하면 다른 앱/UI와의 클릭 충돌 발생
- 마진이 필요하면 최소한으로 (40px 이내), 반드시 주석으로 이유 명시

### KConfigXT / QML Singleton
- KConfigSkeleton(Singleton=false)는 생성자에서 load()를 호출하지 않음 → 반드시 수동 load() 필요
- qmlRegisterSingletonInstance는 단일 QML 엔진에서만 접근 가능 (두 번째 엔진에서 null 반환)
- 멀티 엔진 환경(독 + 설정 창)에서는 qmlRegisterSingletonType + 팩토리 콜백 + CppOwnership 사용

### Async State Handling
- 비동기 상태 대기에 Timer(polling) 금지 → KDE/Qt 시그널(dataChanged, rowsInserted 등) 구독
- KDE API가 one-shot(재시도 없음)이면, 관련 시그널 핸들러에서 명시적 재요청
- Timer가 허용되는 유일한 경우: UI 딜레이(debounce, hide delay 등) — 비즈니스 로직용 아님

### Layer-shell Keyboard Focus
- Layer-shell 서피스 간 키보드 포커스 전환은 신뢰할 수 없음 (Wayland 비동기 round-trip)
- 멀티 서피스 앱(독 + 프리뷰)에서는 단일 서피스가 KeyboardInteractivityExclusive를 보유
- 다른 서피스의 키보드 네비게이션은 C++ 프로퍼티 + QML 바인딩으로 구동 (포커스 전환 아님)

### Subagent Result Handling
- Task result가 빈 배열(`"content":[]`)이거나 agentId/usage 메타데이터만 포함하면, resume로 텍스트 요약 요청
- resume 후에도 빈 결과면, 에이전트가 생성한 파일을 직접 Read (docs/kde/, marketing/ 등)
- 동일 주제를 처음부터 재조사하지 말 것 — 에이전트가 이미 파일을 쓴 경우가 많음
- 근본 원인: maxTurns 초과 시 에이전트가 tool call 중 강제 종료되어 텍스트 반환 불가
