# krema 사용자 QA 체크리스트

사용자가 krema를 쓸 때 각 기능이 의도대로 동작하는지 확인하는 목록이다. 구현 관점의
검사(인터페이스 등록 여부, 모델이 채워지는지)는 넣지 않았다. 모든 항목은 사용자가
직접 하는 조작과 눈으로 보는 결과로만 쓰여 있다.

`tests/ci/`의 프레임 테스트 하네스로 자동화할 수 있는지도 항목마다 표시했다. 승인 후
구현 순서를 정하는 근거로 쓴다.

## 자동화 태그

| 태그 | 뜻 |
|---|---|
| `AUTO` | 지금 하네스로 그대로 실행 가능 |
| `AUTO-REQ` | 클라이언트가 요청·계산한 값까지만 검증. 컴포지터가 실제로 화면 그 위치에 그렸는지는 확인 못 함 |
| `FIXTURE` | 하네스는 쓰되 준비물이 더 필요. 무엇이 필요한지 항목마다 적었다 |
| `MANUAL` | 컨테이너에서 불가. 실제 Plasma 데스크톱에서 사람이 확인해야 한다 |

`AUTO-REQ`가 따로 있는 이유: 검증된 캡처 경로는 클라이언트 `grabWindow()`이고 이건 창
내부 픽셀만 보여준다. 컴포지터 스크린샷(`org.kde.KWin.ScreenShot2`)은 `/dev/dri` 없는
컨테이너에서 전부 실패한다. 그래서 "독이 화면 왼쪽 가장자리에 8px 띄워 붙었는가"는
요청한 anchor·margin 값까지만 확인할 수 있다.

## 커버리지

| 영역 | 이름 | 항목 | P0 | P1 | P2 | AUTO | AUTO-REQ | FIXTURE | MANUAL |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| VIS | 독 표시·배치 | 24 | 10 | 14 | 0 | 7 | 8 | 8 | 1 |
| ITEM | 독 항목·작업 관리 | 25 | 5 | 15 | 5 | 10 | 0 | 11 | 4 |
| PRE | 창 미리보기 | 20 | 6 | 12 | 2 | 1 | 0 | 18 | 1 |
| CMD | 컨텍스트 메뉴·전역 단축키 | 16 | 8 | 6 | 2 | 7 | 0 | 4 | 5 |
| SET | 설정 | 22 | 6 | 13 | 3 | 12 | 0 | 8 | 2 |
| NOTI | 알림·주의 표시 | 17 | 5 | 8 | 4 | 9 | 0 | 8 | 0 |
| KEY | 키보드·접근성 | 16 | 8 | 5 | 3 | 9 | 0 | 7 | 0 |
| **합계** | | **140** | 48 | 73 | 19 | 55 | 8 | 64 | 13 |

우선순위는 P0(깨지면 제품이 안 됨), P1(자주 쓰는 기능), P2(드문 경로·엣지 케이스)다.

## 필요한 준비물

`FIXTURE` 64개가 요구하는 것을 종류별로 묶으면 다섯 가지다.

| 준비물 | 항목 수 | 비고 |
|---|---:|---|
| 토플레벨 창 1~3개 (그룹화 포함) | 약 40 | `fixture_windows` 로 이미 지원. 앱별 app_id 지정만 추가하면 된다 |
| 다중 virtual 출력 | 4 | `kwin_wayland --virtual` 출력 개수 옵션 확인 필요 |
| 알림 D-Bus 스텁 (`org.freedesktop.Notifications`, StatusNotifierWatcher) | 5 | 배지·attention 트리거용 |
| 자기 geometry·입력 이벤트를 보고하는 fixture 창 | 3 | exclusive zone 효과와 클릭 통과 판정용 |
| 가상 데스크톱 2개 이상 | 2 | KWin D-Bus로 구성 |

`MANUAL` 13개는 실제 데스크톱에서 확인한다. 대부분 전역 단축키(KGlobalAccel), 실제 앱
실행, PipeWire 썸네일, 컴포지터 블러다.


## VIS — 독 표시·배치

### 시각 및 표시제어 (Visibility & Display)

#### QA-VIS-001: 하단 가장자리(Bottom Edge) 기본 배치 및 수평 레이아웃
- **사용자 동작**: 설정 > 일반 > 독 위치(`Edge`)를 `1` (Bottom, 기본값)로 지정하고 독을 확인한다.
- **기대 결과**: 독이 화면 하단 중앙에 배치되며, 고정 런처 및 실행 중인 앱 아이콘이 좌에서 우 방향(`Flow.LeftToRight`)으로 수평 정렬된다. 패널 높이는 `IconSize`(기본값 48px) + 패널 패딩(`16px`) + Floating 여백(`8px`)으로 산출된다.
- **실패 징후**: 독이 화면 하단이 아닌 다른 가장자리에 위치하거나, 세로 정렬되거나, 패널이 화면 중앙이 아닌 다른 곳으로 쏠린다.
- **근거**: `src/config/krema.kcfg:47-50`, `src/platform/waylanddockplatform.cpp:168`, `src/qml/main.qml:627-633,703`
- **우선순위**: P0
- **자동화**: `AUTO-REQ` — 클라이언트가 요청·계산한 값(LayerShellQt anchors/margins, QQuickWindow geometry, Flow 방향, 패널 width/height 축)까지 검증. 컴포지터가 실제로 화면 그 위치에 배치했는지는 현재 하네스로 미검증

#### QA-VIS-002: 상단 가장자리(Top Edge) 배치 및 수평 레이아웃 전환
- **사용자 동작**: 설정에서 독 위치(`Edge`)를 `0` (Top)으로 변경한다.
- **기대 결과**: 독이 즉시 화면 상단 중앙으로 이동하며, 아이콘들은 계속 좌에서 우 방향(`Flow.LeftToRight`)으로 수평 정렬된다.
- **실패 징후**: 설정 변경 후에도 독이 하단에 남아있거나, 상단 패널 위치의 Y 좌표 계산 오류로 화면 밖으로 벗어난다.
- **근거**: `src/config/krema.kcfg:47-50`, `src/platform/waylanddockplatform.cpp:163`, `src/qml/main.qml:643,703`
- **우선순위**: P1
- **자동화**: `AUTO-REQ` — 클라이언트가 요청·계산한 값(LayerShellQt anchors/margins, QQuickWindow geometry, Flow 방향, 패널 width/height 축)까지 검증. 컴포지터가 실제로 화면 그 위치에 배치했는지는 현재 하네스로 미검증

#### QA-VIS-003: 좌측 가장자리(Left Edge) 세로 배치 및 Flow 레이아웃 전환
- **사용자 동작**: 설정에서 독 위치(`Edge`)를 `2` (Left)로 변경한다.
- **기대 결과**: 독이 화면 좌측 중앙에 세로 패널로 배치되며, 아이콘 배치 흐름이 위에서 아래 방향(`Flow.TopToBottom`)으로 변경된다. 패널 너비와 높이 축이 상호 교환되어 높이는 전체 화면 높이를 채우고 너비는 아이콘 크기에 맞춰진다.
- **실패 징후**: 아이콘이 가로로 누워서 표시되거나, 패널이 가로 모드의 너비/높이 크기를 유지하여 화면 좌측에서 잘린다.
- **근거**: `src/config/krema.kcfg:47-50`, `src/shell/dockview.cpp:141,215-220`, `src/platform/waylanddockplatform.cpp:173`, `src/qml/main.qml:627-633,703`
- **우선순위**: P1
- **자동화**: `AUTO-REQ` — 클라이언트가 요청·계산한 값(LayerShellQt anchors/margins, QQuickWindow geometry, Flow 방향, 패널 width/height 축)까지 검증. 컴포지터가 실제로 화면 그 위치에 배치했는지는 현재 하네스로 미검증

#### QA-VIS-004: 우측 가장자리(Right Edge) 세로 배치 및 위치 변경
- **사용자 동작**: 설정에서 독 위치(`Edge`)를 `3` (Right)으로 변경한다.
- **기대 결과**: 독이 화면 우측 중앙에 세로 패널로 배치되며, 위에서 아래 방향(`Flow.TopToBottom`)으로 아이콘이 세로 정렬된다.
- **실패 징후**: 화면 우측 패널의 앵커(AnchorRight) 설정 실패로 패널이 화면 중앙이나 우측 경계 바깥으로 사라진다.
- **근거**: `src/config/krema.kcfg:47-50`, `src/platform/waylanddockplatform.cpp:178`, `src/qml/main.qml:649`
- **우선순위**: P1
- **자동화**: `AUTO-REQ` — 클라이언트가 요청·계산한 값(LayerShellQt anchors/margins, QQuickWindow geometry, Flow 방향, 패널 width/height 축)까지 검증. 컴포지터가 실제로 화면 그 위치에 배치했는지는 현재 하네스로 미검증

#### QA-VIS-005: Floating(띄우기) 활성화 시 8px 간격 및 패널 모서리 여백
- **사용자 동작**: 설정에서 `Floating` 옵션을 `true` (기본값)로 설정한다.
- **기대 결과**: 독 패널과 화면 가장자리 사이에 정확히 `8px` (`s_floatingMargin`)의 여백(Gap)이 생기며, 둥근 모서리(`CornerRadius`, 기본값 12px) 형태의 공중 플로팅 패널로 표시된다.
- **실패 징후**: 화면 가장자리에 독이 바짝 붙어 패널 뒤쪽 배경이 보이지 않거나, 여백 크기가 이상하게 크게 작용한다.
- **근거**: `src/config/krema.kcfg:37-40`, `src/shell/dockview.cpp:131`, `src/qml/main.qml:640`
- **우선순위**: P1
- **자동화**: `AUTO-REQ` — 클라이언트가 요청·계산한 값(LayerShellQt anchors/margins, QQuickWindow geometry, Flow 방향, 패널 width/height 축)까지 검증. 컴포지터가 실제로 화면 그 위치에 배치했는지는 현재 하네스로 미검증

#### QA-VIS-006: Floating(띄우기) 비활성화 시 화면 가장자리 완전 밀착
- **사용자 동작**: 설정에서 `Floating` 옵션을 `false`로 변경한다.
- **기대 결과**: 독 패널 하단(하단 독 기준)이 화면 가장자리에 오차 없이 완전 밀착된다 (`floatingPadding` = 0px).
- **실패 징후**: Floating을 껐음에도 불구하고 독과 화면 가장자리 사이에 8px 여백이 그대로 남아있는다.
- **근거**: `src/config/krema.kcfg:37-40`, `src/shell/dockview.cpp:131`, `src/qml/main.qml:640`
- **우선순위**: P1
- **자동화**: `AUTO-REQ` — 클라이언트가 요청·계산한 값(LayerShellQt anchors/margins, QQuickWindow geometry, Flow 방향, 패널 width/height 축)까지 검증. 컴포지터가 실제로 화면 그 위치에 배치했는지는 현재 하네스로 미검증

#### QA-VIS-007: AlwaysVisible(항상 보임) 모드에서 독 지속 표시 및 토글 차단
- **사용자 동작**: `VisibilityMode`를 `0` (AlwaysVisible, 기본값)으로 설정하고 마우스를 독 밖으로 이동하거나 전역 단축키 토글을 시도한다.
- **기대 결과**: 독이 화면에서 항상 표시되며, 마우스 이탈 시에도 사라지지 않는다. `toggleVisibility()` 호출 시에도 모드가 AlwaysVisible이면 아무 동작도 하지 않고 표시 상태를 유지한다.
- **실패 징후**: 마우스가 독을 벗어나거나 창이 접근할 때 독이 예기치 않게 숨겨진다.
- **근거**: `src/config/krema.kcfg:42-45`, `src/shell/dockvisibilitycontroller.cpp:129-131,218-220`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-VIS-008: AutoHide(자동 숨김) 모드에서 마우스 진입 시 ShowDelay 지연 후 슬라이드 표시
- **사용자 동작**: `VisibilityMode`를 `1` (AutoHide)로 설정하고, 독이 숨겨진 상태에서 화면 가장자리 4px 마우스 트리거 영역에 마우스 포인터를 올려놓는다.
- **기대 결과**: 마우스 포인터 진입 후 설정된 `ShowDelay`(기본값 200ms) 동안 대기한 후 독이 슬라이드 애니메이션으로 부드럽게 나타난다.
- **실패 징후**: 트리거 영역 진입 즉시 지연 없이 바로 나타나거나, ShowDelay 시간이 지나도 독이 나타나지 않는다.
- **근거**: `src/config/krema.kcfg:42-45,52-55`, `src/shell/dockvisibilitycontroller.cpp:48-54,142-154`, `src/utils/inputregion.cpp:16-30`
- **우선순위**: P0
- **자동화**: `AUTO` — 등장·퇴장 자체와 슬라이드·페이드 곡선은 검증한다. 지연 길이는 QTimer라 검증 대상이 아니다

#### QA-VIS-009: AutoHide(자동 숨김) 모드에서 마우스 이탈 시 HideDelay 지연 후 슬라이드 숨김
- **사용자 동작**: AutoHide 모드에서 독 패널 위에서 마우스 포인터를 독 바깥 화면 영역으로 이동시킨다.
- **기대 결과**: 마우스 이탈 즉시 숨겨지지 않고, 설정된 `HideDelay`(기본값 400ms) 동안 기다린 후 독이 가장자리 바깥 방향으로 슬라이드되어 숨겨진다.
- **실패 징후**: 마우스가 이탈하자마자 딜레이 없이 숨겨지거나, HideDelay가 지나도 독이 영구히 화면에 남는다.
- **근거**: `src/config/krema.kcfg:42-45,57-60`, `src/shell/dockvisibilitycontroller.cpp:56-62,156-173`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-VIS-010: DodgeWindows(스마트 숨김 - 전체) 모드에서 독 영역 교차 창 존재 시 자동 숨김
- **사용자 동작**: `VisibilityMode`를 `2` (DodgeWindows) 및 `DodgeActiveOnly`를 `false`(기본값)로 설정한 상태에서 일반 앱 창을 이동시켜 독 영역과 겹치게(Overlap) 놓는다.
- **기대 결과**: 독 패널과 위치가 겹치는 창이 1개라도 존재하는 순간 300ms 평가 디바운스 후 독이 바깥으로 슬라이드하여 숨겨진다. 창을 겹치지 않는 곳으로 이동시키거나 최소화하면 독이 다시 나타난다.
- **실패 징후**: 창이 독 패널을 가리는데도 독이 숨겨지지 않거나, 창을 치워도 독이 다시 돌아오지 않는다.
- **근거**: `src/config/krema.kcfg:42-45,47-50`, `src/shell/dockvisibilitycontroller.cpp:32-46,226,380-394`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개 (독 영역 연동 위치)`

#### QA-VIS-011: DodgeWindows(스마트 숨김 - 활성 창 전용) 비활성/활성 전환 동작
- **사용자 동작**: `VisibilityMode`를 `2` (DodgeWindows) 및 `DodgeActiveOnly`를 `true`로 설정한 후, 독 영역과 겹치는 비활성 창 A와 겹치지 않는 활성 창 B를 둔다. 그 후 창 A를 클릭하여 활성화한다.
- **기대 결과**: 창 A가 독과 겹쳐있더라도 비활성 상태일 때는 독이 숨겨지지 않고 계속 보이다가, 창 A가 활성화(IsActive=true)되는 순간 독이 숨겨진다.
- **실패 징후**: 비활성 창만 겹쳐도 독이 숨겨지거나, 활성화되었음에도 숨겨지지 않는다.
- **근거**: `src/config/krema.kcfg:47-50`, `src/shell/dockvisibilitycontroller.cpp:226,380-394`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 2개 (활성/비활성 상태 전환)`

#### QA-VIS-012: 상호작용 잠금(Interacting Lock - 우클릭 메뉴/설정창) 유지 중 자동 숨김 방지
- **사용자 동작**: AutoHide 또는 DodgeWindows 모드에서 독 아이콘을 우클릭하여 컨텍스트 메뉴를 띄우거나 설정 창을 연 상태에서 마우스를 독 밖으로 치운다.
- **기대 결과**: 상호작용 카운트(`m_interactingCount > 0`)가 유지되는 동안은 HideDelay 타이머가 작동하지 않으며 독이 강제로 보임 상태(`setVisible(true)`)를 유지한다. 메뉴나 설정 창을 닫으면 잠금이 해제되고 지연 후 정상 숨김 처리된다.
- **실패 징후**: 우클릭 메뉴나 설정 창이 떠 있는 상태에서 마우스가 나가면 독이 슬라이드하여 숨겨져 메뉴만 공중에 떠 있게 된다.
- **근거**: `src/shell/dockvisibilitycontroller.cpp:164-167,177,308-330`, `src/shell/dockshell.cpp:168,178`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-VIS-013: 키보드 탐색(Keyboard Navigation) 활성화 시 자동 숨김 방지 및 포커스 유지
- **사용자 동작**: AutoHide 모드에서 키보드 탐색 단축키를 입력하거나 `focusDock()`을 실행하여 키보드 탐색 모드(`m_keyboardActive = true`)를 시작한다.
- **기대 결과**: 마우스가 독 패널 영역 밖에 있더라도 독이 보임 상태를 유지하며, Wayland LayerShell의 키보드 상호작용성(`KeyboardInteractivityExclusive`)이 활성화되어 화살표 키로 독 아이콘을 탐색할 수 있다. Escape를 눌러 탐색 종료 시 정상 숨김 모드로 복귀한다.
- **실패 징후**: 키보드로 아이콘을 옮기는 도중 HideDelay 타이머가 작동하여 독이 숨겨지거나, 키 입력이 독으로 전달되지 않는다.
- **근거**: `src/shell/dockvisibilitycontroller.cpp:180,280-306`, `src/platform/waylanddockplatform.cpp:125-131`, `src/shell/dockshell.cpp:89-102`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-VIS-014: AlwaysVisible 모드에서 창 최대화 시 Exclusive Zone 영역 확보 및 창 피함
- **사용자 동작**: `VisibilityMode`를 `0` (AlwaysVisible)으로 설정한 상태에서 임의의 애플리케이션 창을 최대화(Maximize)한다.
- **기대 결과**: Wayland 컴포지터가 독 패널의 높이만큼 exclusive zone 공간을 확보하여, 최대화된 창이 독 패널을 가리지 않고 독의 상단(하단 독 기준) 경계까지만 확충된다.
- **실패 징후**: 최대화된 애플리케이션 창이 독 패널 아래로 들어가 독을 가리거나, 반대로 독 패널이 화면 공간을 비워두지 못한다.
- **근거**: `src/config/krema.kcfg:42-45`, `src/platform/waylanddockplatform.cpp:88-90`, `src/shell/dockview.cpp:146-149`
- **우선순위**: P0
- **자동화**: `FIXTURE: 최대화 가능한 fixture 창 + 그 창이 자기 geometry를 보고하도록 확장` (exclusive zone 효과는 다른 창의 크기 변화로만 관찰 가능)

