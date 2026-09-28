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

---

## N. NotificationManager::Notifications가 외부 앱에서 항상 0행 (2026-02)

**증상:** `NotificationManager::Notifications` C++ 인스턴스 생성 후 `notify-send`를 보내도
`rowCount()` = 0, `rowsInserted` 시그널 미발생.

**근본 원인** (바이너리 분석으로 확인):
- `Notifications::componentComplete()` → `createNotificationsModel()` → 싱글톤 `NotificationsModel` 생성
- `NotificationsModel` 생성자에서 `connect(Server::self().notificationAdded, ...)` 연결
- `Server::self()`는 **프로세스 내** 서버 싱글톤 — `Server::init()` 없이는 알림 수신 불가
- 실제 `notify-send`는 plasmashell의 `Server`로 전달 → 크레마 프로세스의 `Server::self()`는 공백

**오해:** `classBegin()` + `componentComplete()` 수동 호출로 해결된다고 생각했으나 무관.
**진짜 문제:** 소스 모델 타입이 잘못됨 (`NotificationsModel` vs `WatchedNotificationsModel`).

**올바른 해결책:**
- **방법 1 (권장)**: 커스텀 D-Bus 감시자 직접 구현
  - `/org/freedesktop/Notifications` 경로에 D-Bus 객체 등록 (`registerObject`)
  - `org.kde.NotificationManager.RegisterWatcher()` 호출
  - plasmashell이 `Notify()` 슬롯을 직접 호출해 줌
  - `hints["desktop-entry"]`로 앱 식별
- **방법 2**: QML 엔진에서 `WatchedNotificationsModel` 인스턴스 생성

**상세 분석:** `docs/kde/notification-badges.md` 섹션 "CRITICAL BUG" 참조

**핵심 교훈:**
- KDE 라이브러리의 "proxy model" 클래스가 반드시 올바른 소스를 사용하는지 바이너리 수준에서 검증 필요
- `QQmlParserStatus::componentComplete()` 수동 호출 = 소스 모델 생성 트리거이지만, 어떤 소스인지는 별개 문제
- 외부 D-Bus 서비스의 알림을 수신하려면 D-Bus 감시자(watcher) 프로토콜 직접 구현이 가장 안전

## 7. Layer-shell 출력 고정: QWindow::setScreen 불충분 + Plasma primary ≠ Qt primary (2026-09, issue #18)

**증상:** 듀얼 출력 kwin --virtual에서 (a) "All Screens" 모드의 두 번째 독/프리뷰가 Virtual-1이 아닌 Virtual-0에 붙고, (b) PrimaryOnly 독이 Plasma primary가 아닌 첫 번째 wl_output에 생성됨.

**근본 원인 (2개):**
1. `QWindow::setScreen()`은 layer-shell surface에 무시됨 — QtWayland에서 surface가 map될 때 platform window가 primary `wl_output`을 다시 유도함. WAYLAND_DEBUG로 `get_layer_surface`가 `setScreen(Virtual-1)` 후에도 `wl_output#23`(Virtual-0)을 바인딩함을 확인. 올바른 핀은 `LayerShellQt::Window::setScreen()` — `QWaylandLayerSurface` 생성자가 `m_interface->screen()`(LSQt-level)을 먼저 읽고, 그 다음 `window->screen()`을 fallback으로 읽기 때문 (qwaylandlayersurface.cpp:29-46).
2. `QGuiApplication::primaryScreen()`은 registry가 최초로 알린 `wl_output`일 뿐이며, QtWayland에서 `primaryScreenChanged`는 발생하지 않음. KWin의 per-user 우선순위(=System Settings display page가 `kde_output_device_v2`로 편집하는 것)는 **`kde_output_order_v1`** 프로토콜로만 publish됨 — plasmashell도 panel 배치에 이것을 사용. upstream은 프로토콜을 "DE implementation detail"로 취급해 XML을 설치하지 않으므로 vendoring이 필요 (src/protocols/kde-output-order-v1.xml, MIT-CMU).

