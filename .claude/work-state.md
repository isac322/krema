# Work State

> 세션 간 작업 상태 전달 파일. 각 세션 종료 시 갱신.
> CLAUDE.md에서 @-import로 로딩됨. 세션 시작 시 1회 로딩 (세션 중 수정해도 현재 세션에는 미반영).

## 현재 마일스톤

M8 완료 → M9 준비 (Widget System + System Tray)

## 완료된 항목

- [x] M1-M7: 전체 완료
- [x] 접근성 5단계 구현 + 키보드 내비게이션
- [x] E2E 테스트 인프라 (10개 메커니즘 PoC)
- [x] v0.7.0 릴리즈
- [x] M8a: 가상 데스크톱 필터링
  - QML singleton → context property 리팩터
  - 가상 데스크톱 필터링 (현재 데스크톱만 / 모든 데스크톱 + 투명도)
- [x] M8b: 멀티 모니터 코어
  - MultiDockManager (Application의 m_shell → m_dockManager)
  - All Screens / Primary Only 모드
  - 스크린 hot-plug 디바운스 (300ms)
- [x] M8c: Per-Screen 설정
  - ScreenSettings 2계층 KConfig (글로벌 기본 + 스크린별 오버라이드)
  - 9개 오버라이드 가능 속성 (iconSize, edge, visibilityMode 등)
- [x] M8d: Follow Active Mode
  - 3가지 트리거 (마우스/포커스/복합)
  - 디바운스 (300ms), show/hide 전환
  - 설정 UI (트리거 + 전환 효과)

## 알려진 이슈

- AllScreens/FollowActive: 실제 듀얼 모니터에서 검증 필요
- QML fade/slide 전환 애니메이션 미구현 (현재 instant show/hide)
- Per-screen 설정 UI 페이지 미구현 (백엔드만 완료)
- PipeWire 글로벌 스트림 캡 미구현
- 모든 변경사항 미커밋 상태

## 다음 작업

- M8 전체 커밋 + 릴리즈 검토
- M9: Widget System + System Tray