#### QA-VIS-015: AutoHide/DodgeWindows 모드에서 창 최대화 시 Exclusive Zone 미해제(-1) 및 화면 전체 점유
- **사용자 동작**: `VisibilityMode`를 `1` (AutoHide) 또는 `2` (DodgeWindows)로 변경한 상태에서 애플리케이션 창을 최대화한다.
- **기대 결과**: 독의 exclusive zone 값이 `-1`로 설정되어 컴포지터가 화면 전체를 창에게 배정한다. 최대화된 창이 전체 화면을 채우며, 독이 숨겨진 상태에서 트리거 진입 시 창 위에 레이어로 독이 나타난다.
- **실패 징후**: AutoHide 모드인데도 화면 하단에 빈 공간이 남아 최대화 창이 화면 전체를 쓰지 못한다.
- **근거**: `src/config/krema.kcfg:42-45`, `src/platform/waylanddockplatform.cpp:92-95`
- **우선순위**: P0
- **자동화**: `FIXTURE: 최대화 가능한 fixture 창 + 그 창이 자기 geometry를 보고하도록 확장` (exclusive zone 효과는 다른 창의 크기 변화로만 관찰 가능)

#### QA-VIS-016: 숨김 상태에서 독 패널 영역 클릭이 아래 창으로 통과 (Input Region)
- **사용자 동작**: AutoHide 모드로 독이 숨겨져 있는 상태에서, 원래 독 패널이 차지하던 위치(가장자리 4px 트리거 스트립 제외)의 바탕화면이나 아래쪽 앱 창을 마우스로 클릭한다.
- **기대 결과**: 독의 input region이 4px 가장자리 트리거 스트립(`triggerStripHeight = 4`)으로 축소되어 있으므로, 클릭 이벤트가 독 서피스에 막히지 않고 아래의 앱 창이나 바탕화면으로 완벽히 전달된다.
- **실패 징후**: 독이 숨겨져 있음에도 불구하고 이전 독 패널 영역을 클릭할 때 아래 창이 클릭되지 않고 먹통이 된다.
- **근거**: `src/utils/inputregion.cpp:12-32`, `src/shell/dockvisibilitycontroller.cpp:249-266`, `src/platform/waylanddockplatform.cpp:115`
- **우선순위**: P0
- **자동화**: `FIXTURE: 수신한 입력 이벤트를 기록하는 fixture 창` (클릭 통과 여부는 아래 창이 이벤트를 받았는지로만 판정 가능)

#### QA-VIS-017: 표시 상태에서 아이콘 주변 4px 마진 및 Zoom Overflow 영역 포인터 수신
- **사용자 동작**: 독이 표시된 상태에서 패널 외곽 4px 마진 영역 및 포커스 시 아이콘이 줌(Zoom)되어 튀어나오는 상단 오버플로우 영역(`zoomOverflowHeight`)에 마우스를 올린다.
- **기대 결과**: 마우스 포인터가 오버플로우 영역에 접근하더라도 인풋 리전 마스크(`computeDockInputRegion`) 내에 포함되어 있으므로, 마우스 포커스가 아래 창으로 떨어지지 않고 독의 호버 상태 및 확대 효과가 지속된다.
- **실패 징후**: 아이콘 확대로 튀어나온 부분을 클릭하거나 호버할 때 클릭이 아래 창으로 빠져나가거나 호버가 해제된다.
- **근거**: `src/utils/inputregion.cpp:34-70`, `src/shell/dockvisibilitycontroller.cpp:249-266`
- **우선순위**: P1
- **자동화**: `AUTO-REQ` — 클라이언트가 요청·계산한 값(LayerShellQt anchors/margins, QQuickWindow geometry, Flow 방향, 패널 width/height 축)까지 검증. 컴포지터가 실제로 화면 그 위치에 배치했는지는 현재 하네스로 미검증

#### QA-VIS-018: MonitorMode = PrimaryOnly(기본값) 설정 시 주 모니터에만 독 생성
- **사용자 동작**: `MonitorMode`를 `0` (PrimaryOnly, 기본값)으로 설정하고 다중 디스플레이 환경을 구동한다.
- **기대 결과**: `QGuiApplication::primaryScreen()`에 해당하는 디스플레이에만 단 하나의 `DockShell` 및 독 서피스가 생겨나며, 서브 모니터에는 독이 생성되지 않는다.
- **실패 징후**: 주 모니터가 아닌 보조 모니터에 독이 뜨거나 모든 모니터에 독이 생성된다.
- **근거**: `src/config/krema.kcfg:182-185`, `src/shell/multidockmanager.cpp:134-142`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-VIS-019: MonitorMode = AllScreens 설정 시 모든 연결된 모니터에 독 독립 생성
- **사용자 동작**: `MonitorMode`를 `1` (AllScreens)로 변경한다.
- **기대 결과**: 현재 시스템에 연결된 모든 활성 모니터(`QGuiApplication::screens()`) 각각에 별도의 `DockShell` 서피스가 독립적으로 생성되어 배치된다.
- **실패 징후**: 일부 모니터에 독이 뜨지 않거나, 한 모니터에 여러 독 서피스가 중복 생성된다.
- **근거**: `src/config/krema.kcfg:182-185`, `src/shell/multidockmanager.cpp:144-153`
- **우선순위**: P1
- **자동화**: `FIXTURE: 다중 virtual 출력 2개 이상`

#### QA-VIS-020: MonitorMode = FollowActive (Focus/Mouse) 설정 시 활성 모니터로 독 전환
- **사용자 동작**: `MonitorMode`를 `2` (FollowActive)로 설정하고, `FollowActiveTrigger`를 `1` (Focus) 또는 `0` (Mouse)으로 설정한 후 마우스나 창 포커스를 보조 모니터로 이동시킨다.
- **기대 결과**: 이전 모니터의 독 서피스는 숨겨지고(`setShellVisible(false)` 및 `setExclusiveZone(0)`), 포커스/마우스가 위치한 신규 모니터의 독 서피스가 활성화되어 표시된다.
- **실패 징후**: 포커스를 보조 모니터로 옮겨도 독이 이전 모니터에 갇혀있거나, 양쪽 모니터에서 독이 깜빡거린다.
- **근거**: `src/config/krema.kcfg:182-190`, `src/shell/multidockmanager.cpp:197-251,280-325`
- **우선순위**: P1
- **자동화**: `FIXTURE: 다중 virtual 출력 및 토플레벨 창`

#### QA-VIS-021: 디스플레이 핫플러그(모니터 동적 연결/해제) 토폴로지 디바운스 재배치
- **사용자 동작**: 독 실행 중 보조 모니터를 케이블로 연결하거나 제거한다 (`screenAdded`, `screenRemoved`).
- **기대 결과**: 300ms 디바운스 타이머(`m_topologyDebounce`) 실행 후 토폴로지 변경이 안정화되면 설정된 `MonitorMode` 정책에 맞춰 독 서피스를 재구성/재배치한다. 제거된 모니터의 `DockShell`은 즉시 파괴된다.
- **실패 징후**: 모니터 해제 시 댕글링 포인터로 인한 세그멘테이션 오류가 발생하거나 모니터를 꽂아도 독이 새로 뜨지 않는다.
- **근거**: `src/shell/multidockmanager.cpp:328-361`
- **우선순위**: P1
- **자동화**: `FIXTURE: 다중 virtual 출력 동적 추가/제거`

#### QA-VIS-022: 해상도 및 화면 배율(Geometry) 변경 시 독 중앙 재배치
- **사용자 동작**: 시스템 설정에서 모니터 해상도를 변경하거나 화면 배율(DPI Scale)을 조절한다 (`geometryChanged`).
- **기대 결과**: `handleScreenGeometryChanged` 시그널이 트리거되어 `updateSize()`가 호출되고, 변경된 해상도 너비/높이에 맞춰 패널 서피스 크기와 화면 중앙 위치 좌표가 재계산되어 즉시 반영된다.
- **실패 징후**: 해상도를 줄였을 때 독이 화면 밖으로 치우치거나 해상도를 늘렸을 때 화면 중앙이 아닌 어정쩡한 위치에 멈춘다.
- **근거**: `src/shell/dockview.cpp:287-302`
- **우선순위**: P1
- **자동화**: `AUTO-REQ` — 클라이언트가 요청·계산한 값(LayerShellQt anchors/margins, QQuickWindow geometry, Flow 방향, 패널 width/height 축)까지 검증. 컴포지터가 실제로 화면 그 위치에 배치했는지는 현재 하네스로 미검증

#### QA-VIS-023: DPMS Off/On 및 화면 잠금 해제(Screen Lock) 시 독 서피스 복구
- **사용자 동작**: 화면이 절전 모드(DPMS Off)로 들어갔다 깨어나거나 화면 잠금을 해제한다 (`ScreenSaver ActiveChanged`).
- **기대 결과**: Wayland 컴포지터에 의해 파괴되었을 수 있는 layer-shell 서피스를 `hide()` -> `updateSize()` -> `applyBackgroundStyle()` -> `show()` 시퀀스를 거쳐 신규 서피스로 재창조하여 독을 완벽히 복구한다.
- **실패 징후**: 화면 잠금을 해제했을 때 독이 화면에서 완전히 사라져 프로세스를 재시작해야만 나온다.
- **근거**: `src/shell/dockview.cpp:304-317`
- **우선순위**: P1
- **자동화**: `MANUAL: org.freedesktop.ScreenSaver D-Bus 및 Wayland 서피스 재창조 검증 필요`

#### QA-VIS-024: 독 표시/숨김 시 방향별 Slide 및 Opacity Fade 애니메이션 동작
- **사용자 동작**: `ScreenTransition` 설정을 확인하고 독 표시/숨김 이벤트를 발생시킨다.
- **기대 결과**: 독 패널 슬라이드 이동 시 `Kirigami.Units.longDuration`(250ms) 동안 `Easing.InOutQuad` 이징 곡선을 따라 지정된 가장자리 바깥 위치(`_panelEdgePos`)와 표시 위치 사이를 부드럽게 이동하며, 불투명도(`opacity`) 페이드 효과가 함께 적용된다.
- **실패 징후**: 애니메이션 없이 패널이 틱-틱 튀면서 나타나거나, 애니메이션 도중 패널 위치 계산 오류로 끊김 현상이 발생한다.
- **근거**: `src/config/krema.kcfg:192-195`, `src/qml/main.qml:640-652,674-696`
- **우선순위**: P1
- **자동화**: `AUTO`

## ITEM — 독 항목·작업 관리

#### QA-ITEM-001: 미실행 런처 아이콘 클릭 시 앱 실행 및 바운스 시각 피드백
- **사용자 동작**: 독에서 현재 실행 중이지 않은 고정 런처 아이콘(예: Dolphin)을 마우스 좌클릭한다.
- **기대 결과**: 클릭 직후 바운스(Bounce) 애니메이션이 시작되며, 앱 실행 프로세스가 완료되어 창이 나타날 때까지 아이콘이 튀어 오른다. 창이 생성되면 바운스가 멈추고 실행 상태 점(Dot)이 표시된다.
- **실패 징후**: 아이콘을 클릭해도 앱이 실행되지 않거나, 실행되는 동안 아이콘 바운스 시각 피드백이 나타나지 않음.
- **근거**: `src/models/dockactions.cpp:33-35`, `src/qml/DockItem.qml:175-199`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-ITEM-002: 단일 창 실행 중인 앱 아이콘 클릭 시 창 전면 활성화
- **사용자 동작**: 창이 1개 켜져 있으나 다른 창 뒤에 가려져 있거나 비활성화된 앱 아이콘을 마우스 좌클릭한다.
- **기대 결과**: 해당 앱 창이 최상단 전면으로 전환되며 입력 포커스를 받는다.
- **실패 징후**: 클릭해도 해당 창이 앞으로 나오지 않거나 포커스를 얻지 못함.
- **근거**: `src/models/dockactions.cpp:29-31`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개 (비활성)`

#### QA-ITEM-003: 이미 활성화된 단일 창 앱 아이콘 클릭 시 창 최소화
- **사용자 동작**: 현재 화면 전면에서 포커스를 갖고 있는 단일 창 앱의 아이콘을 마우스 좌클릭한다.
- **기대 결과**: 해당 창이 즉시 최소화(Minimize)되어 화면에서 사라지고, 독 아이콘 하단의 실행 상태 점 불투명도가 낮아진다(0.8 -> 0.4).
- **실패 징후**: 활성화된 상태에서 아이콘을 클릭해도 창이 최소화되지 않거나 반응이 없음.
- **근거**: `src/models/dockactions.cpp:30`, `src/qml/DockItem.qml:320-325`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개 (활성)`

#### QA-ITEM-004: 창이 여러 개인 앱 아이콘 클릭 시 창 순환 및 최상단 활성화
- **사용자 동작**: 동일한 앱의 창이 2개 이상 켜져 있는 상태에서 해당 앱 아이콘을 마우스 좌클릭한다.
- **기대 결과**: 그룹화된 창들 중에서 최근 포커스를 가졌던 창이 전면으로 오거나, 클릭할 때마다 자식 창들이 순차적으로 최상단에 활성화된다.
- **실패 징후**: 여러 창 중 특정 창만 계속 고정되어 나타나거나 클릭 반응이 동작하지 않음.
- **근거**: `src/models/dockactions.cpp:29-31`, `src/qml/main.qml:334-336`
- **우선순위**: P1
- **자동화**: `FIXTURE: 동일 앱 토플레벨 창 2개`

#### QA-ITEM-005: 앱 아이콘 가운데 클릭 시 새 인스턴스 실행
- **사용자 동작**: 이미 실행 중인 앱 아이콘 위에서 마우스 가운데 버튼(휠 클릭)을 누른다.
- **기대 결과**: 기존 활성 창을 최소화하거나 전환하지 않고, 해당 애플리케이션의 새로운 창(New Instance)을 하나 더 실행한다.
- **실패 징후**: 가운데 클릭 시 아무 반응이 없거나 새 인스턴스 대신 기존 창이 활성화/최소화됨.
- **근거**: `src/models/dockactions.cpp:38-45`, `src/qml/main.qml:338`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-ITEM-006: 다중 창 앱 아이콘 위 마우스 휠 스크롤 시 자식 창 순환
- **사용자 동작**: 2개 이상의 창이 열려 있는 앱 아이콘 위에 커서를 올리고 마우스 휠을 위/아래로 스크롤한다.
- **기대 결과**: 휠을 위로 올리면 이전 자식 창, 아래로 내리면 다음 자식 창이 순서대로 화면 최상단으로 전환되어 활성화된다.
- **실패 징후**: 휠 스크롤 시 창 전환이 이루어지지 않거나 방향과 반대로 전환됨.
- **근거**: `src/models/dockactions.cpp:66-107`, `src/qml/main.qml:341-344`
- **우선순위**: P1
- **자동화**: `FIXTURE: 동일 앱 토플레벨 창 3개`

#### QA-ITEM-007: 커서 접근 시 포물선 확대 배율 및 이웃 감쇄 효과 적용
- **사용자 동작**: 독의 특정 아이콘 중앙에 마우스 커서를 위치시킨다.
- **기대 결과**: 커서 바로 아래의 아이콘이 설정된 `MaxZoomFactor`(기본 1.6배) 크기로 확대되며, 좌우 이웃 아이콘들은 거리(가우스 곡선)에 따라 가깝고 멀어짐에 맞춰 매끄럽게 감소된 배율로 함께 확대된다.
- **실패 징후**: 포물선 확대가 적용되지 않고 아이콘 크기가 고정되어 있거나, 이웃 아이콘의 크기 변화 단계가 끊겨 어색하게 보임.
- **근거**: `src/utils/zoomcalculator.h:17-26`, `src/qml/DockItem.qml:88-97`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-ITEM-008: 마우스 커서가 독을 떠날 때 포물선 확대 원복 및 히스테리시스 유지
- **사용자 동작**: 확대된 독 아이콘 영역에서 마우스를 이동시켜 아이콘 사이 틈을 지나거나 독 외곽 밖으로 벗어난다.
- **기대 결과**: 아이콘 사이의 미세한 간격을 통과할 때는 확대 상태가 흔들림 없이 유지(`_zoomActive`)되며, 커서가 독 영역 밖으로 완전히 이탈했을 때만 모든 아이콘이 기본 크기(1.0배)로 복원된다.
- **실패 징후**: 아이콘 사이를 지날 때 확대 효과가 빠르게 깜빡이거나, 독을 벗어나도 아이콘 확대가 원래대로 돌아오지 않음.
- **근거**: `src/qml/main.qml:312-327`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-ITEM-009: 미실행 런처 아이콘 호버 시 지연 후 텍스트 툴팁 표시
- **사용자 동작**: 실행 중이지 않은 고정 런처 아이콘 위에 마우스 커서를 올리고 500ms(`PreviewHoverDelay` 기본값) 동안 대기한다.
- **기대 결과**: 설정된 대기 시간이 지난 후 해당 애플리케이션의 이름(displayName)이 적힌 깔끔한 텍스트 툴팁 팝업이 아이콘 반대편 외곽에 나타난다.
- **실패 징후**: 툴팁이 바로 뜨거나 500ms 대기 후에도 전혀 나타나지 않음.
- **근거**: `src/qml/main.qml:742-770`, `src/config/krema.kcfg:233-236`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-ITEM-010: 실행 중인 창 아이콘 호버 시 윈도우 미리보기 전환
- **사용자 동작**: 실행 중인 창이 존재하는 앱 아이콘 위에 마우스 커서를 올리고 500ms 동안 대기한다.
- **기대 결과**: 단순 텍스트 툴팁 대신 썸네일 형태의 윈도우 미리보기(Window Preview) 팝업이 활성화되어 출력된다.
- **실패 징후**: 창이 실행 중임에도 윈도우 미리보기가 생성되지 않고 텍스트 툴팁만 나타나거나 아무것도 뜨지 않음.
- **근거**: `src/qml/main.qml:745-750`, `src/config/krema.kcfg:227-230`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-ITEM-011: 긴 이름을 가진 애플리케이션의 툴팁 가독성 및 레이아웃 유지
- **사용자 동작**: 애플리케이션 이름(displayName)이 매우 긴 앱 아이콘 위에 커서를 올려 툴팁을 표시한다.
- **기대 결과**: 텍스트가 툴팁 박스 영역을 정상적으로 채우며, 화면 밖으로 벗어나지 않도록 패딩 및 암시적 너비(implicitWidth)가 계산되어 깨짐 없이 읽기 쉽게 표시된다.
- **실패 징후**: 긴 이름 텍스트가 툴팁 패널을 뚫고 나가거나 화면 경계 밖으로 잘려서 안 보임.
- **근거**: `src/qml/main.qml:753-772`
- **우선순위**: P2
- **자동화**: `AUTO`

