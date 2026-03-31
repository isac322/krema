# Krema Test Plan — Feature-First Design

> 기능(Feature)을 빠짐없이 나열하고, 각 기능을 가장 잘 테스트할 수 있는 레이어를 결정한다.
> E2E(kwin-mcp 시각 검증)는 이 문서의 범위 밖이다.

## Test Layer 정의

| Layer | Framework | 환경 | CI 실행 | 용도 |
|-------|-----------|------|---------|------|
| **L1** | Catch2 | 디스플레이 없음 | 즉시 | 순수 C++ 로직 (수학, 기하, 색상) |
| **L2** | QTest | `QT_QPA_PLATFORM=offscreen` | 즉시 | QObject 시그널/슬롯, 모델, 상태 머신 |
| **L3** | QtQuickTest | `offscreen` + `QSG_RHI_BACKEND=null` | 즉시 | QML 컴포넌트 바인딩, 레이아웃, 이벤트 |
| **L4** | QTest + kwin --virtual | Wayland compositor | KWin 필요 | Layer-shell, 실제 Wayland 프로토콜 |

---

## Feature Catalog

### Area 1: App/Task Management

#### F1.1 — Running apps shown in dock
**설명**: 실행 중인 앱이 독에 아이콘으로 표시된다.
**구현**: `DockModel` → `TaskManager::TasksModel` wrapper
**최적 레이어**: L2
**테스트 전략**:
- `TasksModel`을 mock하고 `DockModel`에 주입 (현재 내부 생성이므로 seam 추가 필요)
- Mock에 앱 항목 추가 → `tasksModel()->rowCount()` 변화 검증
- `iconName(i)`, `launcherUrl(i)`, `appId(i)` 반환값 검증
**필요 작업**: `DockModel` 생성자에 `TasksModel*` 외부 주입 옵션 추가 (테스트 seam)

#### F1.2 — Pinned launchers
**설명**: 앱을 실행하지 않아도 독에 고정(pin)할 수 있다.
**구현**: `DockModel::pinnedLaunchers`, `DockActions::togglePinned`
**최적 레이어**: L2
**테스트 시나리오**:
- `setPinnedLaunchers(["a.desktop", "b.desktop"])` → `pinnedLaunchers()` == `["a.desktop", "b.desktop"]`
- `pinnedLaunchersChanged` 시그널 발생 검증
- `togglePinned(i)` → 핀 상태 토글 + 시그널
- `isPinned(i)` 정확성 (launcher vs running app 구분)
- 빈 리스트, 중복 URL, 잘못된 URL 엣지 케이스

#### F1.3 — Click to activate/launch
**설명**: 독 아이콘 클릭으로 앱 활성화 또는 런처 실행.
**구현**: `DockActions::activate(index)`
**최적 레이어**: L2
**테스트 시나리오**:
- 실행 중인 앱 index → `TasksModel::requestActivate` 호출 검증 (mock)
- 런처 index → `TasksModel::requestNewInstance` 호출 검증
- 범위 밖 index → 크래시 없음

#### F1.4 — Context menu actions
**설명**: 우클릭 메뉴 — Pin/Unpin, Close, New Instance, Settings, About, Clear Notifications.
**구현**: `DockContextMenu::showForTask(index)`
**최적 레이어**: L2 (메뉴 구성 로직), L4 (실제 QMenu 표시)
**테스트 시나리오 (L2)**:
- `showForTask(i)` 호출 시 `settingsRequested`/`aboutRequested`/`visibleChanged` 시그널 연결 검증
- 핀된 앱 → "Unpin" 메뉴 항목, 안 핀된 앱 → "Pin" 메뉴 항목 (메뉴 구성 로직)
**제약**: `QMenu`는 실제 윈도우 시스템 필요 → 메뉴 *표시*는 L4, 메뉴 *구성 로직*은 L2

#### F1.5 — Mouse wheel to cycle windows
**설명**: 독 아이콘 위에서 마우스 휠 → 그룹 내 윈도우 순환.
**구현**: `DockActions::cycleWindows(index, forward)`
**최적 레이어**: L2
**테스트 시나리오**:
- 단일 윈도우 앱 → 포커스만 (activate 호출)
- 다중 윈도우 앱 → forward=true로 순환, forward=false로 역순환
- 빈 윈도우 리스트 → no-op

#### F1.6 — Drag-and-drop reorder
**설명**: 독 아이콘을 드래그하여 순서 변경.
**구현**: `DockActions::moveTask(from, to)`
**최적 레이어**: L2 (로직), L3 (QML drag 동작)
**테스트 시나리오 (L2)**:
- `moveTask(0, 2)` → 성공 시 true 반환, 핀 순서 업데이트
- `moveTask(0, 0)` → no-op
- 범위 밖 index → false 반환
- 핀되지 않은 런닝 앱 이동 시 동작

#### F1.7 — Drag file onto app to open
**설명**: 파일을 독 아이콘에 드래그하면 해당 앱으로 파일 열기.
**구현**: `DockActions::openUrlsWithTask(index, urls)`
**최적 레이어**: L2
**테스트 시나리오**:
- 유효한 URL 리스트 → `TasksModel` 에 전달 검증
- 빈 URL 리스트 → no-op

#### F1.8 — Add launcher via drag
**설명**: URL 또는 .desktop 파일을 독에 드래그하면 런처 추가.
**구현**: `DockActions::addLauncher(url)`, `DockModel::isDesktopFile(url)`
**최적 레이어**: L2
**테스트 시나리오**:
- `.desktop` URL → `addLauncher` 성공, `pinnedLaunchersChanged` 시그널
- `applications:` 스킴 URL → 성공
- 일반 파일 URL → `isDesktopFile` == false, 동작 정의에 따라 처리
- 이미 핀된 런처 URL → 중복 처리

---

### Area 2: Visual/Appearance

#### F2.1 — Parabolic zoom calculation
**설명**: 마우스 위치에 따른 가우시안 줌 팩터 계산.
**구현**: `krema::parabolicZoomFactor(distance, iconSize, maxZoom)`
**최적 레이어**: L1 ✅ (이미 테스트됨)
**테스트 시나리오** (기존 + 추가):
- distance=0 → maxZoom 반환
- 큰 distance → ~1.0
- maxZoom < 1.0 → 1.0 (clamped)
- 양/음 distance 대칭
- iconSize에 따른 곡선 스케일링
- **추가**: maxZoom=1.0 정확히 → 모든 distance에서 1.0
- **추가**: NaN/infinity distance → 안전한 반환값

