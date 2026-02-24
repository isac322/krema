# 교훈 (Lessons Learned)

## 1. QML 애니메이션 좌표가 C++ 로직에 영향을 주는 문제 (2026-02)

**증상:** 데스크탑 전환 시 독이 번갈아가며 보이고 숨겨짐 (D1: 숨김 → D2: 보임 → D3: 숨김 → ...)

**근본 원인:**
- QML에서 독의 `y` 좌표는 가시성에 따라 애니메이션됨 (보임: 화면 안, 숨김: 화면 밖)
- `onYChanged` → `setPanelRect()` → C++ `m_panelY` 업데이트
- `dockScreenRect()`가 `m_panelY`를 사용해 겹침 판정
- 독이 숨겨진 상태에서 `m_panelY`가 화면 밖 → 겹침 없음 → 독 보임 → 무한 루프

**해결책:**
- `m_panelRefY` 멤버 추가: 독이 **보이는 상태**일 때만 갱신
- `dockScreenRect()`에서 `m_panelY` 대신 `m_panelRefY` 사용
- 숨김 애니메이션 중에도 "독이 보인다면 어디에 있을까"를 정확히 판단

**한계:**
- 이 수정은 Y 좌표에 한정된 우회책 (`m_panelRefY`)
- 더 근본적인 방법: `dockScreenRect()`를 QML 좌표에 의존하지 않고 C++ 측에서 독의 논리적 위치를 독립적으로 계산

**핵심 교훈:**
- QML 애니메이션 값이 C++ 비즈니스 로직에 사용될 때, 애니메이션 중간 상태가 로직을 오염시킬 수 있음
- 번갈아가며 토글되는 패턴은 **상태 피드백 루프**의 전형적 증상
- 근본 원인 파악 시 "어떤 값이 어디서 갱신되고 어디서 읽히는지" 데이터 흐름을 추적해야 함

## 2. PipeWire ScreencastingRequest + KWin 프로토콜 권한 (2026-02)

**증상:** ScreencastingRequest.nodeId가 항상 0, PipeWire 스트림이 Paused에서 멈춤

**근본 원인 (2개):**
1. KWin `fetchProcessServiceField()`는 desktop file의 Exec 경로와
   실행 중인 바이너리 경로를 **정확히** 비교 (canonicalFilePath).
   개발 빌드 경로 불일치 → 프로토콜 권한 거부 → nodeId=0
2. PipeWireSourceItem의 `visible` 속성에 `ready`를 바인딩하면 순환 의존:
   ready=false → visible=false → setActive(false) → Paused → ready 영원히 false

**해결책:**
1. justfile에 dev-desktop 레시피: 빌드 경로 + X-KDE-Wayland-Interfaces 포함
2. visible 바인딩에서 ready 제거, 부모 컨테이너가 visibility 관리

**핵심 교훈:**
- KWin의 Wayland 프로토콜 권한 메커니즘은 헤더를 읽어야만 알 수 있음
- KPipeWire의 itemChange(ItemVisibleHasChanged) 동작은 소스 확인 필수
- Phase 1 전문가 자문을 스킵하면 디버깅 비용이 구현 비용의 3-5배로 증가

## 3. QML Repeater + JS Array Model = delegate 전체 파괴/재생성 (2026-02)

**증상:**
1. 프리뷰 열린 상태에서 새 인스턴스 띄우면 프리뷰에 반영 안 됨
2. 마우스 휠로 앱 간 전환 시 모든 썸네일이 아이콘으로 fallback 후 복구 (깜빡임)

**근본 원인:**
- `Repeater.model`에 JS 배열을 할당하면, 배열 교체 시 Repeater가 기존 delegate를 **전부 파괴**하고 새로 생성
- PipeWire `ScreencastingRequest`가 파괴 → 새로 생성 → nodeId 재요청 → ~100-200ms 대기
- 이 사이 `pipeWireItem.ready=false` → fallback 아이콘 표시 → 깜빡임
- 추가로 `WinIdList`와 `childCount()`의 비동기 불일치: `childCount=3`이지만 `WinIdList`가 아직 2개인 타이밍 → `allWinIds[2] = undefined` → 프리뷰 누락

**해결책:**
- `childWindowModel`을 `ListModel`로 전환
- `rebuildChildModel()`을 `set()`/`append()`/`remove()` 기반 incremental update로 변경
- `count = Math.min(childCount, allWinIds.length)`로 WinIdList 길이를 상한으로 사용
- `childCount > winIdCount`일 때 50ms 후 retry하는 Timer 추가
- `onParentIndexChanged`에서만 `clear()` (다른 앱 = 다른 PipeWire 스트림)