**해결책:**
- `DockPlatform::setScreen(QScreen*)` 추가 → `WaylandDockPlatform`에서 `window->setScreen()`(pre-show geometry용)과 `layerWindow->setScreen()`(surface pin)을 모두 호출. `DockView::initialize`에서 show() 전에 호출.
- `OutputOrderMonitor` 싱글턴으로 kde_output_order_v1을 소비하고 모든 `primaryScreen()` 사용처를 대체. order 변경 시 shell 재생성 (layer surface는 생성 시에만 출력 바인딩). 프로토콜 없으면 primaryScreen() 폴백.
- 모니터를 의도적으로 leak: Qt Wayland platform teardown 후에 wl_proxy destroy 시 크래시 방지.

**핵심 교훈:**
- 헤더/문서만으로 API 정확성을 판단하면 안 됨 — `setScreen`이 "존재"하더라도 wire-level 바인딩을 WAYLAND_DEBUG로 검증해야 함.
- "Plasma primary"는 Qt 개념이 아니라 compositor published 상태; kde_output_order_v1을 쓰는 게 Plasma-first 원칙에도 부합.

## 8. A QML handler must not destroy its own engine: app-wide UI owned by a per-screen object (2026-09, issue #16)

**Symptom:** Choosing another "Monitor mode" in Settings aborted Krema (SIGABRT, `Object ... destroyed while one of its QML signal handlers is in progress`, BehaviorPage.qml:102). Issue #16.

**Root cause:**
- `BehaviorPage.qml` `onActivated` writes `DockSettings.monitorMode`; `MonitorModeChanged` is connected directly to `MultiDockManager::setMonitorMode()`, which destroys every `DockShell` synchronously.
- Each `DockShell` owned a `SettingsWindow` (and its `QQmlApplicationEngine`), so the engine running the handler was deleted from inside the handler; Qt calls `qFatal()` in `QQmlData::destroyed()`.
- Same teardown: `PreviewController` never deleted its preview `QQuickView` (one leaked `krema-preview` surface per rebuilt shell), and it outlived `DockView`, so `PreviewPopup.qml` bindings re-ran against a null `DockView` (`Cannot read property 'edge' of null`).
- Second mechanism on the same flow: `createShellForScreen()` called `setScreen()` on a view left at (0,0). `QWindowPrivate::create()` re-derives the screen from the geometry, so every non-primary dock moved to the primary screen and emitted `screenChanged` while its platform window was being created; `DockView::handleScreenChanged()` then ran `hide()`+`show()`, re-entering `QWindow::create()`. The first `QWaylandWindow` leaked with a dangling `QWindow` pointer, and its next layer-surface configure crashed Krema (typically when Settings was opened after switching back from "All monitors").

**Fix:** One `SettingsWindow` owned by `MultiDockManager` (declared before `m_shells`), shells hold a non-owning pointer and take the interaction lock when created while the dialog is open. Settings QML calls the stateless `SettingsWindow.isStyleAvailable()` instead of a per-dock `DockView`. `DockShell` owns `PreviewController` (which owns its view) and destroys it before `DockView`.
Each view is also positioned on its target screen before creation, and `handleScreenChanged()` does nothing while no platform window exists.

**Rejected:** Queued connection / `deleteLater` for the rebuild: no abort, but the open dialog still disappears with the shell that owned it.

**Key lessons:**
- Application-wide UI (dialogs, their QML engines) must be owned at application scope, never by objects that settings changes recreate.
- Objects created while a ref-counted lock holder is already active must take the lock themselves; a transition signal will not arrive for them.
- Windows that share another view's QML engine must be destroyed before that view.
- `QWindow::setScreen()` alone does not pin a not-yet-created top-level window: move it into the screen's geometry too. Never recreate a surface (`hide()`/`show()`) from a `screenChanged` emitted during creation.
- Regression: `tests/integration/test_settings_lifecycle.cpp` (runs under `kwin_wayland --virtual`, see `tests/run-with-kwin.sh`).

## 9. LayerShellQt < 6.6: no `Window::setScreen`, and `Window::get()` already creates the platform window (2026-09)