#### F2.2 — Icon size configuration
**설명**: 아이콘 크기 24~96px 조절.
**구현**: `KremaSettings::iconSize()`, `DockView::updateSize()`
**최적 레이어**: L2 (설정 저장/로드), L1 (크기 계산)
**테스트 시나리오**:
- `surfaceHeight(iconSize, maxZoom, floating)` — iconSize 변화에 따른 출력 검증 (L1)
- `panelBarHeight(iconSize, floating)` — 정확한 계산 (L1)
- ScreenSettings에서 iconSize override 시 올바른 값 반환 (L2)

#### F2.3 — Icon spacing
**설명**: 아이콘 간격 0~16px 조절.
**구현**: KConfig `IconSpacing`, QML에서 직접 사용
**최적 레이어**: L3 (QML 바인딩)
**테스트 시나리오**:
- spacing=0일 때 아이콘 간 간격 없음 (QML item 위치 검증)
- spacing=16일 때 간격 적용

#### F2.4 — Corner radius
**설명**: 패널 모서리 둥글기 0~24px.
**구현**: KConfig `CornerRadius`, QML `Rectangle.radius`
**최적 레이어**: L2 (ScreenSettings override 테스트), L3 (QML 바인딩)

#### F2.5 — Floating/docked mode
**설명**: 독이 화면 가장자리에서 떠있거나(floating) 붙어있거나(docked).
**구현**: KConfig `Floating`, `DockView::floatingPadding`, surface geometry
**최적 레이어**: L1 (geometry 계산)
**테스트 시나리오**:
- `surfaceHeight(iconSize, maxZoom, floating=true)` vs `floating=false` — 패딩 차이 검증
- `panelBarHeight(iconSize, floating=true)` — floatingMargin 포함

#### F2.6 — Icon normalization
**설명**: 아이콘 내부 투명 패딩을 자동 감지하여 크기 정규화.
**구현**: `TaskIconProvider::findContentBounds`, `analyzeIcon`, `normalizePixmap`
**최적 레이어**: L2 (QPixmap/QImage 연산, offscreen에서 동작)
**테스트 시나리오**:
- `findContentBounds`: 완전 투명 이미지 → 빈 QRect
- `findContentBounds`: 중앙에 콘텐츠 있는 이미지 → 정확한 바운딩 박스
- `findContentBounds`: 가장자리에 1px 콘텐츠 → 전체 이미지 크기
- `analyzeIcon`: 정사각형 아이콘(fillRatio ≈ 1.0) vs 원형 아이콘(fillRatio ≈ π/4)
- `requestPixmap`: normalization=true vs false에서 출력 크기 동일하지만 내용 다름
- `requestPixmap`: 존재하지 않는 아이콘 이름 → 폴백 아이콘 반환 (null 아님)
- 캐시 동작: 같은 아이콘 두 번 요청 → 캐시 히트
- `clearCache()` 후 → 캐시 미스

#### F2.7 — Icon scale
**설명**: 아이콘 크기 비율 0.5~1.0 조절 (균일한 패딩 추가).
**구현**: `TaskIconProvider::setIconScale`, `requestPixmap` 내 shrink 로직
**최적 레이어**: L2
**테스트 시나리오**:
- scale=1.0 → 아이콘 크기 그대로
- scale=0.5 → 캔버스 크기 유지, 내부 아이콘 50% 축소
- scale 경계값: 0.5 미만 → clamp, 1.0 초과 → clamp

#### F2.8 — Background styles (4종)
**설명**: PanelInherit, Transparent, Tinted, Acrylic 배경.
**구현**: `backgroundstyle.h/.cpp` — `computeBackgroundColor`, `styleUsesBlur`, `isStyleAvailable`
**최적 레이어**: L1 (색상 계산), L2 (KColorScheme 의존)
**테스트 시나리오**:
- `computeBackgroundColor(Transparent, ...)` → `Qt::transparent`
- `computeBackgroundColor(Tinted, "#ff0000", 0.5, false, false)` → RGB(255,0,0) + alpha=0.5
- `computeBackgroundColor(Tinted, "invalid", ...)` → Header 색상 폴백
- `computeBackgroundColor(PanelInherit, ...)` → KColorScheme Header 색상 + opacity
- `computeBackgroundColor(..., useAccentColor=true)` → Selection 색상
- `styleUsesBlur(PanelInherit)` → true
- `styleUsesBlur(Transparent)` → false
- `styleUsesBlur(Acrylic)` → true
- `styleUsesBlur(Tinted)` → false
- `isStyleAvailable(*)` → 현재 항상 true

#### F2.9 — Background opacity
**설명**: 배경 투명도 0.1~1.0 조절.
**구현**: KConfig `BackgroundOpacity`, `computeBackgroundColor` opacity 인자
**최적 레이어**: L1 (`computeBackgroundColor`에 opacity 전달 시 alpha 검증)
**테스트 시나리오**:
- opacity=1.0 → alpha=1.0
- opacity=0.1 → alpha≈0.1
- opacity와 style 조합 (Transparent에서는 opacity 무관하게 transparent)

#### F2.10 — Shadow configuration
**설명**: 그림자 활성화, 광원 위치(X/Y/Z), 광원 반경, 색상, 강도, 높이.
**구현**: KConfig 11개 항목, QML shader에서 사용
**최적 레이어**: L2 (설정 저장/로드), L3 (QML 바인딩)
**테스트 시나리오**:
- 각 설정값의 min/max 경계 검증 (KConfig 스키마 범위)
- ShadowEnabled=false 시 shader 비활성화 (QML visible 바인딩)

---

### Area 3: Dock Position & Layout

#### F3.1 — 4 screen edges
**설명**: 독 위치 — Top, Bottom, Left, Right.
**구현**: `DockPlatform::Edge`, `DockView::setEdge`, surface geometry
**최적 레이어**: L1 (기하 계산), L2 (edge 변경 시 시그널)
**테스트 시나리오**:
- `surfaceHeight` + `panelBarHeight` — 각 edge에서의 계산 (L1)
- `computeDockInputRegion` — edge별 trigger strip 위치 (L1, 이미 테스트됨)
- `computeDockScreenRect` — edge별 스크린 좌표 (L1, 이미 테스트됨)
- `DockView::setEdge` → `edgeChanged` 시그널 + `isVertical` 변경 (L2)

#### F3.2 — Indicator dots
**설명**: 실행 중인 앱 아래/옆에 점 표시기.
**구현**: QML `DockItem.qml` — `IsWindow` role 기반 visible 바인딩
**최적 레이어**: L3
**테스트 시나리오**:
- IsWindow=true 아이템 → 인디케이터 visible
- IsWindow=false (런처만) → 인디케이터 hidden
- 여러 윈도우 그룹 → 인디케이터 수/크기 변화