**핵심 교훈:**
- Repeater + JS 배열 교체 = delegate 전량 파괴/재생성 → GPU 리소스 손실
- GPU 리소스(PipeWire 스트림)를 가진 delegate에는 반드시 ListModel + incremental update 사용
- TasksModel의 WinIdList와 childCount()는 비동기적으로 갱신될 수 있음 → 불일치 대응 필요
- "에러 로그 없음 = 문제 없음"은 잘못된 가정 → 기능적 시나리오 재현 검증 필수

## 4. Qt 6 Required Properties와 Repeater model 컨텍스트 (2026-02)

**증상:** Repeater delegate 내에서 `model.title`, `model.isMinimized` 등이 `ReferenceError: model is not defined`

**근본 원인:**
- Qt 6에서 Repeater delegate에 `required property` 하나라도 선언하면 "required properties mode" 활성화
- 이 모드에서는 `model`, `index`, `modelData` 등 기존 context property가 비활성화됨
- Repeater가 required property만 설정하고 나머지는 사용 불가

**해결책:**
- `required property int index` 제거
- 암시적 `index`와 `model` context property 사용

**핵심 교훈:**
- Qt 6에서 required property는 "all or nothing" — 하나라도 쓰면 context property 전체가 비활성화
- Repeater delegate에서 `model.*`에 접근해야 하면 required property 사용 금지

## 5. HoverHandler vs MouseArea.containsMouse in z-ordered layouts (2026-02)

**증상:** 썸네일 위에 마우스를 올려도 닫기 버튼이 안정적으로 표시되지 않음

**근본 원인:**
- 닫기 ToolButton(z:10)이 MouseArea 위에 보이면, MouseArea.containsMouse가 false
- Qt Quick의 마우스 이벤트 전달: 최상위 visible 아이템이 hover를 가로챔
- `visible: mouseArea.containsMouse` → ToolButton 보임 → containsMouse=false → ToolButton 사라짐 → 깜빡임

**해결책:**
- `HoverHandler`를 thumbnailContainer에 추가
- HoverHandler는 passive grab 사용 → ToolButton의 z-order에 영향받지 않음
- 닫기 버튼 / hover highlight: `mouseArea.containsMouse` → `hoverHandler.hovered`

**핵심 교훈:**
- z-order가 있는 레이아웃에서 hover 감지에는 HoverHandler 사용 (MouseArea.containsMouse 아님)
- HoverHandler는 passive → 자식 아이템의 이벤트 처리에 간섭하지 않음
- 같은 패턴: surface-level hover도 MouseArea 대신 HoverHandler로 해결됨

## 5. Layer-shell 서피스 간 키보드 포커스 전환 불가 (2026-02)

**증상:** 독에서 Down 키로 프리뷰 팝업을 열고 `requestKeyboardFocus()` 호출 → 프리뷰 QML의 `forceActiveFocus()`가 실패 (`activeFocus: false`), 키보드 이벤트가 프리뷰에 전달되지 않음.

**근본 원인:**
- Layer-shell `setKeyboardInteractivity()` + `requestActivate()`는 Wayland 비동기 프로토콜
- `invokeMethod("startKeyboardNavigation")`가 동기적으로 실행되어 `forceActiveFocus()` 시점에 윈도우가 아직 active가 아님
- `KeyboardInteractivityExclusive`를 사용해도 즉시 반영되지 않음 (compositor round-trip 필요)
- 두 서피스가 동시에 `Exclusive`를 설정하면 충돌

**해결:**
- 프리뷰 키보드 네비게이션을 독 서피스에서 처리 (단일 서피스가 항상 키보드 보유)
- PreviewController에 C++ 프로퍼티 (`previewKeyboardActive`, `focusedThumbnailIndex`) 추가
- 독의 `Keys.onPressed`에서 `PreviewController.previewKeyboardActive` 여부로 분기
- 프리뷰 QML은 C++ 프로퍼티 바인딩만 (포커스 링, 접근성)

**핵심 교훈:**
- Layer-shell 서피스 간 키보드 포커스 전환은 신뢰할 수 없음 — 항상 단일 서피스에서 키보드 처리
- `QWindow::activeChanged` 시그널 + 재시도 패턴은 안전장치로만 사용 (주 로직에 의존 금지)
- 멀티 서피스 앱에서는 키보드 이벤트를 하나의 "컨트롤러" 서피스에 집중시킬 것