**Symptom:** After #23, master no longer compiled on Debian 13 / Ubuntu 25.04 (LayerShellQt 6.3.4: `'class LayerShellQt::Window' has no member named 'setScreen'`). A compile-only fix still put every dock and preview on the first output (WAYLAND_DEBUG: `get_layer_surface(..., wl_output#20, 2, "krema-dock")`, where `wl_output#20` is Virtual-0, while Virtual-1 was the Plasma primary; "All monitors" stacked both docks on Virtual-0).

**Verified API history (layer-shell-qt tags):** `Window::setScreen`/`screen` since v6.6.0 (commit 430ad36); `setDesiredSize` since 6.4; `ScreenConfiguration` deprecated in 6.6. In 6.3–6.5 `QWaylandLayerSurface` binds `QWindow::screen()` (ScreenFromQWindow, the default) when the surface is created on show, and `Window::Window()` calls `window->create()`.

**Root causes (compat path, `KREMA_COMPAT_NO_LAYERSHELL_SCREEN`):**
1. `Window::get()` creates the platform window, and QtWayland re-derives `QWindow::screen()` from the still-empty geometry, so `DockView` read back the primary screen before pinning it.
2. On a created QtWayland toplevel, `QWindow::setPosition()` runs `screenForGeometry()` from the stale origin and moved the preview back to the old screen (QtWayland pins a created toplevel to its screen origin anyway, `fixedToplevelPositions`).

**Fix:** capture the assigned screen before `setupWindow()`, set `QWindow::screen` plus `ScreenFromQWindow`, and position only windows without a platform window.

**Key lessons:**
- A compile-only compat branch is not a fix: check the wire-level `get_layer_surface` output against the old library too.
- `-D_HAVE_LAYERSHELLQT_SET_SCREEN=OFF` exercises the compat code on a new distro, but only a real old LayerShellQt (Debian 13) reproduces the `create()` in `Window::get()`.

## 10. Destroy JavaScript-owned windows before their QML engine (2026-09, issue #27)

**Symptom:** On Debian 13 (Qt 6.8.2, KF 6.13), Krema segfaulted in `QQmlComponent::~QQmlComponent()` when it quit while the Settings window was still being built or was open. With the real binary, quitting 0 ms after opening Settings crashed 3 out of 3 times, 50 ms 2/3, 300 ms 1/3, and 3 s 0/3. Fedora 44 (Qt 6.11) never crashed, including with kirigami-addons 1.7.0 built from source.

**Cause:** `ConfigurationView.open()` creates `ConfigWindow` with `component.createObject(...)`. On every kirigami-addons version (1.7–1.13) the window has no QObject parent and has `QQmlEngine::JavaScriptOwnership`. Passing `root.window` in 1.12+ only sets `transientParent`. `~SettingsWindow` deleted the engine with the window alive, so the engine's teardown sweep (`QV4::QObjectWrapper::destroyObject`) destroyed the window tree, which crashes on the Debian 13 stack.

**Rule:** An owner of a `QQmlEngine` destroys every top-level window created in that engine, including JS-owned windows it did not construct, before deleting the engine. Keep `QPointer`s to them (closed windows with a pending `deleteLater()`/`destroy()` included), `disconnect()` them from the owner first so no close handling runs from the destructor, then `delete`. Deleting cancels pending deferred deletions, and `QtObject` properties such as `configViewItem` become null.

**Not fixed here:** Kirigami's `ScrollablePage` (`src/controls/ScrollablePage.qml:276-277` on master) logs `TypeError: Cannot read property 'flickable' of null` whenever a page is destroyed. This already happens on every normal Settings close. It is upstream.

## 11. The layer-shell namespace is the window type in KWin (2026-09, issue #16)

**Symptom:** Show Desktop (Meta+D) hid the Krema dock, while Plasma panels stayed.

**Cause:** The dock used the namespace `"krema-dock"`. KWin derives a layer surface's window type only from its namespace (`layershellv1window.cpp` `scopeToType`); anything outside its short list (`dock`, `desktop`, `notification`, `tooltip`, ...) becomes `WindowType::Normal`. `Workspace::setShowingDesktop()` hides every window whose `breaksShowingDesktop()` is true, which includes every normal window. Observed in KWin `--virtual`: `dock=false hiddenByShowDesktop=true` with `"krema-dock"`, `dock=true hiddenByShowDesktop=false` with `"dock"`.