#### F3.3 — Tooltips
**설명**: 독 아이콘 위에 마우스 올리면 앱 이름 툴팁 표시.
**구현**: QML tooltip 컴포넌트, hover 바인딩
**최적 레이어**: L3
**테스트 시나리오**:
- 아이템 hover → 툴팁 visible, 텍스트 == 앱 이름
- hover 해제 → 툴팁 hidden

---

### Area 4: Visibility Control

#### F4.1 — AlwaysVisible mode
**설명**: 독이 항상 보인다.
**구현**: `DockVisibilityController::evaluateVisibility`
**최적 레이어**: L2
**테스트 시나리오**:
- mode=AlwaysVisible → `isDockVisible()` == true (항상)
- setHovered(false) → 여전히 visible
- toggleVisibility() → no-op (AlwaysVisible에서는 토글 비활성)

#### F4.2 — AutoHide mode
**설명**: 독이 기본 숨김, 가장자리에 마우스 올리면 표시.
**구현**: `DockVisibilityController` show/hide 타이머
**최적 레이어**: L2 (mock DockPlatform 사용)
**테스트 시나리오**:
- mode=AutoHide, 초기 상태 → `isDockVisible()` == false
- `setHovered(true)` → showTimer 시작 → 타임아웃 후 visible
- `setHovered(false)` → hideTimer 시작 → 타임아웃 후 hidden
- hover 중 `setHovered(false)` → showTimer 취소
- visible 상태에서 `setHovered(false)` → hideTimer 후 hidden

#### F4.3 — DodgeWindows mode
**설명**: 윈도우가 독과 겹칠 때 숨김.
**구현**: `DockVisibilityController::hasOverlappingWindow`, `m_overlapModel`
**최적 레이어**: L2 (overlapModel mock 필요)
**테스트 시나리오**:
- 겹치는 윈도우 없음 → visible
- 겹치는 윈도우 있음 → hidden
- 겹치는 윈도우 제거 → visible 복귀
- DodgeActiveOnly=true: 비활성 윈도우 겹침 → 여전히 visible
- DodgeActiveOnly=true: 활성 윈도우 겹침 → hidden
**제약**: `m_overlapModel`이 내부 생성이므로, 테스트용 seam 필요하거나 L4에서 실제 윈도우로 테스트

#### F4.4 — DodgeActiveOnly
**설명**: 활성 윈도우만 dodge할지 전체 윈도우를 dodge할지.
**구현**: `DockVisibilityController::setDodgeActiveOnly`
**최적 레이어**: L2 (F4.3과 동일 mock)

#### F4.5 — Show/hide delay
**설명**: 독 표시/숨김 전 딜레이 (ms).
**구현**: `DockVisibilityController::setShowDelay`, `setHideDelay`
**최적 레이어**: L2
**테스트 시나리오**:
- showDelay=0 → hover 즉시 visible
- showDelay=500 → 500ms 대기 후 visible
- hideDelay=0 → unhover 즉시 hidden
- hideDelay=1000 → 1000ms 대기 후 hidden
- 딜레이 중 반대 동작 → 타이머 취소 검증

#### F4.6 — Interaction lock
**설명**: 컨텍스트 메뉴나 설정창이 열려있으면 독이 안 숨겨진다.
**구현**: `DockVisibilityController::setInteracting`
**최적 레이어**: L2
**테스트 시나리오**:
- AutoHide + `setInteracting(true)` → 항상 visible
- `setInteracting(true)` 2번 → `setInteracting(false)` 1번 → 여전히 visible (ref count)
- `setInteracting(false)` 2번 → visible 재평가 (hideTimer 시작)
- 이미 0인 상태에서 `setInteracting(false)` → count 음수 안 됨 (qMax(0, ...))

#### F4.7 — Keyboard navigation prevents hiding
**설명**: 키보드 네비게이션 중 독이 안 숨겨진다.
**구현**: `DockVisibilityController::setKeyboardActive`
**최적 레이어**: L2
**테스트 시나리오**:
- AutoHide + `setKeyboardActive(true)` → visible 유지
- `setKeyboardActive(false)` + not hovered → hideTimer 시작
- AlwaysVisible에서 `setKeyboardActive(false)` → hideTimer 시작 안 함
- `setKeyboardActive(true)` → `m_platform->setKeyboardInteractivity(true)` 호출 검증 (mock)

#### F4.8 — Toggle visibility via shortcut
**설명**: 글로벌 단축키로 독 표시/숨김 토글.
**구현**: `DockVisibilityController::toggleVisibility`
**최적 레이어**: L2
**테스트 시나리오**:
- AutoHide + hidden → `toggleVisibility()` → visible
- AutoHide + visible → `toggleVisibility()` → hidden
- AlwaysVisible → `toggleVisibility()` → no-op

---

### Area 5: Window Previews

#### F5.1 — Preview popup lifecycle
**설명**: 독 아이콘 hover 시 프리뷰 팝업 표시/숨김.
**구현**: `PreviewController::showPreview`, `hidePreview`, `hidePreviewDelayed`
**최적 레이어**: L2 (로직만, QQuickView 생성은 mock/skip)
**테스트 시나리오**:
- `showPreview(i, pos, extent)` → `visible` == true, `parentIndex` == i
- `hidePreview()` → `visible` == false
- `hidePreviewDelayed()` → 타이머 후 hidden
- `cancelHide()` → 대기 중인 숨김 취소
- 이미 같은 index로 show 중 → 위치만 업데이트
**제약**: `initialize()`가 QQuickView layer-shell 생성 → L2에서는 `initialize()` 없이 로직만 테스트하거나 seam 분리 필요

#### F5.2 — Multi-window preview
**설명**: 그룹된 앱의 여러 윈도우 목록 표시.
**구현**: `PreviewController::isGrouped`, `windowIds`, `rootModelIndex`
**최적 레이어**: L2
**테스트 시나리오**:
- 단일 윈도우 앱 → `isGrouped()` == false
- 다중 윈도우 앱 → `isGrouped()` == true, `windowIds()` 개수 일치

#### F5.3 — Click preview to activate
**설명**: 프리뷰 썸네일 클릭 → 해당 윈도우 활성화.
**구현**: QML click handler → `TasksModel::requestActivate`
**최적 레이어**: L3 (QML 이벤트 핸들링)

#### F5.4 — Close button in preview
**설명**: 프리뷰 썸네일의 닫기 버튼.
**구현**: QML click handler → `TasksModel::requestClose`
**최적 레이어**: L3