#### QA-ITEM-012: 실행 중인 창 개수에 따른 상태 표시 점(Dot) 수 증가 (최대 3개)
- **사용자 동작**: 특정 앱을 새로 실행하거나 동일 앱의 창을 2개, 3개, 4개로 늘려가며 독 아이콘 하단을 관찰한다.
- **기대 결과**: 창이 1개일 때 점 1개, 2개일 때 점 2개, 3개 이상일 때 점 3개가 아이콘 가장자리에 일렬로 표시된다.
- **실패 징후**: 창 개수가 늘어나도 점 개수가 변경되지 않거나 3개 초과로 계속 생성되어 레이아웃이 침범됨.
- **근거**: `src/qml/DockItem.qml:283-330`
- **우선순위**: P1
- **자동화**: `FIXTURE: 동일 앱 토플레벨 창 3개`

#### QA-ITEM-013: 최소화 및 활성화 상태에 따른 표시 점 시각 구분 (투명도/크기)
- **사용자 동작**: 실행 중인 창을 활성화 상태, 비활성화 상태, 최소화 상태로 각각 변경하며 하단 점을 관찰한다.
- **기대 결과**: 활성 창인 경우 점 크기가 4px로 커지며 강조되고, 최소화된 창인 경우 점의 투명도가 낮아져(opacity 0.4) 최소화되었음을 한눈에 구분할 수 있다.
- **실패 징후**: 최소화 상태나 활성화 상태가 점의 크기나 불투명도에 시각적으로 반영되지 않음.
- **근거**: `src/qml/DockItem.qml:303-328`
- **우선순위**: P2
- **자동화**: `FIXTURE: 토플레벨 창 1개 (최소화)`

#### QA-ITEM-014: 앱 종료 시 상태 표시 점 즉시 제거
- **사용자 동작**: 실행 중이던 앱의 모든 창을 닫아 앱을 완전 종료한다.
- **기대 결과**: 앱 종료 신호 수신 즉시 독 아이콘 하단의 상태 표시 점이 사라지며, 미고정 앱인 경우 독에서 항목 자체가 즉시 제거된다.
- **실패 징후**: 앱을 종료해도 상태 점이 잔상으로 남아 있거나 미고정 아이콘이 사라지지 않음.
- **근거**: `src/models/dockmodel.cpp:88-93`, `src/qml/DockItem.qml:284-288`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-ITEM-015: 독 내부 마우스 드래그로 아이콘 위치 순서 변경 및 타겟 인디케이터 표시
- **사용자 동작**: 독의 아이콘 하나를 마우스 좌클릭 후 300ms 이상 누른 상태로 10px 이상 이동하여 다른 아이콘 사이 위치로 드래그한다.
- **기대 결과**: 반투명 고스트 아이콘이 마우스 커서를 따라다니며, 삽입될 위치에 하이라이트 선(Target Indicator)이 표시된다. 마우스를 떼면 해당 위치로 아이콘 순서가 변경 및 저장된다.
- **실패 징후**: 드래그 중 고스트/타겟 선 피드백이 없거나 마우스를 떼었을 때 아이콘 위치 순서가 변경되지 않음.
- **근거**: `src/models/dockactions.cpp:109-129`, `src/qml/main.qml:330-335`, `src/qml/main.qml:724-734`
- **우선순위**: P1
- **자동화**: `MANUAL: 드래그 앤 드롭 합성 입력 미검증`

#### QA-ITEM-016: 고정된 아이콘을 독 밖으로 드래그하여 고정 해제 (Unpin)
- **사용자 동작**: 고정(Pinned)된 런처 아이콘을 마우스로 잡고 독 패널 영역 외부 먼 곳으로 끌어낸 뒤 마우스 버튼을 놓는다.
- **기대 결과**: 해당 아이콘의 고정이 해제(Unpin)되어 독에서 제거된다. (단, 해당 앱이 실행 중인 창이 있다면 런처 고정만 해제되고 실행 창 항목으로 전환됨)
- **실패 징후**: 독 패널 밖으로 끌어내어 떼어도 고정이 해제되지 않고 원래 위치로 복귀함.
- **근거**: `src/models/dockactions.cpp:131-149`, `src/qml/main.qml:328-330`
- **우선순위**: P1
- **자동화**: `MANUAL: 드래그 앤 드롭 합성 입력 미검증`

#### QA-ITEM-017: 외부 파일/URL을 실행 앱 아이콘으로 드롭하여 파일 열기
- **사용자 동작**: 외부 파일 관리자나 다른 앱에서 파일 URL을 끌어와 해당 파일 형식을 지원하는 독의 앱 아이콘 위로 드롭한다.
- **기대 결과**: 외부 드롭 타겟 인디케이터가 해당 아이콘을 감싸며 강조 표시되고, 드롭 시 해당 애플리케이션이 전달받은 파일 URL을 열도록 요청(`requestOpenUrls`)이 전달된다.
- **실패 징후**: 드롭 시 해당 앱으로 파일 열기가 수행되지 않거나 시각적 드롭 강조 피드백이 발생하지 않음.
- **근거**: `src/models/dockactions.cpp:151-158`, `src/qml/main.qml:712-723`
- **우선순위**: P1
- **자동화**: `MANUAL: 외부 앱과의 DND 상호작용 미검증`

#### QA-ITEM-018: 외부 .desktop 파일 또는 applications: URL을 독 빈 공간에 드롭하여 고정
- **사용자 동작**: 외부 애플리케이션(.desktop 파일) 또는 `applications:` 프로토콜 URL을 독의 여백 공간에 드롭한다.
- **기대 결과**: 해당 애플리케이션이 독의 고정 런처 목록(`pinnedLaunchers`)에 새롭게 추가되어 새로운 아이콘으로 독에 고정된다.
- **실패 징후**: .desktop 파일을 드롭해도 독에 고정 아이콘으로 등록되지 않음.
- **근거**: `src/models/dockactions.cpp:109-129`, `src/qml/main.qml:712-723`
- **우선순위**: P1
- **자동화**: `MANUAL: 외부 DND 합성 입력 미검증`

#### QA-ITEM-019: IconSize 및 IconSpacing 설정값 변경 즉시 반영
- **사용자 동작**: 설정에서 `IconSize`를 48에서 96으로 키우거나, `IconSpacing`을 4에서 16으로 올린다.
- **기대 결과**: 독 재시작 없이 모든 아이콘 크기와 아이콘 간의 간격이 실시간으로 커지며 독 패널 전체의 너비/높이가 매끄럽게 재조정된다.
- **실패 징후**: 설정값을 변경해도 독의 아이콘 크기나 간격이 변하지 않고 이전 상태를 유지함.
- **근거**: `src/config/krema.kcfg:10-23`, `src/qml/main.qml:704-710`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-ITEM-020: MaxZoomFactor 설정값 변경 시 포물선 확대 최대 배율 즉시 반영
- **사용자 동작**: 설정에서 `MaxZoomFactor`를 기본값 1.6에서 2.0(최대)으로 수정한 뒤 마우스를 아이콘 위에 올려본다.
- **기대 결과**: 마우스 커서가 닿은 아이콘이 기존보다 훨씬 더 크게(2.0배) 확대되어 출력된다. 반대로 1.0(최소)으로 내리면 확대 효과가 전혀 일어나지 않는다.
- **실패 징후**: `MaxZoomFactor`를 변경해도 커서 호버 시 확대 배율이 변하지 않음.
- **근거**: `src/config/krema.kcfg:25-30`, `src/utils/zoomcalculator.h:17-26`, `src/qml/DockItem.qml:89-97`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-ITEM-021: IconNormalization 설정 변경 시 아이콘 투명 여백 제거 및 시각적 크기 균일화
- **사용자 동작**: 설정에서 `IconNormalization` 옵션을 켜짐(`true`) 및 꺼짐(`false`)으로 교체 변경한다.
- **기대 결과**: 켜진 상태에서는 원본 이미지 내 투명 패딩이 자동으로 감지 및 잘려 나가 모든 앱 아이콘의 실제 그래픽 영역이 균일한 꽉 찬 크기로 맞춰진다. 꺼진 상태에서는 각 아이콘 디자인 고유의 투명 여백이 유지된다.
- **실패 징후**: `IconNormalization` 토글 시 아이콘들의 크기 정렬이나 여백 상태에 아무런 시각적 변화가 없음.
- **근거**: `src/config/krema.kcfg:32-35`, `src/models/taskiconprovider.h:35-50`, `src/models/taskiconprovider.cpp`
- **우선순위**: P2
- **자동화**: `FIXTURE: 투명 여백이 있는 실제 아이콘 리소스. 컨테이너의 breeze 아이콘은 모두 정규화 차이가 드러나지 않는다`

#### QA-ITEM-022: VirtualDesktopMode=1 설정 시 다른 가상 데스크톱 창 아이콘 불투명도 적용
- **사용자 동작**: `VirtualDesktopMode`를 `1` (DimOtherDesktops)로 설정하고, 현재 데스크톱이 아닌 다른 가상 데스크톱에만 창이 존재하는 앱 아이콘을 관찰한다.
- **기대 결과**: 해당 앱 아이콘의 불투명도가 설정된 `OtherDesktopOpacity`(기본값 0.4)로 흐려져 다른 데스크톱에 있는 창임을 나타낸다.
- **실패 징후**: 다른 가상 데스크톱의 창 아이콘이 흐려지지 않고 정상 불투명도(1.0)로 똑같이 표시됨.
- **근거**: `src/config/krema.kcfg:259-270`, `src/models/dockmodel.cpp:188-208`, `src/qml/DockItem.qml:128-132`
- **우선순위**: P1
- **자동화**: `FIXTURE: 가상 데스크톱 2개 이상 및 2번 데스크톱에 위치한 창`

#### QA-ITEM-023: VirtualDesktopMode=2 설정 시 현재 가상 데스크톱 창만 독에 표시
- **사용자 동작**: `VirtualDesktopMode`를 `2` (CurrentOnly)로 변경한 후 가상 데스크톱을 1번에서 2번으로 전환해본다.
- **기대 결과**: 1번 데스크톱에만 켜진 창 항목은 독에서 숨겨지며, 가상 데스크톱을 전환하면 전환된 데스크톱에 속한 창들만 독 항목에 동적으로 필터링되어 나타난다.
- **실패 징후**: CurrentOnly 모드임에도 다른 가상 데스크톱의 창이 독 항목에 계속 남아 표시되거나, 데스크톱 전환 시 독 항목이 갱신되지 않음.
- **근거**: `src/config/krema.kcfg:259-264`, `src/models/dockmodel.cpp:177-186`
- **우선순위**: P1
- **자동화**: `FIXTURE: 다중 가상 데스크톱 구성`

#### QA-ITEM-024: 아이콘 리소스가 없는 앱의 플레이스홀더 대체 표시
- **사용자 동작**: 시스템 테마에 아이콘이 존재하지 않거나 유효하지 않은 실행 파일의 런처를 독에 표시한다.
- **기대 결과**: 아이콘 로딩 실패 시(`iconImage.status !== Image.Ready`), 깨진 이미지 대신 하이라이트 배경 단색 상자 안에 해당 앱 이름의 첫 글자(대문자)가 플레이스홀더 텍스트로 깔끔하게 표시된다.
- **실패 징후**: 아이콘 로딩 실패 시 아무것도 나타나지 않거나 엑스표 깨진 이미지 아이콘이 출력됨.
- **근거**: `src/qml/DockItem.qml:475-492`
- **우선순위**: P2
- **자동화**: `FIXTURE: 아이콘을 찾을 수 없는 런처 .desktop. 현재는 breeze가 모든 아이콘을 해석해 플레이스홀더 경로가 실행되지 않는다`

#### QA-ITEM-025: 독 항목이 0개일 때 패널 레이아웃 및 키보드 탐색 예외 처리
- **사용자 동작**: 모든 고정 런처를 해제하고 실행 중인 앱을 전부 종료하여 독의 항목 개수를 0개로 만든 후, 키보드 탐색을 시도한다.
- **기대 결과**: 독 패널이 항목 없이 기본 패딩만 최소한으로 유지되거나 크래시 없이 안전하게 비어있고, 키보드 방향키 탐색 입력 시 에러 없이 예외 처리된다(`if (count === 0) return`).
- **실패 징후**: 항목이 0개가 되었을 때 QML 바인딩 오류가 발생하거나 앱이 크래시됨.
- **근거**: `src/qml/main.qml:50-60`, `src/qml/main.qml:705-710`
- **우선순위**: P2
- **자동화**: `AUTO`

## PRE — 창 미리보기

### 설정 기본값 및 허용 범위
- `PreviewEnabled` (Bool): 기본값 `true` (호버 시 창 미리보기 팝업 활성화)
- `PreviewThumbnailSize` (Int): 기본값 `200` (min: `120`, max: `320`, 썸네일 폭 px)
- `PreviewHoverDelay` (Int): 기본값 `500` (ms, 미리보기 팝업 호버 지연시간)
- `PreviewHideDelay` (Int): 기본값 `200` (ms, 미리보기 팝업 숨김 지연시간)

---

#### QA-PRE-001: 창이 있는 앱 아이콘 마우스 호버 시 미리보기 팝업 표시
- **사용자 동작**: 창이 1개 이상 실행 중인 앱 아이콘 위에 마우스 커서를 `PreviewHoverDelay` (기본값: 500ms) 이상 올리고 기다린다.
- **기대 결과**: 500ms 지연 후 해당 앱의 창 썸네일과 앱 이름이 포함된 미리보기 팝업이 독 위에 나타난다.
- **실패 징후**: 호버 후 500ms가 지나도 미리보기가 뜨지 않거나, 500ms가 지나기 전에 타이머 없이 즉시 떠버린다.
- **근거**: `src/qml/main.qml:971`, `src/config/krema.kcfg:233`, `src/shell/previewcontroller.cpp:165`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-PRE-002: 실행 중이 아닌 앱(런처 전용) 호버 시 미리보기 미표시 및 텍스트 툴팁 표시
- **사용자 동작**: 실행 중인 창이 없는 런처 전용 앱 아이콘 위에 마우스 커서를 `PreviewHoverDelay` (500ms) 동안 올린다.
- **기대 결과**: 미리보기 팝업(PreviewPopup)은 뜨지 않고, 앱 이름만 표시하는 텍스트 툴팁(tooltipItem)이 독 반대편/위쪽에 표시된다.
- **실패 징후**: 빈 미리보기 팝업이 뜨거나, 텍스트 툴팁조차 뜨지 않는다.
- **근거**: `src/qml/main.qml:986`, `src/shell/previewcontroller.cpp:388`, `src/config/krema.kcfg:233`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-PRE-003: 마우스가 독과 미리보기 영역을 벗어났을 때 delayed hide 후 팝업 닫힘
- **사용자 동작**: 미리보기 팝업이 표시된 상태에서 마우스 커서를 독과 미리보기 팝업 바깥 빈 공간으로 이동시킨다.
- **기대 결과**: `PreviewHideDelay` (기본값: 200ms) 타이머가 작동한 후 미리보기 팝업이 마우스 이탈 후 200ms 뒤 완전히 사라진다.
- **실패 징후**: 마우스가 나가자마자 딜레이 없이 팝업이 사라지거나, 마우스가 나간 후에도 팝업이 계속 남아있는다.
- **근거**: `src/shell/previewcontroller.cpp:32`, `src/qml/main.qml:1048`, `src/config/krema.kcfg:239`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-PRE-004: 마우스가 다른 독 아이콘으로 이동할 때 기존 미리보기 delayed hide 및 신규 툴팁/미리보기 재시작
- **사용자 동작**: 앱 A의 미리보기 팝업이 열려 있는 상태에서 마우스 커서를 인접한 앱 B 아이콘 위로 이동시킨다.
- **기대 결과**: 앱 A의 미리보기에 대해 delayed hide (200ms)가 요청되고, 앱 B에 대해 `tooltipTimer` (500ms)가 재시작되어 타이머 만료 후 앱 B의 미리보기 또는 툴팁으로 전환된다.
- **실패 징후**: 다른 아이콘으로 이동해도 앱 A의 미리보기가 영구적으로 닫히지 않거나, 전환 타이머가 정상 작동하지 않는다.
- **근거**: `src/qml/main.qml:1052`, `src/shell/previewcontroller.cpp:177`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 2개`

#### QA-PRE-005: 독 아이콘에서 미리보기 팝업 위로 마우스 이동 시 팝업 유호버 상태 유지 및 닫힘 방지
- **사용자 동작**: 미리보기 팝업이 표시된 상태에서 마우스를 독 아이콘에서 위쪽/옆쪽 미리보기 팝업 영역 내부로 이동시킨다.
- **기대 결과**: 마우스가 독을 벗어나면서 시작된 hideTimer (200ms)가 미리보기 표면의 HoverHandler에서 `setPreviewHovered(true)` 및 `cancelHide()`를 호출함에 따라 취소되고, 미리보기 팝업이 닫히지 않고 계속 유지된다.
- **실패 징후**: 마우스가 독을 나와 미리보기 팝업으로 들어가는 도중에 팝업이 200ms 지연 후 닫혀버린다.
- **근거**: `src/shell/previewcontroller.cpp:189`, `src/qml/PreviewPopup.qml:38`, `src/config/krema.kcfg:239`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-PRE-006: 다중 창 그룹화 시 가로 행 레이아웃 및 앱 이름 헤더 표시
- **사용자 동작**: 창이 3개 열려 있는 단일 앱 아이콘 위에 마우스를 올린다.
- **기대 결과**: 미리보기 팝업 상단에 앱 이름이 굵은 글씨 헤더로 중앙 정렬되어 표시되고, 그 아래 구분선과 함께 3개의 창 썸네일(PreviewThumbnail)이 가로 행(Row)으로 정렬되어 표시된다.
- **실패 징후**: 썸네일들이 세로로 쌓이거나, 창 개수와 다르게 썸네일이 누락되거나 중복되어 나타난다.
- **근거**: `src/qml/PreviewPopup.qml:216-248`, `src/shell/previewcontroller.cpp:385`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 3개 (동일 앱 그룹화)`