**Fix:** The dock surface uses the namespace `"dock"`. `krema_showdesktop_tests` (tests/kwin) toggles Show Desktop over D-Bus and reads KWin's verdict through a test-only scripted effect.

**Key lessons:**
- The namespace is not a free-form label on KWin: pick it from KWin's type table for the surface's role.
- Scripted KWin effects only load when `animationsSupported()`; the software-rendered virtual backend needs `KWIN_EFFECTS_FORCE_ANIMATIONS=1`.

## 12. The overflow reserve must fit what opens on that side, per orientation (2026-09, found auditing PR #15)

**Symptom:** On left/right docks the launcher tooltip was cut off after ~28px. KWin `--virtual`, left dock: surface 108x768, tooltip at x=80 with width 171 ("System Settings Launcher a"), so 80..251 lay mostly outside the surface.

**Cause:** `surfaceHeight()` reserves `max(zoomOverflow, tooltipReserve)` in the axis perpendicular to the dock edge. `tooltipReserve` was 36px, enough for the tooltip's height above/below a horizontal panel, but vertical docks open the tooltip beside the panel, where its width (not height) must fit. The compositor clips everything outside the layer surface.

**Fix:** QML publishes `DockView.sideTooltipReserve` (gap + a font-derived max tooltip width of 15 gridUnits; longer names elide). Vertical docks use it as the tooltip reserve; horizontal docks keep 36px. The input region still follows the panel rect, so the wider transparent surface does not take clicks from windows beside the dock. `krema_tooltip_tests` (tests/kwin) checks the tooltip lies inside the surface on all four edges.

**Key lessons:**
- A perpendicular reserve sized for one orientation is wrong for the other: size it from what actually opens on that side.
- Growing the surface is safe only because the input region is set from the panel rect, never from the surface size.

## 13. A private Plasma QML module disappeared; Qt.createComponent failed silently (2026-09)

**Symptom:** On Fedora 44 (Plasma 6.7.5) app badges and progress bars sent over the Unity LauncherEntry API never showed on dock icons.

**Cause:** `DockItem.qml` created `SmartLauncherItem` with `Qt.createComponent("org.kde.plasma.private.taskmanager", "SmartLauncherItem")`. plasma-desktop 6.6 (commit `4bff79ad`, "Port to plasma_add_applet") compiled that module into the task manager applet plugin, so `qt6/qml/org/kde/plasma/private/taskmanager` no longer exists. The component was never Ready, and the `status` check skipped creation without a log line.

**Fix:** `LauncherEntryTracker` (C++) receives `com.canonical.Unity.LauncherEntry.Update` itself, following the upstream backend semantics; see `notification-badge-approaches.md`. `krema_launcher_entry_tests` sends real D-Bus signals and checks the dock item's badge and progress bar.

**Key lessons:**
- A private module is not a dependency Krema can keep: when the protocol underneath is public (here a D-Bus signal), implement the protocol.
- A silent fallback (`if (comp.status === Component.Ready)`) hides a missing feature. Cover each feature with a test that observes the visible result.

## 14. Running KWin + AT-SPI E2E in an unprivileged container (2026-09, tests/appium)

Verified while building the `tests/appium` harness (KWin 6.7.5, Qt 6.10, KF 6.30, Fedora 43 image):