#### F5.5 — Preview hide delay
**설명**: 독→프리뷰 마우스 이동 시 부드러운 전환 (delay).
**구현**: `PreviewController::setHideDelay`, `hidePreviewDelayed`, `cancelHide`
**최적 레이어**: L2
**테스트 시나리오**:
- 독에서 마우스 나감 → `hidePreviewDelayed()` → 프리뷰로 마우스 진입 → `cancelHide()`
- 프리뷰에서도 마우스 나감 → 최종 숨김

#### F5.6 — Preview keyboard navigation
**설명**: 키보드로 프리뷰 썸네일 탐색 및 활성화/닫기.
**구현**: `PreviewController::startPreviewKeyboardNav`, `navigatePreviewThumbnail`, `activatePreviewThumbnail`, `closePreviewThumbnail`
**최적 레이어**: L2
**테스트 시나리오**:
- `startPreviewKeyboardNav()` → `previewKeyboardActive` == true, `focusedThumbnailIndex` == 0
- `navigatePreviewThumbnail(1)` → index 증가
- `navigatePreviewThumbnail(-1)` → index 감소
- 경계: 마지막 인덱스에서 +1 → 순환 또는 클램프
- `endPreviewKeyboardNav()` → `previewKeyboardActive` == false
- `activatePreviewThumbnail()` → 해당 윈도우 활성화 호출
- `closePreviewThumbnail()` → 해당 윈도우 닫기 호출

---

### Area 6: Notifications & Attention

#### F6.1 — Notification badge count
**설명**: 앱별 미읽은 알림 개수 뱃지.
**구현**: `NotificationTracker::Notify`, `CloseNotification`, `unreadCount`
**최적 레이어**: L2 (D-Bus 슬롯 직접 호출로 시뮬레이션)
**테스트 시나리오**:
- `Notify(id=1, appName="Slack", ...)` → `unreadCount("slack")` == 1
- `Notify(id=2, appName="Slack", ...)` → `unreadCount("slack")` == 2
- `CloseNotification(id=1)` → `unreadCount("slack")` == 1
- `NotificationClosed(id=2, reason=...)` → `unreadCount("slack")` == 0
- `revisionChanged` 시그널 매번 발생 검증
- 다른 앱 알림 → 해당 앱 count만 증가
- `clearUnreadNotifications("slack")` → `unreadCount("slack")` == 0
- **엣지**: replacesId != 0 → 기존 알림 교체 (count 불변)

#### F6.2 — SNI NeedsAttention
**설명**: 시스템 트레이 앱의 NeedsAttention 상태 추적.
**구현**: `NotificationTracker::sniNeedsAttention`, `onSniNewStatus`
**최적 레이어**: L2
**테스트 시나리오**:
- SNI 등록 → 앱 ID 매핑 검증
- status="NeedsAttention" → `sniNeedsAttention("app")` == true
- status="Active" → `sniNeedsAttention("app")` == false
- SNI 서비스 해제 → 엔트리 제거
**제약**: D-Bus 통신 필요 — L2에서 직접 슬롯 호출로 우회 가능

#### F6.3 — Attention animation styles (6종)
**설명**: Bounce, Wiggle, Pulse, Glow, Dot color, Blink.
**구현**: QML `DockItem.qml` — `AttentionAnimation` 설정값 기반 조건부 애니메이션
**최적 레이어**: L3
**테스트 시나리오**:
- 각 style (0-6) 설정 + NeedsAttention=true → 해당 애니메이션 활성 (animation.running 검증)
- style=0 (None) → 애니메이션 없음
- NeedsAttention=false → 모든 style에서 애니메이션 비활성

#### F6.4 — Badge display mode
**설명**: 알림 뱃지 표시 방식 — Number, Dot, Off.
**구현**: KConfig `BadgeDisplayMode`, QML 바인딩
**최적 레이어**: L3
**테스트 시나리오**:
- mode=Number + count=3 → 뱃지 텍스트 "3" 표시
- mode=Dot + count>0 → 점 표시
- mode=Off → 뱃지 숨김
- count=0 → 모든 mode에서 뱃지 숨김

#### F6.5 — Attention auto-stop
**설명**: 설정된 시간(초) 후 attention 애니메이션 자동 중단.
**구현**: KConfig `AttentionAnimationDuration`, QML Timer
**최적 레이어**: L3
**테스트 시나리오**:
- duration=5 → 5초 후 애니메이션 중단
- duration=0 → 무한 재생

#### F6.6 — Do Not Disturb
**설명**: 시스템 DND 활성 시 attention 애니메이션 억제.
**구현**: `NotificationTracker::dndActive`, QML 바인딩
**최적 레이어**: L2 (dndActive 상태), L3 (QML 억제 동작)
**테스트 시나리오**:
- DND 활성 → attention 애니메이션 실행 안 함
- DND 비활성 → 정상 실행

#### F6.7 — Clear Notifications menu action
**설명**: 컨텍스트 메뉴에서 "Clear Notifications" 선택 시 알림 초기화.
**구현**: `DockContextMenu` → `NotificationTracker::clearUnreadNotifications`
**최적 레이어**: L2
**테스트 시나리오**:
- 알림 있는 앱에서 clear → unreadCount == 0 + revision 증가

#### F6.8 — Task progress bar
**설명**: Dolphin 파일 복사 등 진행률 표시.
**구현**: QML `DockItem.qml` — `TasksModel::Percentage` role
**최적 레이어**: L3
**테스트 시나리오**:
- Percentage > 0 → 프로그레스 바 visible
- Percentage == 0 또는 -1 → 프로그레스 바 hidden
- Percentage == 100 → 바 꽉 참

---

### Area 7: Keyboard & Accessibility

#### F7.1 — Global shortcut: toggle dock (Meta+`)
**설명**: 글로벌 단축키로 독 표시/숨김.
**구현**: `Application::registerGlobalShortcuts` → `KGlobalAccel`
**최적 레이어**: L4 (KGlobalAccel은 D-Bus 기반)
**테스트 시나리오**: F4.8과 동일 (toggleVisibility 로직은 L2에서 테스트)

#### F7.2 — Meta+Number to activate Nth app
**설명**: Meta+1~9로 독의 N번째 앱 활성화.
**구현**: `Application::registerGlobalShortcuts` → `DockActions::activate(n-1)`
**최적 레이어**: L2 (activate 로직), L4 (단축키 등록/호출)
**테스트 시나리오 (L2)**:
- `activate(0)` ~ `activate(8)` → 각각 올바른 index 활성화
- index >= rowCount → 무시