#### QA-PRE-007: 창이 매우 많을 때 화면 너비 제한 내 팝업 크기 조절 및 가로 위치 제한
- **사용자 동작**: 창이 10개 이상 열려 있는 앱 아이콘 위에 마우스를 올려 미리보기를 띄운다.
- **기대 결과**: 팝업 크기(`contentWidth`)가 늘어나면서 화면 가장자리 패딩(8px) 내에 수평 위치가 제한되며, 팝업 입력 영역(input region)도 넓어진 팝업 영역에 맞춰 자동으로 재계산된다.
- **실패 징후**: 팝업이 화면 좌우 밖으로 삐져나가 잘리거나 input region 범위를 벗어나 마우스 클릭이 입력되지 않는다.
- **근거**: `src/shell/previewcontroller.cpp:495`, `src/shell/previewcontroller.cpp:552`, `src/qml/PreviewPopup.qml:191`
- **우선순위**: P2
- **자동화**: `FIXTURE: 토플레벨 창 10개 (동일 앱 그룹화)`

#### QA-PRE-008: PipeWire 노드 연결을 통한 실시간 창 미리보기 렌더링
- **사용자 동작**: 활성 창이 존재하는 상태에서 미리보기 팝업을 연다.
- **기대 결과**: TaskManager.ScreencastingRequest를 통해 얻은 KWin Screencast `nodeId`로 `PipeWireSourceItem`이 연결되어 창의 실시간 화면 썸네일이 렌더링된다.
- **실패 징후**: 실시간 창 화면이 표시되지 않고 멈춰있거나 검은 화면만 표시된다.
- **근거**: `src/qml/PreviewThumbnail.qml:49`, `src/qml/PreviewThumbnail.qml:99`, `docs/kde/pipewire-thumbnails.md:143`
- **우선순위**: P0
- **자동화**: `MANUAL: PipeWire 데몬 및 KWin Screencasting 서비스 헤드리스 미지원`

#### QA-PRE-009: PipeWire 로딩 중/실패 시 대체 아이콘 표시
- **사용자 동작**: PipeWire 스트림 연결 전 또는 PipeWire 스트림 생성 실패 시 미리보기 팝업을 관찰한다.
- **기대 결과**: `pipeWireItem.ready`가 false일 때 fallback인 `Kirigami.Icon`이 중앙에 앱 아이콘(또는 `application-x-executable`)으로 대용 표시된다.
- **실패 징후**: PipeWire 준비가 안 되었을 때 아이콘도 뜨지 않고 완전히 투명하거나 깨진 영역이 나온다.
- **근거**: `src/qml/PreviewThumbnail.qml:112-121`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개 (Screencast nodeId가 0인 상태)`

#### QA-PRE-010: 최소화된 창의 썸네일 불투명도 오버레이 표시
- **사용자 동작**: 최소화(Minimized) 상태인 창이 포함된 앱의 미리보기 팝업을 연다.
- **기대 결과**: 해당 창의 썸네일 영역 위에 50% 투명도의 `Kirigami.Theme.backgroundColor` 반투명 오버레이가 덮여 표시되어 최소화 상태임을 시각적으로 구분한다.
- **실패 징후**: 최소화된 창과 일반 창의 썸네일 시각적 구분이 전혀 없다.
- **근거**: `src/qml/PreviewThumbnail.qml:123-128`
- **우선순위**: P2
- **자동화**: `FIXTURE: 토플레벨 창 1개 (isMinimized = true)`

#### QA-PRE-011: 썸네일 클릭 시 창 활성화 및 미리보기 닫힘
- **사용자 동작**: 미리보기 팝업에서 특정 창의 썸네일 구역(MouseArea)을 좌클릭한다.
- **기대 결과**: `tasksModel.requestActivate()`가 호출되어 해당 창이 데스크톱 최상단으로 활성화되고, 미리보기 팝업은 즉시 닫힌다(`hidePreview()`).
- **실패 징후**: 클릭해도 해당 창이 활성화되지 않거나 미리보기 팝업이 계속 열려있다.
- **근거**: `src/qml/PreviewThumbnail.qml:175-188`, `src/shell/previewcontroller.cpp:171`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-PRE-012: 썸네일 상단 닫기 버튼 클릭 시 해당 창 닫기 및 마지막 창 종료 시 팝업 닫힘
- **사용자 동작**: 미리보기 썸네일 우상단의 닫기 버튼(`window-close` 아이콘 ToolButton)을 클릭한다.
- **기대 결과**: `tasksModel.requestClose()`가 호출되어 해당 창이 닫힌다. 만약 해당 앱의 마지막 창이 닫히면 `onRowsRemoved` 체인에서 parent row의 유효성을 확인하고 미리보기 팝업이 자동으로 닫힌다.
- **실패 징후**: 닫기 버튼을 눌러도 창이 닫히지 않거나, 마지막 창이 닫혔음에도 미리보기 팝업이 닫히지 않고 유령 상태로 남는다.
- **근거**: `src/qml/PreviewThumbnail.qml:140-168`, `src/qml/PreviewPopup.qml:176-184`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-PRE-013: 키보드 방향키를 통한 미리보기 썸네일 탐색 및 포커스 링 표시
- **사용자 동작**: 독 키보드 탐색 중 미리보기 열기 단축키(독 방향의 반대/수직 방향 키: 예컨대 하단 독의 경우 Up 키)를 눌러 미리보기를 켜고, 좌/우 방향키(Left/Right)를 누른다.
- **기대 결과**: `startPreviewKeyboardNav()`가 호출되어 첫 번째 썸네일에 `isKeyboardFocused` 상태가 지정되며 `previewFocusRing`(포커스 테두리)이 나타난다. Left/Right 키 입력 시 `focusedThumbnailIndex`가 이동하여 포커스 링이 위치를 옮기고 접근성 안내 메시지(제목, 활성/최소화 상태, 번호)가 낭독된다.
- **실패 징후**: 키보드 입력 시 포커스 링이 이동하지 않거나, 인덱스 범위를 벗어나 에러가 발생한다.
- **근거**: `src/qml/main.qml:128-147`, `src/qml/main.qml:173-175`, `src/shell/previewcontroller.cpp:322-339`, `src/qml/PreviewThumbnail.qml:62-72`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 2개 (그룹화)`

#### QA-PRE-014: 키보드 포커스 상태에서 Enter 키로 활성화 및 Delete 키로 창 닫기
- **사용자 동작**: 미리보기 키보드 탐색 모드에서 특정 썸네일에 포커스가 맞춰진 상태로 Enter 키, Delete 키, 또는 Esc 키를 누른다.
- **기대 결과**: Enter 키 입력 시 `activatePreviewThumbnail()`이 실행되어 창이 활성화되고 미리보기가 닫힌다. Delete 키 입력 시 `closePreviewThumbnail()`이 실행되어 해당 창이 닫히고 포커스 인덱스가 조정된다. Esc 키를 누르면 미리보기 탐색 모드가 종료된다.
- **실패 징후**: Enter/Delete 키를 눌러도 대응하는 창 활성화/닫기 동작이 수행되지 않는다.
- **근거**: `src/qml/main.qml:148-164`, `src/shell/previewcontroller.cpp:341-364`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 2개 (그룹화)`

#### QA-PRE-015: PreviewThumbnailSize 설정 변경 시 썸네일 너비 및 높이 즉시 반영
- **사용자 동작**: 설정에서 `PreviewThumbnailSize`를 기본값 200에서 최소값 120 또는 최대값 320으로 변경한다 (`krema.kcfg` 기본값: 200, 범위: 120~320).
- **기대 결과**: 미리보기 팝업 내부의 모든 썸네일 너비(`thumbnailWidth`)가 지정한 크기로 변경되고, 높이(`thumbnailHeight = thumbnailWidth * 0.7`) 및 전체 팝업 레이아웃 크기가 즉시 재계산되어 적용된다.
- **실패 징후**: 설정을 변경해도 썸네일 크기가 기존 200px로 유지된다.
- **근거**: `src/config/krema.kcfg:226-231`, `src/qml/PreviewThumbnail.qml:31-32`, `src/shell/previewcontroller.cpp:193`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-PRE-016: PreviewHoverDelay 및 PreviewHideDelay 설정 변경 시 호버/숨김 지연시간 반영
- **사용자 동작**: `PreviewHoverDelay`(기본값: 500ms)를 100ms로, `PreviewHideDelay`(기본값: 200ms)를 1000ms로 변경한 후 아이콘 호버 및 마우스 이탈을 수행한다.
- **기대 결과**: 마우스를 올렸을 때 100ms 만에 미리보기가 뜨고, 마우스가 벗어났을 때 1000ms 동안 미리보기 팝업이 유지된 후 닫힌다.
- **실패 징후**: 설정 변경 후에도 항상 기존 500ms / 200ms 지연시간으로 작동한다.
- **근거**: `src/config/krema.kcfg:233-241`, `src/qml/main.qml:971`, `src/shell/previewcontroller.cpp:32`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-PRE-017: PreviewEnabled = false 설정 시 미리보기 비활성화 및 텍스트 툴팁 대체 표시
- **사용자 동작**: `PreviewEnabled` 설정을 `false`(기본값: `true`)로 변경한 후 창이 열려 있는 앱 아이콘 위에 마우스를 500ms 동안 올린다.
- **기대 결과**: 창이 존재하는 앱이라 하더라도 미리보기 팝업은 뜨지 않으며, 일반 런처 앱과 동일하게 단순 텍스트 툴팁만 표시된다.
- **실패 징후**: `PreviewEnabled`가 false 임에도 호버 시 미리보기 팝업이 발생한다.
- **근거**: `src/config/krema.kcfg:221-224`, `src/qml/main.qml:977`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-PRE-018: 미리보기 등장 및 퇴장 시 레이어 쉘 입력 영역(Mask) 전환 및 호버 오버레이 애니메이션
- **사용자 동작**: 미리보기 팝업이 열리고 닫힐 때 visual 및 input region 상태 변화를 관찰한다.
- **기대 결과**: 미리보기 표면은 빠른 재표시를 위해 사전 매핑(pre-shown)되어 있으며, 숨김 상태일 때는 1x1 투명 마스크로 입력을 차단하다가 `doShow()` 시 팝업 사각형+여백(margin 40px)으로 QRegion mask가 확장되고 `visibleChanged(true)`가 전송된다. 썸네일 호버 시 오버레이 투명도가 Kirigami shortDuration 동안 애니메이션된다.
- **실패 징후**: 숨김 상태일 때 1x1 마스크가 아닌 empty QRegion이 설정되어 전체 화면의 클릭 입력을 차단해 버리거나, 팝업 전환 시 비정상적인 깜빡임이 발생한다.
- **근거**: `src/shell/previewcontroller.cpp:82`, `src/shell/previewcontroller.cpp:513`, `src/shell/previewcontroller.cpp:534`, `src/qml/PreviewThumbnail.qml:132-138`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-PRE-019: 화면 가장자리(양 끝) 근처 아이콘의 미리보기 팝업 클리핑 방지 및 화면 내부 제한
- **사용자 동작**: 화면 맨 왼쪽 또는 맨 오른쪽에 위치한 앱 아이콘 위에 마우스를 올려 미리보기를 연다.
- **기대 결과**: `recalcContentPosition()` 계산 시 화면 가장자리 패딩(`pad = 8`)이 적용되어 `m_contentX`가 `[pad, screenW - pad - m_contentWidth]` 범위 내로 제한되며, 팝업이 화면 바깥으로 잘려나가지 않고 완전히 화면 내부에 위치한다.
- **실패 징후**: 좌/우 끝 아이콘의 미리보기 팝업 일부가 화면 밖으로 잘려서 표시된다.
- **근거**: `src/shell/previewcontroller.cpp:486-504`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개 (화면 가장자리 독 아이콘)`

#### QA-PRE-020: 세로 독(좌/우 에지) 환경에서의 미리보기 팝업 위치 및 입력 영역 계산
- **사용자 동작**: 독 위치(`Edge`)를 좌측(2) 또는 우측(3) 세로 독으로 설정하고 앱 아이콘 위에 마우스를 올린다.
- **기대 결과**: 미리보기 팝업의 Y 위치(`m_contentY`)가 아이콘 중앙에 맞춰지며 상하 가장자리 패딩(8px) 내로 정렬되고, 레이어 쉘 앵커 및 마스크(QRegion)가 세로 독 구도에 맞게 재계산되어 정상 표시된다.
- **실패 징후**: 세로 독 모드에서 미리보기 팝업이 가로 독 위치(화면 하단)에 뜨거나 클릭 영역이 빗나간다.
- **근거**: `src/shell/previewcontroller.cpp:446-480`, `src/shell/previewcontroller.cpp:518-532`, `src/config/krema.kcfg:114`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개, 세로 독 설정`

## CMD — 컨텍스트 메뉴·전역 단축키

#### QA-CMD-001: 실행 중이지 않은 핀 아이콘 우클릭 시 메뉴 구성 및 순서 확인
- **사용자 동작**: 독의 실행 중이지 않은 핀 아이콘(예: Dolphin)에 마우스 우클릭을 하거나, 키보드 포커스 후 Menu 키를 누른다.
- **기대 결과**: 컨텍스트 메뉴가 마우스 커서 위치에 나타나며, 위에서부터 다음 순서로 정확히 표시된다: `[앱 이름 (비활성화·굵은글씨)]` -> `[구분선]` -> `Unpin from Dock` -> `New Instance` -> `[구분선]` -> `Settings...` -> `[구분선]` -> `About Krema` -> `Quit`. 실행 중이지 않으므로 `Close` 항목은 나타나지 않는다.
- **실패 징후**: 메뉴 항목 순서가 바뀌거나, `Close` 항목이 잘못 노출되거나, 헤더 이름이 굵게 표시되지 않는다.
- **근거**: `src/models/dockcontextmenu.cpp:48-102`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-CMD-002: 실행 중인 창 아이콘 우클릭 시 메뉴 구성 확인
- **사용자 동작**: 창이 1개 이상 실행 중인 독 아이콘에 마우스 우클릭을 한다.
- **기대 결과**: 컨텍스트 메뉴 항목 중 `New Instance` 아래에 구분선과 함께 `Close` 항목이 새로 포함되어 표시된다. (전체 순서: `[앱 이름]` -> `[구분선]` -> `Pin/Unpin from Dock` -> `New Instance` -> `[구분선]` -> `Close` -> `[구분선]` -> `Settings...` -> `[구분선]` -> `About Krema` -> `Quit`)
- **실패 징후**: 실행 중인 창이 있음에도 `Close` 항목이 표시되지 않는다.
- **근거**: `src/models/dockcontextmenu.cpp:83-88`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-CMD-003: 읽지 않은 알림 배지가 있는 아이콘 우클릭 시 "Clear Notifications" 노출 확인
- **사용자 동작**: 알림 배지 숫자(1 이상)가 표시된 앱 아이콘을 우클릭한다.
- **기대 결과**: `New Instance` 바로 아래에 `Clear Notifications` 항목이 추가로 표시된다.
- **실패 징후**: 배지가 존재함에도 `Clear Notifications` 항목이 보이지 않거나, 배지가 0일 때 나타난다.
- **근거**: `src/models/dockcontextmenu.cpp:73-80`
- **우선순위**: P1
- **자동화**: `FIXTURE: org.freedesktop.Notifications D-Bus 스텁 및 알림 전송`

#### QA-CMD-004: .desktop 파일의 Actions (점프 리스트) 항목 미노출 검증
- **사용자 동작**: Desktop Action(예: New Incognito Window 등 점프리스트)이 정의된 `.desktop` 파일 아이콘을 우클릭한다.
- **기대 결과**: 점프 리스트 액션 항목들이 메뉴에 포함되지 않으며, 기본 독 메뉴 구성만 나타난다 (현재 점프 리스트 파싱 로직 미구현).
- **실패 징후**: 파싱 오류로 메뉴 생성이 실패하거나 crash가 발생한다.
- **근거**: `src/models/dockcontextmenu.cpp:30-109`
- **우선순위**: P2
- **자동화**: `AUTO`

#### QA-CMD-005: 메뉴 항목 - "Pin to Dock" / "Unpin from Dock" 실행
- **사용자 동작**: 고정되지 않은 앱 아이콘 우클릭 후 `Pin to Dock` 클릭, 또는 고정된 앱 아이콘 우클릭 후 `Unpin from Dock` 클릭.
- **기대 결과**: `Pin to Dock` 클릭 시 실행 중인 핀 목록에 추가되고 독 설정(`kremarc`)에 즉시 자동 저장된다. `Unpin from Dock` 클릭 시 독에서 아이콘이 즉시 제거(창이 실행 중이 아니면 릴리스)되며 설정 파일이 업데이트된다.
- **실패 징후**: 아이콘 고정 상태가 변경되지 않거나 `kremarc` 설정 파일에 저장되지 않는다.
- **근거**: `src/models/dockcontextmenu.cpp:57-65`, `src/models/dockactions.cpp:63-79`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-CMD-006: 메뉴 항목 - "New Instance" 실행
- **사용자 동작**: 독 아이콘 우클릭 후 `New Instance` 항목을 클릭한다.
- **기대 결과**: 해당 응용프로그램의 새로운 프로세스/창 인스턴스가 즉시 실행된다.
- **실패 징후**: 새로운 인스턴스가 실행되지 않거나 아무 반응이 없다.
- **근거**: `src/models/dockcontextmenu.cpp:68-70`, `src/models/dockactions.cpp:45-52`
- **우선순위**: P1
- **자동화**: `MANUAL: 컨테이너 환경 내 실행 대상 응용프로그램 미설치`

#### QA-CMD-007: 메뉴 항목 - "Clear Notifications" 실행
- **사용자 동작**: 알림 배지가 있는 아이콘 우클릭 후 `Clear Notifications` 항목을 클릭한다.
- **기대 결과**: 해당 앱의 안읽은 알림 수 카운터가 0으로 초기화되고, 아이콘 위의 알림 배지가 즉시 사라진다.
- **실패 징후**: 알림 배지가 지워지지 않고 남아있다.
- **근거**: `src/models/dockcontextmenu.cpp:77-79`, `src/models/notificationtracker.cpp`
- **우선순위**: P1
- **자동화**: `FIXTURE: org.freedesktop.Notifications D-Bus 스텁 및 알림 전송`

#### QA-CMD-008: 메뉴 항목 - "Close" 실행
- **사용자 동작**: 실행 중인 창 아이콘 우클릭 후 `Close` 항목을 클릭한다.
- **기대 결과**: 해당 앱에 속한 모든 창에 대해 창 닫기(`requestClose`)가 요청되어 창이 닫힌다.
- **실패 징후**: 창이 닫히지 않고 유지된다.
- **근거**: `src/models/dockcontextmenu.cpp:85-87`, `src/models/dockactions.cpp:54-61`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개`