- **`setcap -r kwin_wayland`**: Fedora's `kwin_wayland` ships `cap_sys_nice=ep`. An unprivileged container's bounding set lacks CAP_SYS_NICE, so `execve()` fails with EPERM before `main()` — drop the file capability in the image instead of running with `--cap-add`/`--privileged`.
- **KWin 6.7 needs a DRM device for OpenGL**: the virtual backend opens a DRM device via libdrm (vgem through its primary node is the exception); without `/dev/dri` it falls back to QPainter, and then ScreenShot2 answers every request with `Error.Cancelled` and `zkde_screencast` (KPipeWire thumbnails) is disabled — `ScreencastManager` requires `OpenGLCompositing`. Host fix: `modprobe vgem` (render node → `--virtual`) or `modprobe vkms` (KMS card → DRM backend). Docker-on-VM setups (OrbStack, Docker Desktop) have neither module; use a generic-kernel VM (Lima).
- **ScreenShot2 PNGs have alpha**: the dock surface's empty areas are alpha-0, so luminance/pixel comparisons must alpha-composite over a known background (the harness uses white) before diffing.
- **inputsynth press/release must be one W3C action chain**: selenium-webdriver-at-spi spawns a fresh `inputsynth` per `/actions` call and interpolates pointer moves from (0,0), so a press in one call and the release in the next loses the held button and teleports the pointer. `drag()` issues press→hold→move→release in a single chain; the chain must start by moving to the last known pointer position.
- **`Qt::Popup` windows (QMenu) are never in AT-SPI**: `QAccessibleApplication` builds children from `topLevelWindows()` and skips popups. Drive the menu via its KWin window (screen geometry) plus keyboard navigation over the known enabled-entry order (`krema_e2e.context_menu_entries`).
- **Meta+F5 collides with a KWin default**: KWin's `MoveMouseToFocus` ("Move Mouse to Focus") owns Meta+F5 in stock KWin, so krema's `focus-dock` never receives the real key press. To test real Meta+F5, unbind the KWin action first (`set_shortcut_keys("MoveMouseToFocus", [], component="kwin")`); `invokeShortcut("focus-dock")` over D-Bus always works. Product decision pending: pick a free default.

## 14. Running the AT-SPI suite on older distro stacks (2026-09, tests/distro)

Verified while building Tier 3 (`tests/distro`, `tests/appium` run on 12 distros' own KWin/Qt/KF):

- **vgem is a faux device since kernel 6.15**: KWin < 6.5 (debian-13, ubuntu-25.04/25.10, opensuse-leap-16.0) recognises vgem only on the platform bus — libdrm < 2.4.126 cannot enumerate faux devices at all, and KWin < 6.5 opens a faux vgem's render node where dumb-buffer allocation fails. Both end in QPainter compositing (no ScreenShot2, no screencast). Fix: build vgem out-of-tree registered as a platform device (`tests/appium/setup-vgem.sh`); a packaged `modprobe vgem` on kernel >= 6.15 is not enough.
- **KWin permission checks are also xdg-activation gating**: with `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1` KWin never reads `X-KDE-Wayland-Interfaces`, so no client is privileged. On KWin < 6.5 a non-privileged client can obtain an xdg-activation token only while it owns the active surface — a packaged dock could not raise its own Settings window from its context menu (`test_set001` failed on debian-13/ubuntu-25.04/25.10). Tier 3 therefore runs with checks on (`tools/run-permission-checks.patch`, `KWIN_WAYLAND_NO_PERMISSION_CHECKS=0`): the installed `com.bhyoo.krema.desktop` declares the interfaces, so krema is privileged — which doubles as a packaging-contract test. A source-tree krema has no matching desktop file and must keep the bypass.
- **Qt < 6.9 reports AT-SPI rects that ignore Item scale**: a zoomed DockItem's rect has the scaled top-left corner but the untransformed size, so `Rect.of(item).width` never changes under zoom. `painted_rect()` recovers the zoom factor from how far the corner rose (`EXTENTS_IGNORE_SCALE`); Qt >= 6.9 returns the scaled rect. Never compare scaled widths through the raw AT-SPI rect.
- **Qt 6.11 changed AT-SPI roles**: `QQuickPage` moved from PageTab (`page_tab`) to Pane (`panel`), and since every `QQuickControl` is accessible, `ApplicationWindow`'s content adds a `filler` level above the PageRow — and the page stack's own role still differs across distro Qt/Kirigami (`panel`, `layered_pane`). Match structure by children, not by role (`PAGE_ROLE`, `SETTINGS_STACK_XPATH` in `krema_e2e/krema.py`).
- **`XDG_MENU_PREFIX=plasma-` is required for KService on Debian**: KService resolves desktop files (dock item names, icons) through `${XDG_MENU_PREFIX}applications.menu`; startplasma exports it, but the harness's bare session does not, and Debian/Ubuntu ship only `plasma-applications.menu` (Fedora also has redhat-menus' `applications.menu`, which masked the issue there). The Tier 3 image exports it like a real Plasma session (`tests/distro/image/Dockerfile`).