#### F7.3 — Meta+Shift+Number for new instance
**설명**: Meta+Shift+1~9로 N번째 앱 새 인스턴스 실행.
**구현**: `DockActions::newInstance(n-1)`
**최적 레이어**: L2
**테스트 시나리오**:
- `newInstance(0)` → `TasksModel::requestNewInstance` 호출

#### F7.4 — Arrow navigation
**설명**: 키보드 화살표로 독 아이템 간 이동.
**구현**: QML key handler
**최적 레이어**: L3
**테스트 시나리오**:
- 포커스된 상태에서 Right → 다음 아이템 포커스
- Left → 이전 아이템 포커스
- 첫 아이템에서 Left → 마지막으로 순환 (또는 no-op)
- Up/Down → 프리뷰 열기/닫기

#### F7.5 — Focus ring
**설명**: 키보드 포커스 시 아이콘 주위 포커스 링 표시.
**구현**: QML `FocusIndicator` 또는 `Rectangle` visible 바인딩
**최적 레이어**: L3
**테스트 시나리오**:
- 키보드 포커스 활성 → 포커스 링 visible
- 마우스 인터랙션 → 포커스 링 hidden

#### F7.6 — Screen reader (AT-SPI)
**설명**: 접근성 트리에 앱 이름, 상태 등 노출.
**구현**: QML `Accessible.name`, `Accessible.role`
**최적 레이어**: L4 (실제 AT-SPI 트리 검증 필요)

#### F7.7 — Launch bounce animation
**설명**: 앱 실행 시 아이콘 바운스 애니메이션.
**구현**: `DockActions::taskLaunching` 시그널 → QML 애니메이션
**최적 레이어**: L2 (시그널 발생), L3 (애니메이션 트리거)
**테스트 시나리오 (L2)**:
- `activate(launcher_index)` → `taskLaunching(index)` 시그널 발생

---

### Area 8: Multi-Monitor

#### F8.1 — PrimaryOnly mode
**설명**: 기본 모니터에만 독 표시.
**구현**: `MultiDockManager::setupPrimaryOnly`
**최적 레이어**: L2
**테스트 시나리오**:
- PrimaryOnly + 1 screen → shell 1개 생성
- PrimaryOnly + 2 screens → primary에만 shell
- primary screen 변경 → shell 이동

#### F8.2 — AllScreens mode
**설명**: 모든 모니터에 독 표시.
**구현**: `MultiDockManager::setupAllScreens`
**최적 레이어**: L2
**테스트 시나리오**:
- AllScreens + 2 screens → shell 2개 생성
- screen 추가 → shell 추가
- screen 제거 → shell 제거

#### F8.3 — FollowActive mode
**설명**: 활성 화면을 따라 독이 이동.
**구현**: `MultiDockManager::setupFollowActive`, `setActiveScreen`
**최적 레이어**: L2
**테스트 시나리오**:
- FollowActive + TriggerFocus + 활성 윈도우 변경 → 해당 스크린 shell만 visible
- TriggerMouse + 마우스 스크린 변경 → 해당 스크린 shell만 visible
- Debounce: 빠른 스크린 전환 → debounce 타이머 동작
**제약**: 실제 스크린 이벤트 시뮬레이션 어려움 → mock QScreen 목록 + 수동 시그널

#### F8.4 — Screen hot-plug
**설명**: 모니터 연결/해제 시 자동 대응.
**구현**: `MultiDockManager::onScreenAdded/Removed`, `scheduleTopologyUpdate`
**최적 레이어**: L2
**테스트 시나리오**:
- `QGuiApplication::screenAdded` 시그널 → shell 생성 (AllScreens)
- `QGuiApplication::screenRemoved` 시그널 → shell 파괴
- 빠른 연속 add/remove → debounce 후 1회만 처리

---

### Area 9: Per-Screen Settings

#### F9.1~F9.6 — Per-screen overrides
**설명**: 모니터별 iconSize, edge, visibilityMode, backgroundStyle/Opacity, maxZoomFactor, floating, cornerRadius, pinnedLaunchers override.
**구현**: `ScreenSettings` — KConfig `Screen-{name}` 그룹
**최적 레이어**: L2
**테스트 시나리오**:
- override 없음 → 글로벌 값 반환
- `setIconSize(64)` → `iconSize()` == 64 (글로벌과 무관)
- `clearOverride("IconSize")` → 글로벌 값으로 복귀
- `clearOverrides()` → 모든 override 제거, `hasOverrides()` == false
- `save()` → KConfig 파일에 기록 검증
- 각 속성별 change 시그널 발생 검증
- 글로벌 값 변경 시 override 없는 속성은 따라가고, override 있는 속성은 유지

#### F9.7 — Clear overrides
**설명**: 모든 모니터별 override 초기화.
**테스트 시나리오**: F9.1~F9.6에 포함

---

### Area 10: Virtual Desktop

#### F10.1 — ShowAll mode
**설명**: 모든 가상 데스크톱의 앱을 표시.
**구현**: `DockModel::setVirtualDesktopMode(0)`
**최적 레이어**: L2
**테스트 시나리오**:
- mode=ShowAll → `isOnCurrentDesktop(i)` 모든 앱에 true

#### F10.2 — DimOtherDesktops mode
**설명**: 다른 데스크톱 앱 아이콘을 흐리게 표시.
**구현**: `DockModel::isOnCurrentDesktop`, QML opacity 바인딩
**최적 레이어**: L2 (isOnCurrentDesktop 로직), L3 (opacity 바인딩)
**테스트 시나리오 (L2)**:
- 현재 데스크톱 앱 → `isOnCurrentDesktop` == true
- 다른 데스크톱 앱 → `isOnCurrentDesktop` == false
- 모든 데스크톱 앱 (allDesktops 플래그) → true
- 런처 → true (데스크톱 무관)

#### F10.3 — CurrentOnly mode
**설명**: 현재 데스크톱 앱만 표시.
**구현**: `DockModel::setVirtualDesktopMode(2)`, TasksModel 필터링
**최적 레이어**: L2
**테스트 시나리오**:
- mode=CurrentOnly → 다른 데스크톱 앱 목록에서 제외 (TasksModel 필터 설정 검증)

#### F10.4 — Desktop change event
**설명**: 가상 데스크톱 전환 시 앱 목록 갱신.
**구현**: `DockModel::currentDesktopChanged` 시그널
**최적 레이어**: L2
**테스트 시나리오**:
- VirtualDesktopInfo에서 데스크톱 변경 → `currentDesktopChanged` 시그널 전파