#### QA-CMD-009: 메뉴 항목 - "Settings..." 및 "About Krema" 실행
- **사용자 동작**: 우클릭 메뉴에서 `Settings...` 또는 `About Krema` 항목을 클릭한다.
- **기대 결과**: `Settings...` 클릭 시 Krema 환경설정 창이 열린다. `About Krema` 클릭 시 설정 창의 '정보(about)' 탭/페이지가 즉시 열린다.
- **실패 징후**: 설정 창이 나타나지 않거나 잘못된 탭이 열린다.
- **근거**: `src/models/dockcontextmenu.cpp:91-100`, `src/shell/dockshell.cpp:185-190`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-CMD-010: 메뉴 항목 - "Quit" 실행
- **사용자 동작**: 우클릭 메뉴에서 `Quit` 항목을 클릭한다.
- **기대 결과**: Krema 독 프로세스가 안전하게 종료된다 (`qApp->quit()`).
- **실패 징후**: 프로세스가 종료되지 않고 계속 실행된다.
- **근거**: `src/models/dockcontextmenu.cpp:101`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-CMD-011: 컨텍스트 메뉴 열림 시 독 자동 숨김 방지 및 Esc/외부 클릭으로 닫기
- **사용자 동작**: 독이 자동 숨김(AutoHide) 설정된 상태에서 아이콘 우클릭으로 메뉴를 연 후, 1) 마우스를 메뉴 밖으로 옮김, 2) Esc 키를 누르거나 독 외부를 클릭함.
- **기대 결과**: 메뉴가 열려 있는 동안 독의 상호작용 잠금(Interaction Lock)이 작동하여 독이 사라지거나 숨겨지지 않는다. Esc 키 입력이나 외부 클릭 시 컨텍스트 메뉴가 즉시 닫히고 잠금이 해제된다.
- **실패 징후**: 메뉴가 열린 상태에서 독이 숨겨지거나 메뉴가 닫히지 않는다.
- **근거**: `src/models/dockcontextmenu.cpp:104-107`, `src/shell/dockshell.cpp:183`
- **우선순위**: P1
- **자동화**: `FIXTURE: 컨텍스트 메뉴가 실제로 매핑되어야 한다. 현재 dockcontextmenu.cpp:43이 부모 없는 QMenu를 만들어 Wayland가 popup 생성을 거부하므로 "바깥 클릭·Esc로 닫힌다"를 관찰할 수 없다`

#### QA-CMD-012: 독 빈 공간(배경) 우클릭 동작
- **사용자 동작**: 독 아이콘이 없는 패널 빈 영역에 마우스 우클릭을 한다.
- **기대 결과**: 빈 영역 클릭 시(`hoveredIndex < 0`) 어떤 메뉴도 노출되지 않으며 무반응 처리된다 (배경 컨텍스트 메뉴 미구현).
- **실패 징후**: 예외가 발생하거나 잘못된 항목의 메뉴가 나타난다.
- **근거**: `src/qml/main.qml:490`
- **우선순위**: P2
- **자동화**: `AUTO`

#### QA-CMD-013: 전역 단축키 Toggle Dock 및 Focus Dock 동작
- **사용자 동작**: 키보드로 `Meta+`` 키 또는 `Meta+F5` 키를 입력한다.
- **기대 결과**: `Meta+`` 입력 시 독 표시/숨김 상태가 토글된다. `Meta+F5` 입력 시 마우스 커서가 있는 모니터의 독에 키보드 포커스가 주어지며 첫 번째 아이콘이 포커스된다.
- **실패 징후**: 단축키 입력에 독이 반응하지 않거나 키보드 포커스가 지정되지 않는다.
- **근거**: `src/app/application.cpp:182-205`
- **우선순위**: P0
- **자동화**: `MANUAL: KGlobalAccel D-Bus 데몬 및 전역 단축키 바인딩은 가상 헤드리스 환경 미검증`

#### QA-CMD-014: 전역 단축키 앱 활성화 및 새 인스턴스 실행
- **사용자 동작**: 키보드로 `Meta+1` ~ `Meta+9` 키 또는 `Meta+Shift+1` ~ `Meta+Shift+9` 키를 입력한다.
- **기대 결과**: `Meta+N` 입력 시 주 모니터 독의 N번째 항목이 활성화(창 전환 또는 앱 실행)된다. `Meta+Shift+N` 입력 시 N번째 앱의 새 인스턴스가 실행된다.
- **실패 징후**: 잘못된 순서의 앱이 작동하거나 키보드 명령이 무시된다.
- **근거**: `src/app/application.cpp:207-231`
- **우선순위**: P0
- **자동화**: `MANUAL: KGlobalAccel D-Bus 데몬 미검증 및 컨테이너 내 대상 응용프로그램 미설치`

#### QA-CMD-015: 독이 숨겨져 있을 때 전역 단축키 실행
- **사용자 동작**: AutoHide/Dodge 모드로 독이 화면에서 완전히 숨겨진 상태에서 `Meta+1` 키를 누른다.
- **기대 결과**: 독이 비가시 상태이더라도 백그라운드 KGlobalAccel 이벤트를 수신하여 1번째 앱 활성화/실행 액션이 즉시 수행된다.
- **실패 징후**: 독이 숨겨져 있을 때 전역 단축키 입력을 수신하지 못한다.
- **근거**: `src/app/application.cpp:177-232`
- **우선순위**: P1
- **자동화**: `MANUAL: KGlobalAccel D-Bus 데몬 미검증`

#### QA-CMD-016: 단일 인스턴스(중복 실행 방지) 및 자동 시작 데스크톱 파일
- **사용자 동작**: 1) Krema가 이미 실행 중인 상태에서 터미널에서 `krema` 명령을 다시 실행한다. 2) KDE 세션 로그인 시 자동 시작 동작을 확인한다.
- **기대 결과**: 1) KDBusService(Unique)에 의해 기존 프로세스에 `activateRequested`가 전달되고 두 번째 실행된 프로세스는 즉시 종료된다. 2) `com.bhyoo.krema.autostart.desktop`에 의해 KDE 로그인 시 자동으로 독이 실행된다.
- **실패 징후**: 프로세스가 2개 이상 중복 실행되어 독 창이 겹쳐 뜨거나 crash가 발생한다.
- **근거**: `src/app/application.cpp:51-55`, `src/com.bhyoo.krema.autostart.desktop.in:1-8`
- **우선순위**: P0
- **자동화**: `MANUAL: D-Bus 세션 버스 단일 서비스 선점 및 Plasma 세션 로그인 검증 필요`

## SET — 설정

#### QA-SET-001: 설정 창 진입 경로 및 중복 열기/재활성화
- **사용자 동작**: 독의 빈 공간을 우클릭하여 컨텍스트 메뉴에서 "Configure Dock..."을 클릭하거나, "About Krema..."를 클릭한다. 창이 이미 열려 있는 상태에서 다시 우클릭 메뉴의 설정/정보 항목을 선택하거나 닫은 뒤 다시 클릭한다.
- **기대 결과**: "Configure Dock..." 선택 시 Kirigami 기반의 설정 창이 뜨며 좌측 사이드바에서 Appearance 페이지가 기본 선택된다. "About Krema..." 선택 시 About 페이지가 사전 선택된 상태로 설정 창이 열린다. 창이 이미 열려 있는 경우 새 창이 뜨지 않고 기존 창이 가장 위로 올라오며(raise) 활성화(requestActivate)된다. 창을 닫은 후 다시 메뉴를 누르면 설정값이 유지된 채 창이 정상 재개된다. 설정 창이 열려 있는 동안 독은 자동 숨김/Dodge 상태이더라도 가려지지 않고 노출 상태를 유지한다(Interacting refcount 활성화).
- **실패 징후**: 중복으로 설정 창이 여러 개 뜨거나, 기존 창이 가려진 채 활성화되지 않거나, 설정 창을 열었을 때 독이 숨어버린다.
- **근거**: `src/shell/settingswindow.cpp:32-130`, `src/qml/SettingsDialog.qml:17-57`, `src/shell/dockshell.cpp:180-192`
- **우선순위**: P0
- **자동화**: `AUTO`

---

#### QA-SET-002: 설정 파일 없는 첫 실행 시 기본값 읽기
- **사용자 동작**: `~/.config/kremarc` 설정 파일이 존재하지 않는 상태에서 독을 처음 실행한다.
- **기대 결과**: `src/config/krema.kcfg`에 정의된 모든 기본값으로 독 및 설정 창이 초기화된다:
  - IconSize: 48, IconSpacing: 4, MaxZoomFactor: 1.6, IconNormalization: true, IconScale: 1.0, CornerRadius: 12, Floating: true
  - VisibilityMode: 0 (Always), DodgeActiveOnly: false, Edge: 1 (Bottom), ShowDelay: 200ms, HideDelay: 400ms
  - BackgroundOpacity: 0.6, BackgroundStyle: 0 (PanelInherit), UseAccentColor: false, UseSystemColor: true, TintColor: `#1e3a5f`
  - ShadowEnabled: false, ShadowLightX: 0, ShadowLightY: -150, ShadowLightZ: 500, ShadowLightRadius: 5.0, ShadowColor: `#80000000`, ShadowIntensity: 0.3, ShadowElevation: 15
  - PreviewEnabled: true, PreviewThumbnailSize: 200, PreviewHoverDelay: 500ms, PreviewHideDelay: 200ms
  - AttentionAnimation: 2 (Wiggle), AttentionAnimationDuration: 5초, BadgeDisplayMode: 0 (Number)
  - MonitorMode: 0 (PrimaryOnly), FollowActiveTrigger: 1 (Focus), ScreenTransition: 0 (Fade)
  - VirtualDesktopMode: 1 (DimOtherDesktops), OtherDesktopOpacity: 0.4
  - PinnedLaunchers: Dolphin, Konsole, Kate, SystemSettings 4개 기본 핀
- **실패 징후**: 기본 설정값이 적용되지 않아 독 아이콘이 잘리거나, 무한 루프 오류가 발생하거나, 투명도가 0이 된다.
- **근거**: `src/config/krema.kcfg:12-262`, `src/config/krema.kcfgc:1-7`
- **우선순위**: P0
- **자동화**: `FIXTURE: kremarc 파일 제거 후 실행`

---

#### QA-SET-003: 설정 변경값의 프로세스 재시작 후 지속성
- **사용자 동작**: 설정 창에서 여러 값(예: 아이콘 크기 64, 배경 스타일 Tinted, 모서리 둥글기 20)을 변경한 후 독 프로세스를 종료하고 다시 실행한다.
- **기대 결과**: `~/.config/kremarc` 파일의 `[General]` 그룹에 변경된 키-값들이 자동 저장되며, 재시작 후에도 설정 창과 독 패널이 이전에 변경한 값을 그대로 유지하여 로드된다.
- **실패 징후**: 재시작 시 설정이 기본값으로 리셋되거나, `kremarc` 파일에 저장이 누락된다.
- **근거**: `src/config/krema.kcfg:7`, `src/shell/dockshell.cpp:115-180`
- **우선순위**: P0
- **자동화**: `FIXTURE: kremarc 파일 저장 검증 및 프로세스 재시작`

---

#### QA-SET-004: 아이콘 크기 및 간격 조절
- **사용자 동작**: 설정 > Appearance > Icons에서 "Icon size" SpinBox(범위: 24~96, 단계: 4, 기본값: 48)와 "Icon spacing" SpinBox(범위: 0~16, 단계: 1, 기본값: 4) 값을 조절한다.
- **기대 결과**: 값 변경 즉시 독 패널의 전체 높이/너비 및 아이콘들 간의 간격이 실시간으로 커지거나 줄어든다. 수치 변경 시 별도의 적용(Apply) 버튼 클릭이나 재시작 없이 화면에 즉각 반영된다.
- **실패 징후**: 아이콘 크기가 변경되어도 패널 높이가 업데이트되지 않아 아이콘이 잘리거나, 간격 조절 시 아이콘이 서로 겹친다.
- **근거**: `src/qml/settings/AppearancePage.qml:21-36`, `src/config/krema.kcfg:13-24`, `src/qml/main.qml:628-634`, `src/shell/dockshell.cpp:125-126`
- **우선순위**: P1
- **자동화**: `AUTO`

---

#### QA-SET-005: 줌 배율 및 아이콘 비율/정규화 설정
- **사용자 동작**: Appearance > Icons에서 "Zoom factor" Slider(범위: 1.0~2.0, 단계: 0.1, 기본값: 1.6), "Icon size normalization" Switch(기본값: true), "Icon scale" Slider(범위: 0.5~1.0, 단계: 0.05, 기본값: 1.0)를 조절한다.
- **기대 결과**:
  - Zoom factor를 2.0x로 변경 시 마우스 호버 시 아이콘 확대 비율이 최대 2배까지 늘어나며, 독 패널의 예비 서피스 고도가 이에 맞게 확보되어 확대한 아이콘이 패널 외부로 잘리지 않는다.
  - Normalization 스위치 토글 시 여백이 포함된 아이콘의 패딩 제거 여부가 캐시 정산(`clearCache()`, `bumpIconCacheVersion()`)과 함께 즉시 적용된다.
  - Icon scale을 0.5로 내리면 셀 내부에서 아이콘의 여백 비율이 늘어나 아이콘이 소형화된다.
- **실패 징후**: 줌 확대 시 패널 상단에서 아이콘이 잘리거나, Normalization 변경 시 아이콘 이미지 캐시가 갱신되지 않아 모양 변화가 없다.
- **근거**: `src/qml/settings/AppearancePage.qml:38-118`, `src/config/krema.kcfg:26-51`, `src/shell/dockshell.cpp:159-170`
- **우선순위**: P1
- **자동화**: `AUTO`

---

#### QA-SET-006: 알림 배지 및 주의 애니메이션 설정
- **사용자 동작**: Appearance > Icons에서 "Attention animation" ComboBox(None, Bounce, Wiggle, Pulse, Glow, Dot color, Blink 중 선택, 기본값: Wiggle)를 선택하고, 서브 항목으로 나타나는 "Attention duration" SpinBox(범위: 0~60초, 기본값: 5초)를 조절한다. 또한 "Badge display" ComboBox(Number, Dot, Off 중 선택, 기본값: Number)를 변경한다.
- **기대 결과**:
  - Attention animation이 None(0)이 아닌 경우에만 Attention duration 설정 항목이 노출된다. Duration을 0으로 설정하면 주의 필요 알림이 무한 지속된다.
  - Badge display를 Number에서 Dot으로 변경하면 숫자가 표시되던 배지가 단순 점 표식으로 바뀌고, Off 선택 시 배지가 감춰진다.
- **실패 징후**: Attention animation을 None으로 바꿨음에도 Duration 위젯이 여전히 화면에 남아있거나, Badge display 모드가 변경되어도 배지 형태가 전환되지 않는다.
- **근거**: `src/qml/settings/AppearancePage.qml:120-165`, `src/config/krema.kcfg:200-221`
- **우선순위**: P1
- **자동화**: `FIXTURE: org.freedesktop.Notifications D-Bus 스텁 및 demansAttention 신호`

---