---

### Area 11: Settings Persistence

#### F11.1 — KConfig save/load
**설명**: 모든 설정이 `~/.config/kremarc`에 저장/복원.
**구현**: `KremaSettings` (generated from krema.kcfg)
**최적 레이어**: L2
**테스트 시나리오**:
- 각 설정값 write → read → 동일 값 (37개 전체)
- 기본값 검증 (kcfg의 `<default>` 태그와 일치)
- min/max 경계값 검증

#### F11.2 — Settings restore on startup
**설명**: 앱 재시작 시 설정 복원.
**구현**: KConfigXT 자동 로드
**최적 레이어**: L2 (새 KremaSettings 인스턴스 생성 → 기존 값 로드 확인)

#### F11.3 — Settings UI
**설명**: 설정 다이얼로그 — 7개 탭, FormCard 기반.
**구현**: `SettingsWindow`, `qml/settings/*.qml`
**최적 레이어**: L3 (FormCard delegate 바인딩), L4 (실제 설정 변경 반영)

---

### Area 12: Application Lifecycle

#### F12.1 — Single-instance enforcement
**설명**: 두 번째 Krema 실행 시 무시.
**구현**: `KDBusService::Unique`
**최적 레이어**: L4 (D-Bus 기반)

#### F12.2 — Autostart
**설명**: KDE 로그인 시 자동 시작.
**구현**: `.desktop` AutostartCondition
**최적 레이어**: 수동 검증 (파일 존재 확인)

#### F12.3 — Global shortcut registration
**설명**: Meta+`, Meta+1~9, Meta+Shift+1~9 등록.
**구현**: `KGlobalAccel`
**최적 레이어**: L4

---

## Test Priority Matrix

### P0 — 핵심 기능 (반드시 테스트)

| Feature | Layer | Mock 필요 | 현재 상태 |
|---------|-------|-----------|-----------|
| F2.1 Zoom calculation | L1 | 없음 | ✅ 완료 |
| F3.1 Surface geometry | L1 | 없음 | ✅ 완료 |
| F3.1 Input region | L1 | 없음 | ✅ 완료 |
| F2.8 Background color | L1 | KColorScheme | 🔲 미구현 |
| F4.1~F4.8 Visibility state machine | L2 | DockPlatform, TasksModel | 🔲 미구현 |
| F1.2 Pinned launchers CRUD | L2 | TasksModel | 🔲 미구현 |
| F6.1 Notification count | L2 | 없음 (직접 슬롯 호출) | 🔲 미구현 |
| F9.1~F9.7 Per-screen settings | L2 | KConfig temp file | 🔲 미구현 |
| F11.1 KConfig save/load | L2 | KConfig temp file | 🔲 미구현 |

### P1 — 중요 기능

| Feature | Layer | Mock 필요 | 현재 상태 |
|---------|-------|-----------|-----------|
| F2.6 Icon normalization | L2 | 테스트 이미지 | 🔲 미구현 |
| F5.6 Preview keyboard nav | L2 | DockModel mock | 🔲 미구현 |
| F1.3 Activate/launch | L2 | TasksModel | 🔲 미구현 |
| F1.5 Cycle windows | L2 | TasksModel | 🔲 미구현 |
| F1.6 Move task | L2 | TasksModel | 🔲 미구현 |
| F6.2 SNI NeedsAttention | L2 | D-Bus mock | 🔲 미구현 |
| F8.1~F8.4 Multi-monitor | L2 | QScreen mock | 🔲 미구현 |
| F10.1~F10.4 Virtual desktop | L2 | VirtualDesktopInfo mock | 🔲 미구현 |

### P2 — QML 바인딩 테스트

| Feature | Layer | 현재 상태 |
|---------|-------|-----------|
| F3.2 Indicator dots | L3 | 🔲 미구현 |
| F3.3 Tooltips | L3 | 🔲 미구현 |
| F6.3 Attention animations | L3 | 🔲 미구현 |
| F6.4 Badge display | L3 | 🔲 미구현 |
| F6.8 Progress bar | L3 | 🔲 미구현 |
| F7.4 Arrow navigation | L3 | 🔲 미구현 |
| F7.5 Focus ring | L3 | 🔲 미구현 |

---

## Implementation Prerequisites — 3 Stages

L2/L3 테스트를 활성화하기 위한 사전 작업 3단계.
각 단계는 순서대로 의존한다 (Stage 1 없이 Stage 2 불가).

### Stage 1: DI 리팩토링 — 공유 자원을 Composition Root로 이동

**문제**: `DockModel`이 `TasksModel`, `VirtualDesktopInfo`, `ActivityInfo`를 내부에서 직접 생성한다.
이 3개 객체는 `DockModel`, `DockVisibilityController`, `PreviewController`가 공유하는 자원인데,
`DockModel`이 소유권을 독점하고 getter로 노출하는 구조다.
테스트에서 `DockModel`을 생성하면 `TasksModel`이 자동으로 KWin에 연결을 시도하고,
headless 환경에서는 빈 껍데기가 된다.

**해결**: `Application`(composition root)에서 생성하고 `DockModel`에 주입.

**변경 대상 파일**:

1. `src/models/dockmodel.h` — 생성자 시그니처 변경
```cpp
// Before
explicit DockModel(QObject *parent = nullptr);

// After
explicit DockModel(TaskManager::TasksModel *tasksModel,
                   TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                   TaskManager::ActivityInfo *activityInfo,
                   QObject *parent = nullptr);