#### QA-SET-007: 배경 스타일 선택 및 실시간 시각 효과 변경
- **사용자 동작**: Appearance > Background에서 "Style" ComboBox를 조절한다: Panel Inherit (0), Transparent (1), Tinted (2), Acrylic (3).
- **기대 결과**:
  - Panel Inherit 선택 시: 컴포지터의 BlurBehind 및 BackgroundContrast 효과가 적용되며, KDE 헤더 색상 기반 패널이 표시된다.
  - Transparent 선택 시: 독 배경 rectangular 색상이 완전 투명(`Qt.transparent`)해지며 블러/대비 컴포지터 효과가 제거된다.
  - Tinted 선택 시: 커스텀/시스템 틴트 색상의 단색 패널이 렌더링되며 컴포지터 블러는 비활성화된다.
  - Acrylic 선택 시: 컴포지터 BlurBehind 위에 GPU 노이즈 셰이더(`acrylic_overlay.frag.qsb`)가 합성된 아크릴 유리 효과가 렌더링된다.
  - 만약 가상 환경 등에서 특정 스타일이 불가할 경우 목록에 `(unavailable)` 표기되고 선택이 거부된다.
- **실패 징후**: Transparent를 선택했음에도 블러 레이어가 잔상으로 남거나, Acrylic 선택 시 셰이더 오류로 패널이 검은색으로 깨진다.
- **근거**: `src/qml/settings/AppearancePage.qml:173-207`, `src/style/backgroundstyle.cpp:24-50`, `src/config/krema.kcfg:85-90`, `src/qml/main.qml:635-667`
- **우선순위**: P0
- **자동화**: `MANUAL: KWindowEffects blur-behind/contrast 효과 및 GPU 노이즈 셰이더 합성은 헤드리스 가상 환경에서 시각적 확인 불가`

---

#### QA-SET-008: 배경 투명도 조절 (0%는 도달 불가 — krema.kcfg:82의 min이 0.1)
- **사용자 동작**: Appearance > Background에서 "Opacity" Slider(범위: 0.0~1.0, 단계: 0.05, 기본값: 0.6)를 조절한다. (Transparent 스타일일 때는 Opacity 슬라이더가 숨겨짐)
- **기대 결과**: 슬라이더를 움직이면 독 배경의 alpha 값이 실시간 변경되어 독 뒤의 바탕화면 투과율이 달라진다. Opacity를 0.0(0%)으로 밀면 `qFuzzyIsNull(opacity)` 조건이 참이 되어 `removeBackgroundFromWindow()`가 호출되고 컴포지터의 서리 유리(frosted layer) 블러 효과가 완전히 제거되어 렌더링 지연 및 잔상을 방지한다.
- **실패 징후**: Opacity를 0%로 내려도 서리 유리 블러 사각형이 배경에 그대로 남아 있거나, Transparent 스타일 선택 시에도 Opacity 슬라이더가 노출된다.
- **근거**: `src/qml/settings/AppearancePage.qml:211-248`, `src/style/backgroundstyle.cpp:293-302`, `src/config/krema.kcfg:78-83`
- **우선순위**: P1
- **자동화**: `AUTO`

---

#### QA-SET-009: Tinted 스타일의 시스템 색상 및 커스텀 틴트 색상 변경
- **사용자 동작**: Background Style을 Tinted (2)로 설정한 상태에서 "Use system color" Switch(기본값: true)를 토글하고, 스위치를 끌 때 나타나는 "Tint color" 픽커(ColorDialog, 기본값: `#1e3a5f`)를 이용해 색상을 `#ff0000`(빨강)으로 변경한다.
- **기대 결과**:
  - "Use system color"가 켜져 있으면 시스템 헤더 색상이 배경색으로 지정되며 커스텀 틴트 색상 선택 위젯이 숨겨진다.
  - "Use system color"를 끄면 틴트 색상 미리보기 사각형과 픽커가 노출되며, 색상을 선택하면 독 패널 배경색이 즉시 빨간색 틴트로 변경된다.
- **실패 징후**: 스위치를 꺼도 ColorDialog 버튼이 나타나지 않거나, 커스텀 색상을 변경해도 독 패널 색상이 갱신되지 않는다.
- **근거**: `src/qml/settings/AppearancePage.qml:255-309`, `src/style/backgroundstyle.cpp:78-95`, `src/config/krema.kcfg:104-115`
- **우선순위**: P1
- **자동화**: `AUTO`

---

#### QA-SET-010: 배경 액센트 색상 사용 및 시스템 라이트/다크 테마 변경 추종
- **사용자 동작**: Appearance > Background에서 "Use accent color" Switch(기본값: false, PanelInherit/Acrylic 또는 Tinted+UseSystemColor 모드에서만 노출)를 켠다. 이후 시스템 테마를 라이트 테마에서 다크 테마로 변경하거나 액센트 컬러를 바꾼다.
- **기대 결과**:
  - "Use accent color"를 켜면 KColorScheme의 Header 색상 대신 Selection(강조) 색상이 배경색으로 바뀐다.
  - 시스템의 라이트/다크 색상 스키마가 변경되면 독 패널의 배경색이 변경된 시스템 스키마 Palette/KColorScheme 색상을 자동으로 추종하여 즉시 다시 그려진다.
- **실패 징후**: 시스템 색상이 바뀌어도 독 패널이 이전 색상을 유지하거나, Accent color 토글 시 배경색이 변경되지 않는다.
- **근거**: `src/qml/settings/AppearancePage.qml:311-325`, `src/style/backgroundstyle.cpp:63-76`, `src/config/krema.kcfg:92-97`
- **우선순위**: P1
- **자동화**: `MANUAL: Plasma 시스템 색상 스키마 동적 변경은 실제 Plasma 세션 필요`

---

#### QA-SET-011: 모서리 둥글기 및 플로팅 모드 설정
- **사용자 동작**: Appearance > Background에서 "Corner radius" SpinBox(범위: 0~24, 기본값: 12)와 "Floating" Switch(기본값: true)를 설정한다.
- **기대 결과**:
  - Corner radius를 0으로 내리면 독 패널 모서리가 직각으로 바뀌고, 24로 올리면 곡률이 커진다. 컴포지터 블러 영역(QPainterPath roundedRect)도 이 곡률에 맞춰 함께 재계산된다.
  - Floating 스위치를 끄면 독 패널이 화면 가장자리에 밀착(마진 0)되며, 켜면 떠 있는 마진(`s_floatingMargin = 8px`)이 추가된다.
- **실패 징후**: Corner radius를 올려도 블러 영역이 직각으로 남아 마스크 외부에 블러 잔상이 남거나, Floating 토글 시 패널 위치 계산이 틀어진다.
- **근거**: `src/qml/settings/AppearancePage.qml:329-343`, `src/config/krema.kcfg:53-63`, `src/shell/dockview.cpp:120-123`, `src/shell/dockview.cpp:310-318`
- **우선순위**: P1
- **자동화**: `AUTO`

---

#### QA-SET-012: 독 그림자 활성화, 색상 및 투명도(강도) 조절
- **사용자 동작**: Appearance > Shadow에서 "Enable shadow" Switch(기본값: false)를 켜고, 서브 항목으로 노출되는 "Shadow intensity" Slider(범위: 0.0~1.0, 단계: 0.05, 기본값: 0.3) 및 "Shadow color" ColorDialog(기본값: `#80000000`)를 변경한다.
- **기대 결과**:
  - Shadow를 켜면 독 패널 뒤로 `outer_shadow.frag.qsb` ShaderEffect 항목이 visible=true가 된다.
  - Shadow intensity를 1.0(100%)으로 올리면 그림자가 진해지며, Shadow color를 변경하면 그림자 색상이 해당 ARGB 색상으로 실시간 렌더링된다.
  - 그림자는 독 패널 본체 내부 영역에는 칠해지지 않고(SDF 패널 마스킹), 패널 외곽으로만 퍼진다.
- **실패 징후**: 그림자를 켰을 때 패널 전면 전체가 검게 오버레이되거나, Intensity를 바꿔도 그림자 진하기에 변화가 없다.
- **근거**: `src/qml/settings/AppearancePage.qml:349-361`, `src/qml/settings/AppearancePage.qml:593-655`, `src/config/krema.kcfg:117-122`, `src/config/krema.kcfg:148-160`, `src/qml/shaders/outer_shadow.frag:50-54`
- **우선순위**: P1
- **자동화**: `AUTO`

---

#### QA-SET-013: 그림자 투영 광원 위치(X/Y/Z), 반경 및 패널 고도 조절
- **사용자 동작**: Shadow 설정이 켜진 상태에서 "Light X" Slider(-300~300, 기본값: 0), "Light Y" Slider(-300~300, 기본값: -150), "Light Z (height)" Slider(100~2000, 기본값: 500), "Light radius" Slider(0.5~20.0, 기본값: 5.0), "Elevation" Slider(1~50, 기본값: 15)를 움직인다.
- **기대 결과**:
  - Light X/Y 값을 조절함에 따라 가상 광원 위치에 맞추어 그림자가 반대 방향으로 투영된다.
  - Light Z(높이) 및 Elevation(고도)을 변경하면 광선 추적 투영 식($t = (L_z - \text{elevation}) / L_z$)에 의해 그림자의 빗겨남 및 오프셋 크기가 조절된다.
  - Light radius(광원 크기)를 키우면 가우시안 sigma 오차가 커지면서 그림자 경계가 보슬보슬하게 부드러워진다.
- **실패 징후**: 광원 좌표 조절 시 그림자가 갑자기 사라지거나, 패널 서피스 마진(margin)을 넘어 그림자가 픽셀 단위로 기하학적으로 잘린다.
- **근거**: `src/qml/settings/AppearancePage.qml:367-590`, `src/qml/shaders/outer_shadow.frag:28-55`, `src/config/krema.kcfg:124-146`, `src/config/krema.kcfg:162-167`, `src/qml/main.qml:605-618`
- **우선순위**: P2
- **자동화**: `AUTO`

---

#### QA-SET-014: 표시 모드(Always / AutoHide / Dodge) 및 Dodge 옵션
- **사용자 동작**: Behavior > Behavior에서 "Visibility mode" ComboBox를 Always visible (0), Auto hide (1), Dodge windows (2)로 변경하고, Dodge 선택 시 나타나는 "Only dodge active window" Switch(기본값: false)를 설정한다.
- **기대 결과**:
  - Always visible: 독이 항상 화면에 고정된다.
  - Auto hide: 마우스가 독 영역을 벗어나면 독이 패널 밖으로 슬라이드/페이드 숨김 처리된다.
  - Dodge windows: 창이 독 영역을 침범할 때 독이 숨겨진다. "Only dodge active window"를 켜면 활성화된(focused) 창이 겹칠 때만 숨겨지고, 비활성 창이 겹쳐 있을 때는 숨겨지지 않는다.
- **실패 징후**: Visibility mode를 변경해도 독 숨김 동작 규칙이 변경되지 않거나, DodgeActiveOnly 옵션 토글이 동작하지 않는다.
- **근거**: `src/qml/settings/BehaviorPage.qml:17-43`, `src/config/krema.kcfg:65-76`, `src/shell/dockshell.cpp:153-157`
- **우선순위**: P0
- **자동화**: `FIXTURE: 토플레벨 창 1개 이상 띄우고 활성화/비활성화 전환`

---

#### QA-SET-015: 독 화면 가장자리 위치 변경 (Top / Bottom / Left / Right)
- **사용자 동작**: Behavior > Behavior에서 "Screen edge" ComboBox를 Top (0), Bottom (1), Left (2), Right (3)로 전환한다.
- **기대 결과**:
  - Bottom/Top 선택 시: 독이 가로 방향(Horizontal) 패널로 배치되며 아이콘들이 좌우로 배열된다.
  - Left/Right 선택 시: 독이 세로 방향(Vertical, `DockView.isVertical = true`) 패널로 변환되며, 패널 수치(width/height) 및 아이콘 배열 flow가 TopToBottom으로 전환된다.
  - Layer-shell 서피스 앵커 및 엣지 설정이 Wayland 컴포지터에 즉각 재전송된다.
- **실패 징후**: 세로 엣지(Left/Right) 선택 시 패널 레이아웃이 가로로 남아 아이콘이 겹치거나, 화면 밖으로 독이 삐져나간다.
- **근거**: `src/qml/settings/BehaviorPage.qml:47-60`, `src/config/krema.kcfg:78-83`, `src/shell/dockview.cpp:131-143`, `src/qml/main.qml:737-738`
- **우선순위**: P0
- **자동화**: `AUTO`

---

#### QA-SET-016: 독 표시 및 숨김 지연 시간 설정
- **사용자 동작**: Visibility mode가 Auto hide 또는 Dodge인 상태에서 "Show delay (ms)" SpinBox(0~2000ms, step 50, 기본값: 200)와 "Hide delay (ms)" SpinBox(0~2000ms, step 50, 기본값: 400) 값을 조절한다. (Always visible 모드 시 타이머 위젯 숨김)
- **기대 결과**: 마우스를 가장자리에 가져다 대거나 창을 치웠을 때 Show delay 밀리초만큼 대기한 후 독이 나타나며, 마우스가 떠났을 때 Hide delay 밀리초 대기 후 독이 숨겨진다. 수치를 0ms로 설정하면 대기 없이 즉시 반응한다.
- **실패 징후**: 수치를 2000ms로 늘렸음에도 즉시 독이 숨어버리거나, Show/Hide delay 수치가 `DockVisibilityController`에 반영되지 않는다.
- **근거**: `src/qml/settings/BehaviorPage.qml:62-88`, `src/config/krema.kcfg:85-96`, `src/shell/dockshell.cpp:173-178`
- **우선순위**: P1
- **자동화**: `AUTO` — 다만 지연 **길이**는 검증하지 않는다. ShowDelay/HideDelay는 QTimer라 가상 시계 밖이고 몇 번째 프레임에 터지는지가 실행마다 흔들리므로, 시나리오는 "설정이 반영되어 최종 상태가 바뀐다"까지만 확인한다. 지연이 0으로 회귀해도 통과한다

---

#### QA-SET-017: 창 미리보기(Window Preview) 켜기/끄기, 썸네일 크기 및 지연 시간
- **사용자 동작**: 설정 > Window Preview에서 "Enable window preview" Switch(기본값: true)를 설정하고, 하위 항목인 "Thumbnail width (px)" SpinBox(120~320px, step 20, 기본값: 200), "Hover delay (ms)" SpinBox(0~2000ms, step 50, 기본값: 500), "Hide delay (ms)" SpinBox(0~1000ms, step 50, 기본값: 200)를 변경한다.
- **기대 결과**:
  - Preview가 켜져 있을 때 실행 중인 앱 아이콘 위에 마우스를 올리면 Hover delay 후 팝업 썸네일 창이 뜬다. 썸네일 가로 크기는 설정한 Thumbnail width 값으로 렌더링된다.
  - 마우스가 아이콘을 벗어나면 Hide delay 후 팝업이 사라진다.
  - Preview 스위치를 끄면 하위 SpinBox들이 비활성화(disabled)되며, 아이콘 호버 시 썸네일 팝업이 일체 뜨지 않는다.
- **실패 징후**: Preview 스위치를 꼈음에도 썸네일 팝업이 계속 나타나거나, 썸네일 크기 조절 시 팝업 서피스 크기가 달라지지 않는다.
- **근거**: `src/qml/settings/PreviewPage.qml:17-61`, `src/config/krema.kcfg:169-198`, `src/qml/main.qml:970-985`
- **우선순위**: P1
- **자동화**: `FIXTURE: 토플레벨 창 1개 및 PipeWire/PipeWire-Plumber 스텁`

---

#### QA-SET-018: 다중 모니터 표시 모드, 추적 트리거 및 화면 전환 효과
- **사용자 동작**: Behavior > Multi-Monitor에서 "Monitor mode" ComboBox(Primary monitor only (0), All monitors (1), Follow active screen (2), 기본값: PrimaryOnly)를 변경한다. Follow active screen 선택 시 나타나는 "Follow trigger" ComboBox(Mouse position (0), Active window focus (1), Composite (2), 기본값: Focus) 및 "Screen transition" ComboBox(Fade (0), Slide (1), Instant (2), 기본값: Fade)를 조절한다.
- **기대 결과**:
  - PrimaryOnly: 주 모니터에만 독 패널이 생성된다.
  - AllMonitors: 연결된 모든 디스플레이 각각에 DockShell/DockView 인스턴스가 생성된다.
  - Follow active screen: 마우스 위치 또는 활성 창 포커스 변경에 따라 독이 현재 작업 중인 모니터로 이동하며, Screen transition 설정에 따라 Fade/Slide/Instant 애니메이션과 함께 모니터를 이동한다.
- **실패 징후**: Follow active 모드에서 모니터 이동 시 애니메이션이 멈추거나, 다른 모니터로 독이 이동하지 않는다.
- **근거**: `src/qml/settings/BehaviorPage.qml:92-149`, `src/config/krema.kcfg:223-241`
- **우선순위**: P1
- **자동화**: `FIXTURE: 다중 virtual 출력 (kwin_wayland multi-output) 구성`

---

#### QA-SET-019: 가상 데스크톱 표시 모드 및 타 데스크톱 창 투명도 설정
- **사용자 동작**: Behavior > Virtual Desktops에서 "Display mode" ComboBox(Show all windows (0), Dim other desktops (1), Current desktop only (2), 기본값: DimOtherDesktops)를 설정한다. DimOtherDesktops 선택 시 노출되는 "Other desktop opacity" Slider(범위: 0.1~0.9, 단계: 0.05, 기본값: 0.4)를 조절한다.
- **기대 결과**:
  - Show all windows: 현재 데스크톱과 다른 데스크톱의 창 태스크 아이콘이 동일한 불투명도로 모두 표시된다.
  - Dim other desktops: 다른 가상 데스크톱에 존재하는 창의 독 아이콘/인디케이터가 Other desktop opacity 수치(예: 40%)만큼 옅게 dimmed 처리된다.
  - Current desktop only: 현재 가상 데스크톱에 속한 창 태스크 아이콘만 독에 노출된다.
- **실패 징후**: DimOtherDesktops 상태에서 슬라이더를 0.1로 낮춰도 다른 데스크톱의 창 아이콘 투명도에 변화가 없다.
- **근거**: `src/qml/settings/BehaviorPage.qml:153-205`, `src/config/krema.kcfg:243-256`
- **우선순위**: P1
- **자동화**: `FIXTURE: 가상 데스크톱 2개 이상 및 타 데스크톱 창 생성`

---

#### QA-SET-020: 고정 핀 런처 목록 기본값 및 보존
- **사용자 동작**: 독의 핀 런처 아이콘을 우클릭하여 "Unpin" 하거나 새로운 앱을 드래그앤드롭하여 "Pin to Dock" 처리한다.
- **기대 결과**: `src/config/krema.kcfg`에 정의된 `PinnedLaunchers` StringList 기본값(`applications:org.kde.dolphin.desktop`, `applications:org.kde.konsole.desktop`, `applications:org.kde.kate.desktop`, `applications:systemsettings.desktop`)에서 사용자의 고정/해제 조작에 따라 리스트 항목이 동적으로 업데이트되고 저장된다.
- **실패 징후**: 핀 고정/해제 후 재시작 시 기본 4개 핀 런처로 강제 리셋된다.
- **근거**: `src/config/krema.kcfg:258-261`, `src/shell/dockshell.cpp:50-100`
- **우선순위**: P1
- **자동화**: `FIXTURE: kremarc 파일 조작 및 TasksModel 핀 변경`

---

#### QA-SET-021: 설정 창 크기 조절, 스크롤 및 키보드 조작성
- **사용자 동작**: 설정 창을 띄운 후 창 모서리를 드래그하여 크기를 최소화하거나 확대한다. Tab / Shift+Tab / 화살표 키 / Enter / Space 키를 사용하여 사이드바 모듈 변경 및 설정 위젯 간 포커스를 이동하고 값을 변경한다.
- **기대 결과**: 설정 창 크기를 줄여도 `FormCard` 및 컨트롤들이 잘리지 않고 스크롤바가 생기며 스크롤이 원활하다. 키보드만으로 Appearance, Behavior, Preview, About 모듈 간 이동이 가능하며, SpinBox, Slider, ComboBox, Switch 등의 컨트롤을 키보드로 조작할 수 있다.
- **실패 징후**: 창 크기를 줄였을 때 스크롤이 되지 않아 하단 설정 항목에 접근할 수 없거나, Tab 키 포커스가 갇힌다(focus trap).
- **근거**: `src/qml/SettingsDialog.qml:17-57`, `src/shell/settingswindow.cpp:100-130`
- **우선순위**: P2
- **자동화**: `MANUAL: 설정 창 리사이즈·스크롤 조작 액션이 없다. 창 크기 변경은 컴포지터 측 동작이라 하네스로 불가`

---

#### QA-SET-022: 한계치 설정(Minimum/Maximum) 시 독 패널 레이아웃 및 그림자 안정성
- **사용자 동작**: 모든 설정 수치를 최소 및 최대 극단값으로 동시에 설정한다:
  - IconSize: 24 (최소) 및 96 (최대)
  - IconSpacing: 0 (최소) 및 16 (최대)
  - MaxZoomFactor: 1.0 (최소) 및 2.0 (최대)
  - IconScale: 0.5 (최소) 및 1.0 (최대)
  - CornerRadius: 0 (최소) 및 24 (최대)
  - ShadowLightX/Y: -300 / 300, LightZ: 100 / 2000, LightRadius: 0.5 / 20.0, Elevation: 1 / 50
- **기대 결과**:
  - IconSize 24 + Spacing 0: 초소형 독 상태에서도 아이콘 클릭 및 호버 히트테스팅이 정확하며, 레이아웃 계산 에러나 0 나누기 예외가 발생하지 않는다.
  - IconSize 96 + Spacing 16 + Zoom 2.0: 대형 독 상태에서도 화면 경계를 넘어서는 오버플로우 서피스 마진이 정상 확보되어 아이콘과 줌 애니메이션이 깨지거나 렌더링 영역 밖으로 잘리지 않는다.
  - 그림자 파라미터를 극단값으로 밀어도 셰이더의 UBO 버퍼 계산 시 NaN이나 무한대(Inf)가 발생하지 않고 자연스러운 렌더링 상태를 유지한다.
- **실패 징후**: IconSize 24 적용 시 독 패널 높이가 0이 되거나, IconSize 96 적용 시 아이콘이 패널 상단에서 잘리거나 셰이더 크래시가 발생한다.
- **근거**: `src/qml/main.qml:581-640`, `src/qml/settings/AppearancePage.qml:21-655`, `src/config/krema.kcfg:13-167`, `src/qml/shaders/outer_shadow.frag:20-55`
- **우선순위**: P2
- **자동화**: `AUTO`

## NOTI — 알림·주의 표시

#### QA-NOTI-001: 신규 알림 수신 시 배지 카운트 표시 및 증가
- **사용자 동작**: 미확인 알림을 발생시키는 앱(예: Slack, Konsole 등)에서 신규 알림 메시지 수신.
- **기대 결과**: 해당 앱의 독 아이콘 우측 상단에 빨간색/하이라이트 색상의 원형 배지가 나타나고, 수신된 unread 알림 개수가 정확히 1부터 시작하여 메시지 수에 따라 1씩 증가함.
- **실패 징후**: 알림을 받았음에도 배지가 나타나지 않거나, 수신된 알림 개수와 배지 숫자가 일치하지 않음.
- **근거**: `src/models/notificationtracker.cpp:301`, `src/qml/DockItem.qml:579`, `src/qml/DockItem.qml:603`
- **우선순위**: P0
- **자동화**: `FIXTURE: org.freedesktop.Notifications D-Bus 스텁 및 notify-send D-Bus 호출`

#### QA-NOTI-002: 99개 초과 알림 수신 시 배지 "99+" 약식 표시
- **사용자 동작**: 특정 앱에서 100개 이상의 미확인 알림이 누적 수신됨.
- **기대 결과**: 배지 숫자가 100 이상으로 늘어나 영역을 넘치지 않고 "99+" 텍스트로 제한되어 깨짐 없이 깔끔하게 표시됨.
- **실패 징후**: 숫자가 "100", "101" 등 그대로 표시되어 배지 범위를 벗어나거나 텍스트가 잘림.
- **근거**: `src/qml/DockItem.qml:603`
- **우선순위**: P2
- **자동화**: `FIXTURE: 동일 desktop-entry로 100개 이상의 Notify D-Bus 호출 주입`

#### QA-NOTI-003: 해당 앱 활성화(포커스) 시 알림 배지 자동 소멸
- **사용자 동작**: 미확인 알림 배지(예: 숫자 3)가 떠 있는 독 아이콘을 클릭하거나 창 포커스를 전환하여 해당 앱을 활성화함.
- **기대 결과**: 앱 창이 활성화(Active State)되는 순간 해당 앱에 누적되어 있던 fd.o 알림 배지 카운트가 `clearUnreadNotifications`에 의해 0으로 초기화되고 배지가 즉시 사라짐.
- **실패 징후**: 앱 창을 열어 알림을 확인했음에도 독 아이콘의 배지가 계속 남아 있음.
- **근거**: `src/qml/DockItem.qml:251`, `src/models/notificationtracker.cpp:165`
- **우선순위**: P0
- **자동화**: `FIXTURE: 알림이 존재하는 앱의 xdg_toplevel fixture window 생성 및 포커스 전환`

#### QA-NOTI-004: 알림 닫기/만료 시 배지 카운트 감소 및 0개 시 배지 숨김
- **사용자 동작**: 알림 팝업의 닫기 버튼을 누르거나 알림 유지 시간이 만료되어 `NotificationClosed` D-Bus 신호가 발송됨.
- **기대 결과**: 해당 알림 ID가 트래커에서 제거되어 배지 숫자가 1 감소하며, 남은 알림이 0개가 되면 배지가 자연스럽게 숨겨짐.
- **실패 징후**: 알림을 닫거나 만료되었음에도 독 아이콘의 배지 숫자가 감소하지 않고 이전 숫자로 유지됨.
- **근거**: `src/models/notificationtracker.cpp:346`, `src/models/notificationtracker.cpp:364`, `src/qml/DockItem.qml:579`
- **우선순위**: P1
- **자동화**: `FIXTURE: org.freedesktop.Notifications NotificationClosed 방송 신호 주입`

#### QA-NOTI-005: 앱 작업 진행률(Progress) 실시간 인디케이터 표시
- **사용자 동작**: 파일 다운로드나 연산 작업을 수행하는 앱(KTaskbarProgress / Unity Launcher API 지원)에서 작업 진행률을 업데이트함.
- **기대 결과**: 독 아이콘 하단에 가로 바 모양의 ProgressBar가 나타나고, 진행률(0~100%)에 비례하여 바의 길이가 실시간 애니메이션(`Kirigami.Units.shortDuration`)으로 부드럽게 늘어남.
- **실패 징후**: 진행률이 변경되어도 하단 진행 바가 나타나지 않거나 100% 진행 후에도 바가 사라지지 않음.
- **근거**: `src/qml/DockItem.qml:619`, `src/qml/DockItem.qml:628`
- **우선순위**: P1
- **자동화**: `FIXTURE: com.canonical.Unity.LauncherEntry D-Bus update 신호 주입 (progress, progressVisible)`

#### QA-NOTI-006: Attention 신호 수신 시 Attention 애니메이션 트리거
- **사용자 동작**: 메시지 앱 수신, 터미널 벨, 또는 앱에서 `IsDemandingAttention`(EWMH) / SNI `NeedsAttention` / SmartLauncher `urgent` 상태를 발생시킴.
- **기대 결과**: 해당 앱 아이콘에서 즉시 설정된 Attention 애니메이션이 개시되며, Attention 이벤트 수신 로그 및 시각 효과가 켜짐.
- **실패 징후**: 창에서 Attention 요청이 들어왔으나 독 아이콘에 아무런 시각적 변화가 없음.
- **근거**: `src/models/notificationtracker.cpp:151`, `src/qml/DockItem.qml:60`, `src/qml/DockItem.qml:78`
- **우선순위**: P0
- **자동화**: `FIXTURE: _NET_WM_STATE_DEMANDS_ATTENTION 속성이 설정된 xdg_toplevel 창`

#### QA-NOTI-007: Attention 애니메이션 - Bounce (타입 1) 수직 바운스 동작
- **사용자 동작**: AttentionAnimation 설정을 Bounce(1)로 지정하고 Attention 신호를 발생시킴.
- **기대 결과**: 독 방향에 따라 아이콘이 수직/수평으로 -14px 이동하며 튕김 (올라갈 때 300ms OutQuad, 내려올 때 300ms InBounce, 쉬는시간 800ms). Attention 상태가 유지되는 동안 무한 반복(`Animation.Infinite`).
- **실패 징후**: 튕기는 방향이 독 위치(상/하/좌/우)와 반대이거나 1회 후 튕김이 정지함.
- **근거**: `src/qml/DockItem.qml:639`, `src/qml/DockItem.qml:733`
- **우선순위**: P1
- **자동화**: `FIXTURE: IsDemandingAttention 또는 SNI urgent를 유발할 창·D-Bus 스텁. setting 액션으로는 유발 불가 (구현 시도 후 확인)`

#### QA-NOTI-008: Attention 애니메이션 - Wiggle (타입 2) 좌우 흔듦 동작
- **사용자 동작**: AttentionAnimation 설정을 Wiggle(2, 기본값)로 지정하고 Attention 신호를 발생시킴.
- **기대 결과**: 아이콘 중심을 기준으로 좌우 회전 각도가 5° -> -5° -> 3° -> -3° -> 1° -> 0° 순서로 빠르게 흔들린 후 (각 구간 80~160ms), 2000ms 동안 대기하는 패턴이 무한 반복됨.
- **실패 징후**: 회전 흔듦이 너무 빠르거나 느림, 또는 회전 후 2초 대기 없이 멈춰버림.
- **근거**: `src/config/krema.kcfg:174`, `src/qml/DockItem.qml:751`
- **우선순위**: P1
- **자동화**: `FIXTURE: 위와 동일`

#### QA-NOTI-009: Attention 애니메이션 - Pulse (타입 3) 및 Glow (타입 4) 스케일/글로우 동작
- **사용자 동작**: AttentionAnimation 설정을 Pulse(3) 또는 Glow(4)로 변경 후 Attention 발생.
- **기대 결과**:
  - Pulse(3): 아이콘 크기가 1.0 -> 1.15배 확대(600ms) 후 1.0배 축소(600ms), 400ms 대기 패턴으로 무한 반복됨.
  - Glow(4): 아이콘 주변으로 테마 하이라이트 색상의 MultiEffect 그림자 글로우 오파시티가 0.15 ~ 0.85 사이를 800ms 주기로 왕복하며 무한 불빛 펄스를 발생시킴.
- **실패 징후**: Pulse 시 아이콘 확장으로 인접 아이콘 레이아웃이 밀리거나, Glow 시 멀티이펙트 그림자가 나타나지 않음.
- **근거**: `src/qml/DockItem.qml:558`, `src/qml/DockItem.qml:765`
- **우선순위**: P1
- **자동화**: `FIXTURE: 위와 동일`

#### QA-NOTI-010: Attention 애니메이션 - DotColor (타입 5) 및 Blink (타입 6) 점 색상/투명도 점멸
- **사용자 동작**: AttentionAnimation 설정을 DotColor(5) 또는 Blink(6)로 설정 후 Attention 발생.
- **기대 결과**:
  - DotColor(5): 하단 인디케이터 점 색상이 즉시 `Kirigami.Theme.negativeTextColor`(빨간색 계열)로 바뀌고, 점 불투명도가 0.3 ~ 1.0 사이를 500ms 주기로 점멸 반복함.
  - Blink(6): 아이콘 전체 불투명도가 0.2 ~ 1.0 사이를 400ms 주기로 무한 반복하며 깜빡임.
- **실패 징후**: Blink 설정 시 다른 투명도 애니메이션과 충돌하여 아이콘이 계속 켜져 있거나 사라짐.
- **근거**: `src/qml/DockItem.qml:782`, `src/qml/DockItem.qml:797`, `src/qml/DockItem.qml:926`
- **우선순위**: P1
- **자동화**: `FIXTURE: 위와 동일`

#### QA-NOTI-011: AttentionAnimationDuration 타임아웃 및 창 활성화에 의한 애니메이션 중지
- **사용자 동작**: `AttentionAnimationDuration`을 5초로 설정한 상태에서 Attention 발생 후 방치하거나, 5초 이내에 해당 앱 창을 클릭하여 활성화함.
- **기대 결과**: 5초 타이머(`attentionTimer`)가 만료되거나 창이 활성화되면 Attention 애니메이션이 즉시 정지하고 아이콘의 position, angle, scale, opacity 속성이 원상복구(0, 1.0 등)됨.
- **실패 징후**: 5초가 지나거나 창을 활성화했음에도 애니메이션이 계속 실행되거나 아이콘 좌표/회전각이 삐뚤어진 상태로 멈춤.
- **근거**: `src/config/krema.kcfg:181`, `src/qml/DockItem.qml:84`, `src/qml/DockItem.qml:810`
- **우선순위**: P1
- **자동화**: `FIXTURE: attention 유발 fixture + AttentionAnimationDuration 만큼의 실시간 캡처`

#### QA-NOTI-012: 설정에서 AttentionAnimation 스타일 즉시 변경 반영
- **사용자 동작**: KConfig/설정 창에서 `AttentionAnimation` 값을 Wiggle(2)에서 Bounce(1) 또는 Blink(6)로 변경.
- **기대 결과**: 독 재시작 없이 현재 실행 중이거나 향후 발생하는 Attention 애니메이션에 변경된 애니메이션 스타일이 즉시 적용됨.
- **실패 징후**: 설정을 변경하였으나 기존 애니메이션 스타일로 동작함.
- **근거**: `src/config/krema.kcfg:174`, `src/qml/DockItem.qml:121`
- **우선순위**: P2
- **자동화**: `AUTO`

#### QA-NOTI-013: 앱 실행(Launch) 시 수직 바운스 애니메이션 동작 및 창 생성 종료
- **사용자 동작**: 핀 런처 아이콘을 클릭하여 앱을 실행시킴.
- **기대 결과**: 앱 실행이 확인되는 즉시 아이콘이 상하로 -8px 바운스 운동(올라갈 때 `longDuration * 0.85` OutQuad, 내려올 때 InQuad)을 시작하고, 신규 창이 생성되어 TasksModel에 반영되면 현재 바운스 주기를 마친 후 자연스럽게 정지함.
- **실패 징후**: 아이콘을 클릭해도 바운스가 안 일어나거나, 창이 생성되었는데도 바운스가 멈추지 않음.
- **근거**: `src/qml/DockItem.qml:145`, `src/qml/DockItem.qml:260`, `src/qml/DockItem.qml:673`
- **우선순위**: P0
- **자동화**: `FIXTURE: 클릭 후 xdg_toplevel fixture window 생성`

#### QA-NOTI-014: 이미 활성화된 앱 재실행 시 no-op 감지 및 바운스 즉시 중단
- **사용자 동작**: 이미 활성화되어 창이 열려있는 단일 인스턴스 앱의 런처 아이콘을 다시 클릭함.
- **기대 결과**: 클릭 직후 2초 동안 새 창 생성이 없고 창 개수가 변하지 않는 것을 `noOpDetectionTimer`(2000ms)가 감지하여 헛바운스를 즉시 멈추고 실행 상태를 해제함.
- **실패 징후**: 새 창이 뜨지 않는 앱임에도 클릭 후 바운스 애니메이션이 멈추지 않고 오랫동안 지속됨.
- **근거**: `src/qml/DockItem.qml:282`, `src/qml/DockItem.qml:310`
- **우선순위**: P2
- **자동화**: `FIXTURE: 활성화된 fixture window가 존재하는 상태에서 런처 클릭`