```

2. `src/models/dockmodel.cpp` — 내부 생성 제거, 외부 포인터 저장
   - `std::make_unique<TaskManager::TasksModel>(this)` 제거
   - `std::make_shared<VirtualDesktopInfo>(this)` 제거
   - `std::make_shared<ActivityInfo>(this)` 제거
   - `configureTasksModel()` private 메서드로 설정 로직 분리 (classBegin → properties → componentComplete)
   - 멤버 타입: `unique_ptr` → raw pointer (소유권 없음)

3. `src/app/application.h/.cpp` — TasksModel/VDI/AI 생성 추가
```cpp
// Application::run()
m_tasksModel = new TaskManager::TasksModel(this);
m_virtualDesktopInfo = new TaskManager::VirtualDesktopInfo(this);
m_activityInfo = new TaskManager::ActivityInfo(this);
m_dockModel = std::make_unique<DockModel>(m_tasksModel, m_virtualDesktopInfo, m_activityInfo);
```

**이 단계로 해제되는 테스트 (약 20개 기능)**:
- 테스트에서 `TasksModel`을 직접 생성 → `DockModel`에 주입 → headless에서 동작
- `ScreenSettings`, `DockVisibilityController`(AlwaysVisible/AutoHide), `PreviewController` lifecycle,
  `MultiDockManager` 모드 전환, KConfig 설정 CRUD 등

**예상 변경량**: ~50줄 (파일 3개)

---

### Stage 2: ITasksSource 인터페이스 — TasksModel 추상화

**문제**: `TaskManager::TasksModel`은 KDE libtaskmanager의 구체 클래스다.
headless 환경에서 윈도우(running app) 행을 넣을 public API가 없어서,
윈도우 존재를 전제로 하는 기능을 테스트할 수 없다.

**해결**: `ITasksSource` 인터페이스를 정의하고, `DockModel`/`DockActions`가
`TaskManager::TasksModel*` 대신 이 인터페이스에 의존하도록 변경.
프로덕션에서는 `TasksModelAdapter`(실제 TasksModel 래핑),
테스트에서는 `MockTasksSource`(fake 데이터 + 호출 기록)를 사용.

**Krema가 실제로 사용하는 TasksModel API (전수 조사)**:

읽기 (role data via `QAbstractItemModel::data()`) — 17개:
```
Qt::DecorationRole, AppId, AppName, LauncherUrlWithoutIcon,
IsWindow, IsLauncher, IsActive, IsMinimized, IsMaximized,
IsFullScreen, IsStartup, IsOnAllVirtualDesktops,
VirtualDesktops, WinIdList, ScreenGeometry, SkipTaskbar, Percentage
```

쓰기 (TasksModel 고유 메서드) — 8개:
```
requestActivate, requestNewInstance, requestClose,
requestAddLauncher, requestRemoveLauncher, requestOpenUrls,
move, syncLaunchers
```

조회/설정 — 3개:
```
launcherList, setLauncherList, makeModelIndex
```

**변경 대상 파일**:

1. `src/tasks/itaskssource.h` — 신규 인터페이스 (~30줄)
```cpp
class ITasksSource {
public:
    virtual ~ITasksSource() = default;

    // 모델 접근 (QML 바인딩용)
    virtual QAbstractItemModel *model() = 0;

    // 읽기
    virtual int rowCount(const QModelIndex &parent = {}) const = 0;
    virtual QModelIndex index(int row, int col = 0) const = 0;
    virtual QModelIndex makeModelIndex(int row, int childRow) const = 0;
    virtual QStringList launcherList() const = 0;

    // 쓰기
    virtual void setLauncherList(const QStringList &list) = 0;
    virtual void requestActivate(const QModelIndex &idx) = 0;
    virtual void requestClose(const QModelIndex &idx) = 0;
    virtual void requestNewInstance(const QModelIndex &idx) = 0;
    virtual bool requestAddLauncher(const QUrl &url) = 0;
    virtual bool requestRemoveLauncher(const QUrl &url) = 0;
    virtual void requestOpenUrls(const QModelIndex &idx, const QList<QUrl> &urls) = 0;
    virtual bool move(int from, int to) = 0;
    virtual void syncLaunchers() = 0;

    // 설정 (DockModel 초기화 시)
    virtual void setFilterByVirtualDesktop(bool filter) = 0;
};
```

2. `src/tasks/tasksmodeladadpter.h/.cpp` — 신규 프로덕션 래퍼 (~60줄)
   - `TaskManager::TasksModel*`을 받아서 `ITasksSource` 인터페이스로 노출
   - 모든 메서드는 단순 delegation

3. `src/models/dockmodel.h/.cpp` — `TaskManager::TasksModel*` → `ITasksSource*`
   - `tasksModel()` 반환 타입 변경
   - 내부 `m_tasksModel` 타입 변경

4. `src/models/dockactions.cpp` — `tasksModel()->requestActivate(idx)` 등 호출부 변경 없음
   (ITasksSource에 같은 메서드가 있으므로)

5. `src/app/application.cpp` — `TasksModelAdapter` 생성

6. `tests/mocks/mocktaskssource.h` — 테스트용 mock (~120줄)
```cpp
class MockTasksSource : public QAbstractListModel, public ITasksSource {
public:
    struct TaskEntry {
        QString appId, appName;
        QUrl launcherUrl;
        bool isWindow = false, isLauncher = false, isActive = false;
        bool isMinimized = false, isMaximized = false, isFullScreen = false;
        QVariantList winIds, virtualDesktops;
        QList<TaskEntry> children;
    };

    // 테스트 헬퍼
    void addTask(const TaskEntry &entry);
    void removeTask(int index);
    void setTaskData(int index, int role, const QVariant &value);

    // QAbstractItemModel
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QAbstractItemModel *model() override { return this; }

    // ITasksSource mutations — 호출만 기록
    void requestActivate(const QModelIndex &idx) override { m_calls << Call{"activate", idx.row()}; }
    void requestClose(const QModelIndex &idx) override { m_calls << Call{"close", idx.row()}; }
    // ... 나머지 동일 패턴

    // 테스트 검증
    struct Call { QString method; int row = -1; };
    QList<Call> m_calls;
};
```

**이 단계로 추가 테스트 가능 (약 12개 — 이전에 L4 only였던 윈도우 기능)**:
- F1.1 실행 중인 앱 표시 (mock에 윈도우 행 추가)
- F1.3 실행 중인 앱 activate (requestActivate 호출 검증)
- F1.5 윈도우 순환 — cycleWindows 로직 (children 설정)
- F1.7 openUrlsWithTask (requestOpenUrls 호출 검증)
- F5.2 다중 윈도우 프리뷰 — childCount, windowIds
- F5.3/F5.4 프리뷰 윈도우 활성화/닫기
- F10.2/F10.3 isOnCurrentDesktop (virtualDesktops role 설정)
- F10.4 데스크톱 전환 시 목록 갱신

**예상 변경량**: ~210줄 신규 + ~30줄 기존 수정

---

### Stage 3: DockVisibilityController의 DodgeWindows 테스트 활성화

**문제**: `DockVisibilityController::hasOverlappingWindow()`가 `private` 비가상 메서드다.
내부 `m_overlapModel`(별도 `TasksModel` 인스턴스)의 rowCount로 겹침을 판단하는데,
이 모델도 headless에서 윈도우 행이 없다.

DodgeWindows 상태 머신을 테스트하려면 "겹침 있음/없음"을 외부에서 제어해야 하는데,
현재 `hasOverlappingWindow()`에 접근할 방법이 없다.

**해결**: `hasOverlappingWindow()`를 `protected virtual`로 변경.

```cpp
// src/shell/dockvisibilitycontroller.h