#### QA-NOTI-015: 창 생성이 무응답인 앱 실행 시 30초 타임아웃 강제 중단
- **사용자 동작**: 실행 시 창이 뜨기까지 매우 오래 걸리거나 응답이 없는 앱을 실행함.
- **기대 결과**: 바운스 애니메이션이 계속 실행되다가 30초 안전 타임아웃(`maxLaunchTimer`, 30000ms)이 만료되는 즉시 `_waitingForWindow = false`로 변경되며 바운스가 무한정 지속되지 않고 안전하게 종료됨.
- **실패 징후**: 앱이 창을 띄우지 않을 때 바운스가 몇 분 이상 무한히 계속됨.
- **근거**: `src/qml/DockItem.qml:294`
- **우선순위**: P2
- **자동화**: `FIXTURE: 창을 만들지 않는 실행 fixture. 30초 타임아웃은 기본 140프레임 캡처로 불가`

#### QA-NOTI-016: BadgeDisplayMode 설정 변경에 따른 배지 표시 제어 (Dot / Off)
- **사용자 동작**: 설정에서 `BadgeDisplayMode`를 Number(0, 기본값)에서 Dot(1) 또는 Off(2)로 변경함.
- **기대 결과**:
  - Dot(1): 배지가 숫자 대신 작은 원형 점(아이콘 크기의 18%)으로 변경되어 텍스트 없이 깔끔하게 표시됨.
  - Off(2): 미확인 알림이 존재하더라도 독 아이콘 우측 상단 배지가 완전히 숨겨짐(`visible: false`).
- **실패 징후**: Dot 모드에서도 숫자가 보이거나, Off 모드로 설정했음에도 배지가 계속 나타남.
- **근거**: `src/config/krema.kcfg:188`, `src/qml/DockItem.qml:579`, `src/qml/DockItem.qml:584`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-NOTI-017: D-Bus 알림 데몬 부재 시 독 안정성 및 기본 동작 유지
- **사용자 동작**: D-Bus 세션 버스에 `org.freedesktop.Notifications` 데몬(plasmashell 등)이 실행되지 않는 환경에서 krema 독을 실행하거나 사용함.
- **기대 결과**: NotificationTracker가 D-Bus 등록 실패 경고 로그만 남기고 에러로 크래시되지 않으며, 배지/알림 관련 기능을 제외한 런처/창 관리/애니메이션 등 독의 모든 기본 기능이 정상 작동함.
- **실패 징후**: 알림 데몬이 없거나 D-Bus 응답이 없을 때 독 전체가 정지(freeze)하거나 세그폴트로 종료됨.
- **근거**: `src/models/notificationtracker.cpp:218`, `src/models/notificationtracker.cpp:237`
- **우선순위**: P0
- **자동화**: `AUTO`

## KEY — 키보드·접근성

#### QA-KEY-001: Meta+F5 단축키로 독 포커스 진입 및 첫 번째 아이템 포커스
- **사용자 동작**: 독에 포커스가 없는 상태에서 전역 단축키 Meta+F5(D-Bus `focus-dock` 액션)를 누른다.
- **기대 결과**: 독이 활성화되며 첫 번째 독 아이템(`hoveredIndex = 0`)에 파란색 포커스 링(`border.color: Kirigami.Theme.focusColor`)이 표시되고, 해당 아이콘을 중심으로 파라볼릭 확대(Parabolic zoom)가 적용된다. 스크린 리더로 아이템 이름과 위치 정보("%1 of %2")가 발송된다.
- **실패 징후**: 독에 포커스가 들어가지 않거나, 포커스 링이 표시되지 않거나, 첫 아이템이 아닌 다른 곳에 포커스가 가거나 확대되지 않음.
- **근거**: `src/app/application.cpp:205`, `src/shell/dockshell.cpp:96`, `src/qml/main.qml:42`, `src/qml/DockItem.qml:395`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-KEY-002: 방향키(Left/Right)를 통한 수평 독 아이템 탐색
- **사용자 동작**: 수평 독(Edge=1 Bottom, 기본값)에서 키보드 포커스 진입 후 Right 방향키와 Left 방향키를 연달아 누른다.
- **기대 결과**: Right 키 입력 시 다음 아이템으로 포커스 링과 파라볼릭 zoom 중심점(`dockPanel.mouseX`)이 순차 이동하며, Left 키 입력 시 이전 아이템으로 복귀한다. 스크린 리더로 각 아이템 이름 및 `accessibleDescription`이 Polite로 발송된다.
- **실패 징후**: 방향키 입력에도 포커스 링 및 파라볼릭 zoom이 이동하지 않거나, 스크린 리더 안내가 누락됨.
- **근거**: `src/qml/main.qml:75`, `src/qml/main.qml:156`, `src/qml/main.qml:163`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-KEY-003: 세로 독(Left/Right Edge)에서 상하 방향키(Up/Down) 탐색
- **사용자 동작**: 설정(`krema.kcfg` `Edge` 기본값 1, 범위 0~3)에서 Edge를 2(Left) 또는 3(Right)로 변경한 후, Meta+F5로 독 진입 후 Down 방향키와 Up 방향키를 누른다.
- **기대 결과**: 세로 독에서는 Up 키가 이전(-1) 아이템, Down 키가 다음(+1) 아이템으로 포커스와 파라볼릭 zoom을 이동시킨다.
- **실패 징후**: 세로 독에서 Up/Down 키로 탐색이 되지 않거나, Left/Right 키 입력 시 잘못된 포커스 이동이 발생함.
- **근거**: `src/config/krema.kcfg:58`, `src/qml/main.qml:155`, `src/qml/main.qml:163`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-KEY-004: 독 아이템 목록의 양 끝 경계에서의 포커스 고정 (Clamp vs Wrap-around)
- **사용자 동작**: 첫 번째 아이템 포커스 상태에서 Left(또는 Up) 키를 누르거나, 마지막 아이템 포커스 상태에서 Right(또는 Down) 키를 누른다.
- **기대 결과**: 포커스가 끝에서 순환(wrap-around)하지 않고 첫 번째(index 0) 또는 마지막(index count-1) 아이템 위치에 고정(clamped)된다.
- **실패 징후**: 첫 번째 아이템에서 Left 키 입력 시 마지막 아이템으로 넘어가거나, 인덱스 범위를 벗어나 에러 발생.
- **근거**: `src/qml/main.qml:81`
- **우선순위**: P2
- **자동화**: `AUTO`

#### QA-KEY-005: Home 및 End 키를 통한 첫/마지막 아이템 이동 시도
- **사용자 동작**: 독 키보드 내비게이션 중 Home 키 또는 End 키를 누른다.
- **기대 결과**: Home 키 누름 시 맨 첫 번째 아이템(index 0), End 키 누름 시 맨 마지막 아이템(index count-1)으로 즉시 포커스 링과 zoom이 이동하거나, 미지원 키로 안전하게 무시된다.
- **실패 징후**: Home/End 키 입력 시 의도와 달리 예외가 발생하거나 포커스가 탈출/해제됨.
- **근거**: `src/qml/main.qml:114-192`
- **우선순위**: P2
- **자동화**: `AUTO`

#### QA-KEY-006: Enter / Space 키를 통한 포커스된 앱 활성화
- **사용자 동작**: 키보드로 탐색 중 특정 독 아이템에 포커스를 맞추고 Enter 키 또는 Space 키를 누른다.
- **기대 결과**: 해당 아이템의 앱이 실행되거나 창이 활성화(`DockActions.activate`)되고, 독의 키보드 내비게이션 모드가 종료(`endKeyboardNavigation()`)되어 포커스 링이 사라지며 layer-shell 키보드 상호작용이 해제된다.
- **실패 징후**: Enter/Space 입력 시 앱이 활성화되지 않거나, 활성화 후에도 독의 키보드 포커스 링이 계속 유지됨.
- **근거**: `src/qml/main.qml:168-175`, `src/shell/dockshell.cpp:108`, `src/shell/dockvisibilitycontroller.cpp:289`
- **우선순위**: P0
- **자동화**: `FIXTURE: 실행 중인 창 1개 이상`

#### QA-KEY-007: Esc 키를 통한 키보드 내비게이션 모드 탈출
- **사용자 동작**: 독 키보드 탐색 모드 중 Esc 키를 누른다.
- **기대 결과**: 키보드 내비게이션 모드가 즉시 종료(`endKeyboardNavigation()`)되고 포커스 링이 제거되며, `setKeyboardActive(false)`를 통해 layer-shell 키보드 상호작용이 해제되어 이전 활성 창으로 키보드 포커스가 복귀한다.
- **실패 징후**: Esc 입력 후에도 독 아이템에 포커스 링이 남아있거나 키보드 입력이 독에 계속 갇혀있음.
- **근거**: `src/qml/main.qml:176-183`, `src/qml/main.qml:64-72`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-KEY-008: 수직/수평 독에서 직교 방향 키를 통한 미리보기 팝업 열기
- **사용자 동작**: 실행 중인 창이 있는 앱 아이템에 키보드 포커스를 맞춘 상태에서 독 패널의 직교 방향 키(Bottom 독: Up 키, Left 독: Right 키)를 누른다.
- **기대 결과**: 해당 앱의 미리보기 팝업(`PreviewPopup`)이 표시되고, 미리보기 키보드 모드(`PreviewController.previewKeyboardActive = true`)로 전환되며 첫 번째 창 썸네일(`focusedThumbnailIndex = 0`)에 포커스 링이 표시된다. 스크린 리더로 "Preview for %1, %2 windows" 안내가 발송된다.
- **실패 징후**: 직교 방향 키 입력 시 미리보기가 열리지 않거나, 썸네일에 포커스가 가지 않음.
- **근거**: `src/qml/main.qml:158-161`, `src/qml/main.qml:184-203`, `src/qml/PreviewPopup.qml:24-28`
- **우선순위**: P0
- **자동화**: `FIXTURE: xdg_toplevel fixture 창 1개 이상`

#### QA-KEY-009: 미리보기 팝업 내부에서의 썸네일 키보드 탐색 및 스크린 리더 음성
- **사용자 동작**: 미리보기 팝업이 키보드로 열린 상태에서 Left / Right 방향키를 누른다.
- **기대 결과**: 썸네일 목록 간에 포커스 링(`previewFocusRing`)이 이동하며, 포커스가 이동할 때마다 스크린 리더로 창 제목과 상태("Active", "Minimized") 및 순서("%1 of %2")가 발송된다.
- **실패 징후**: 썸네일 간 포커스가 이동하지 않거나, 스크린 리더 안내에 창 제목/상태가 누락됨.
- **근거**: `src/qml/main.qml:121-131`, `src/qml/PreviewPopup.qml:248-251`, `src/qml/PreviewThumbnail.qml:64-73`
- **우선순위**: P1
- **자동화**: `FIXTURE: 그룹화된 창 2개 이상 (동일 앱 multiple windows)`

#### QA-KEY-010: 미리보기 팝업에서 Enter 키로 특정 창 선택 활성화 및 Delete 키로 창 닫기
- **사용자 동작**: 미리보기 팝업 탐색 중 원하는 썸네일에 포커스를 맞추고 Enter 키를 누르거나, Delete 키를 누른다.
- **기대 결과**: Enter 키 누름 시 해당 창이 최상단으로 활성화되고 미리보기와 독 키보드 모드가 모두 종료된다. Delete 키 누름 시 해당 창에 requestClose가 전달되어 창이 닫히며, 남은 창이 없으면 미리보기가 닫히고 독 키보드 모드로 돌아간다.
- **실패 징후**: Enter 키 누름에도 해당 창이 활성화되지 않거나, Delete 키 입력으로 창이 닫히지 않음.
- **근거**: `src/qml/main.qml:132-141`, `src/qml/PreviewThumbnail.qml:202-206`, `src/shell/previewcontroller.cpp:322-350`
- **우선순위**: P0
- **자동화**: `FIXTURE: xdg_toplevel fixture 창 2개 이상`

#### QA-KEY-011: 키보드 탐색 중 마우스 이동 시 키보드 모드 즉시 취소 및 모드 전환
- **사용자 동작**: Meta+F5로 독 키보드 내비게이션 진입 후 마우스를 독 패널 영역 위로 움직인다.
- **기대 결과**: 마우스 포지션 변경(`onPositionChanged`)이 감지되어 `keyboardNavigating`이 false로 변경되고, `DockVisibility.setKeyboardActive(false)`가 호출되어 포커스 링이 즉시 제거되고 일반 마우스 호버 모드로 전환된다.
- **실패 징후**: 마우스 이동 후에도 키보드 포커스 링이 계속 남아있거나, 마우스 호버 확대와 키보드 포커스 링이 충돌함.
- **근거**: `src/qml/main.qml:342`, `src/shell/dockvisibilitycontroller.cpp:280`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-KEY-012: 키보드 포커스 이동 시 파라볼릭 확대 적용 및 텍스트 툴팁 억제 확인
- **사용자 동작**: 키보드 모드에서 방향키로 아이템을 이동할 때 비주얼 확대와 툴팁 표시 여부를 관찰한다.
- **기대 결과**: 키보드 포커스가 맞춰진 아이템 중심으로 파라볼릭 zoom 확대가 마우스 호버 시와 동일하게 적용되나, 일반 마우스 호버 시 뜨는 텍스트 툴팁(`tooltipItem`)은 억제(`tooltipTimer.stop()`, `tooltipItem.show = false`)되어 시각적 가림을 방지한다.
- **실패 징후**: 키보드 포커스 이동 시 파라볼릭 확대가 작동하지 않거나, 화면상에 텍스트 툴팁 박스가 키보드 포커스 위치마다 불필요하게 떠오름.
- **근거**: `src/qml/main.qml:44-45`, `src/qml/main.qml:86-90`, `src/qml/main.qml:1052`
- **우선순위**: P1
- **자동화**: `AUTO`

#### QA-KEY-013: 스크린 리더용 Accessible 속성 누락 및 이름 빈 요소 존재 여부 점검
- **사용자 동작**: 독 내의 모든 요소(독 바, 독 아이템, 미리보기 팝업, 썸네일, 닫기 버튼)에 대한 AT-SPI 접근성 트리 속성을 검사한다.
- **기대 결과**: Root는 `ToolBar`("Krema Dock"), 아이템은 `Button`(`displayName`), 팝업은 `PopupMenu`("Preview for %1"), 썸네일은 `Button`(`title`), 닫기 버튼은 `Button`("Close %1") 역할을 가지며 `Accessible.name`이 빈 문자열(`""`)로 남는 요소가 없다.
- **실패 징후**: 이름이 설정되지 않은 `Button` 요소가 있거나, 역할(role)이 올바르지 않음.
- **근거**: `src/qml/main.qml:21-22`, `src/qml/DockItem.qml:17-18`, `src/qml/PreviewPopup.qml:216-218`, `src/qml/PreviewThumbnail.qml:25-31`, `src/qml/PreviewThumbnail.qml:195`
- **우선순위**: P0
- **자동화**: `FIXTURE: accerciser 또는 AT-SPI D-Bus 캡처 환경`

#### QA-KEY-014: AutoHide / DodgeVisibility 모드에서 키보드 탐색 중 독 숨김 방지 및 모드 종료 후 숨김 복귀
- **사용자 동작**: `krema.kcfg` 설정의 `VisibilityMode`를 1(AutoHide) 또는 2(Dodge)로 설정하고 창이 독 영역을 가린 상태에서 Meta+F5로 독 진입 후, 키보드 탐색을 진행하다가 Esc로 탈출한다.
- **기대 결과**: 키보드 탐색 중에는 `setKeyboardActive(true)`에 의해 오토하이드/Dodge 타이머가 중지되어 독이 타임아웃(기본 `HideDelay` 400ms) 지나도 계속 화면에 표시된다. Esc 키 입력으로 탐색 모드가 종료되면 타이머가 재개되어 독이 숨겨진다.
- **실패 징후**: 키보드로 입력하는 도중에 독이 오토하이드되어 화면 아래로 사라짐.
- **근거**: `src/config/krema.kcfg:39`, `src/config/krema.kcfg:68`, `src/shell/dockvisibilitycontroller.cpp:280-300`, `tests/e2e/scenarios/01-keyboard-nav.md:120`
- **우선순위**: P0
- **자동화**: `AUTO`

#### QA-KEY-015: 항목이 0개일 때 독 포커스 진입 시 안정성 및 에러 방지
- **사용자 동작**: 고정 핀 및 실행 중인 앱이 하나도 없는 상태(고정 핀 0개)에서 Meta+F5를 눌러 독 포커스 진입 및 키보드 방향키를 누른다.
- **기대 결과**: `dockRepeater.count === 0` 조건을 확인하여 `hoveredIndex`가 -1로 유지되거나 안전하게 예외 처리되며 crash나 인덱스 에러가 발생하지 않는다.
- **실패 징후**: 고정 핀이 0개일 때 Meta+F5 또는 방향키 입력 시 QML TypeError/ReferenceError 또는 Segmentation Fault 발생.
- **근거**: `src/qml/main.qml:46`, `src/qml/main.qml:77`
- **우선순위**: P2
- **자동화**: `FIXTURE: 고정 핀 0개 및 창 0개 상태`

#### QA-KEY-016: 미리보기 팝업 내부 키보드 탐색 중 Esc 키 입력 시 미리보기만 닫히고 독 키보드 포커스 유지가 가능한가
- **사용자 동작**: 미리보기 팝업이 키보드로 열린 상태에서 Esc 키 또는 독 방향 키(Bottom 독인 경우 Down 키)를 누른다.
- **기대 결과**: 미리보기 팝업 키보드 모드만 종료(`PreviewController.endPreviewKeyboardNav()`)되고 미리보기 팝업은 닫히며(또는 비활성화), 키보드 포커스는 해당 독 아이템에 그대로 유지된다.
- **실패 징후**: 미리보기에서 Esc 누름 시 독 키보드 포커스까지 완전히 해제되어 버리거나, 반대로 미리보기가 닫히지 않음.
- **근거**: `src/qml/main.qml:142-147`
- **우선순위**: P1
- **자동화**: `FIXTURE: xdg_toplevel fixture 창 1개 이상`