// Before (private, non-virtual)
private:
    [[nodiscard]] bool hasOverlappingWindow(bool activeOnly = false) const;

// After (protected, virtual — test subclass에서 override 가능)
protected:
    [[nodiscard]] virtual bool hasOverlappingWindow(bool activeOnly = false) const;
```

테스트에서:
```cpp
class TestableDockVisibilityController : public DockVisibilityController {
public:
    using DockVisibilityController::DockVisibilityController; // 생성자 상속
    bool m_fakeOverlap = false;
protected:
    bool hasOverlappingWindow(bool /*activeOnly*/) const override { return m_fakeOverlap; }
};

// 사용
controller.m_fakeOverlap = true;
// → evaluateVisibility() 호출 시 DodgeWindows 모드에서 hidden
controller.m_fakeOverlap = false;
// → evaluateVisibility() 호출 시 visible
```

**변경량**: 헤더 1줄 수정 (`private:` → `protected:`, `virtual` 추가)
**프로덕션 영향**: 없음 (기존 호출 경로 동일, vtable 오버헤드 무시 가능)

**이 단계로 추가 테스트 가능 (약 5개)**:
- F4.3 DodgeWindows: 겹침 → 숨김 → 겹침 해제 → 표시
- F4.4 DodgeActiveOnly: activeOnly=true vs false 분기
- F4.3 + F4.6: 겹침 + interacting lock 조합
- F4.3 + F4.7: 겹침 + keyboard active 조합
- DodgeWindows 모드 전환 시 타이머 정리

---

### 추가 인프라 (3 Stage와 별개, 병렬 진행 가능)

#### MockDockPlatform

`DockPlatform` 순수 인터페이스 구현. Stage 1의 DockVisibilityController 테스트에 필요.

```cpp
// tests/mocks/mockdockplatform.h
class MockDockPlatform : public krema::DockPlatform {
public:
    // 모든 set*() 호출을 기록하고 현재 상태를 getter로 노출
    void setEdge(Edge edge) override { m_edge = edge; m_calls.push_back("setEdge"); }
    void setExclusiveZone(int zone) override { m_exclusiveZone = zone; }
    void setMargin(int margin) override { m_margin = margin; }
    void setVisibilityMode(VisibilityMode mode) override { m_visibilityMode = mode; }
    void setSize(const QSize &size) override { m_size = size; }
    void setInputRegion(const QRegion &region) override { m_inputRegion = region; }
    void setKeyboardInteractivity(bool enabled) override { m_keyboardInteractive = enabled; }
    void setupWindow(QWindow *) override {}
    Edge edge() const override { return m_edge; }

    // Test inspection
    Edge m_edge = Edge::Bottom;
    int m_exclusiveZone = 0;
    int m_margin = 0;
    VisibilityMode m_visibilityMode = VisibilityMode::AlwaysVisible;
    QSize m_size;
    QRegion m_inputRegion;
    bool m_keyboardInteractive = false;
    QStringList m_calls;
};
```

**난이도**: 낮음 (~40줄). `DockPlatform`이 이미 순수 가상 인터페이스.

#### KConfig 테스트 환경

```cpp
// 각 테스트 setUp에서
QStandardPaths::setTestModeEnabled(true);
// → ~/.qttest/config/kremarc 에 설정 파일 격리
// 각 테스트 tearDown에서 임시 파일 삭제
```

#### QML 컴포넌트 테스트 harness (L3용)

```cmake
# tests/qml/CMakeLists.txt
find_package(Qt6 REQUIRED COMPONENTS QuickTest)

add_executable(krema_qml_tests main.cpp)
target_link_libraries(krema_qml_tests PRIVATE Qt6::QuickTest Qt6::Quick krema_lib)
target_compile_definitions(krema_qml_tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}/tests/qml"
)
add_test(NAME KremaQmlTests COMMAND krema_qml_tests)
set_tests_properties(KremaQmlTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=null"
)
```

### CMake 변경

```cmake
# tests/CMakeLists.txt에 추가
find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)

add_subdirectory(unit)      # 기존 Catch2
add_subdirectory(qtest)     # 신규 QTest (Stage 1~3 후)
add_subdirectory(qml)       # 신규 QtQuickTest (L3)
```

---

## Testability Roadmap

```
현재:              5/71  (Catch2 순수 로직만)

Stage 1 (DI):    +20   = 25/71
  ├── 설정 CRUD, ScreenSettings, KConfig
  ├── DockVisibilityController (AlwaysVisible, AutoHide)
  ├── PreviewController lifecycle
  └── MultiDockManager 모드 전환

Stage 2 (mock):  +22   = 47/71
  ├── ITasksSource 인터페이스 + MockTasksSource
  ├── 런처 CRUD: isPinned, togglePinned, moveTask, addLauncher
  └── 윈도우 기능: activate, cycleWindows, isOnCurrentDesktop, childCount

Stage 3 (seam):    +5   = 52/71
  └── DodgeWindows 상태 머신 전체

L3 (QML):         +14   = 66/71
  ├── 인디케이터 dots, 툴팁, 뱃지
  ├── Attention 애니메이션 트리거
  └── 키보드 네비게이션, 포커스 링

L4 only:           5/71
  └── 글로벌 단축키, 싱글 인스턴스, autostart, AT-SPI
```

---

## Feature Coverage Summary

| Area | 총 기능 | L1 | L2 | L3 | L4 only |
|------|---------|----|----|----|---------| 
| App/Task Management | 8 | 0 | 7 | 1 | 0 |
| Visual/Appearance | 10 | 4 | 5 | 1 | 0 |
| Position & Layout | 3 | 1 | 0 | 2 | 0 |
| Visibility Control | 8 | 0 | 8 | 0 | 0 |
| Window Previews | 6 | 0 | 4 | 2 | 0 |
| Notifications | 8 | 0 | 4 | 4 | 0 |
| Keyboard/A11y | 7 | 0 | 2 | 3 | 2 |
| Multi-Monitor | 4 | 0 | 4 | 0 | 0 |
| Per-Screen Settings | 7 | 0 | 7 | 0 | 0 |
| Virtual Desktop | 4 | 0 | 4 | 0 | 0 |
| Settings Persistence | 3 | 0 | 2 | 1 | 0 |
| App Lifecycle | 3 | 0 | 0 | 0 | 3 |
| **합계** | **71** | **5** | **47** | **14** | **5** |

**Headless (L1+L2+L3): 66/71 (93%)**
L4 only (글로벌 단축키, D-Bus 서비스 등): 5/71 (7%)
