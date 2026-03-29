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
- [x] M8a-M8d: 가상 데스크톱, 멀티 모니터, Per-Screen 설정, Follow Active
- [x] 멀티 배포판 패키징 인프라 구축
  - COPR (Fedora 42/43/Rawhide): 6/6 빌드 성공
  - OBS (openSUSE Tumbleweed/Slowroll, Fedora, Debian 13, Ubuntu 25.04-26.04): 17/17 빌드 성공
  - Launchpad PPA (Ubuntu 25.10 questing, 26.04 resolute): Published
  - AUR: 기존 운영 유지
- [x] 크로스 배포판 빌드 호환성
  - LayerShellQt setDesiredSize 컴파일 시점 감지 (KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE)
  - QString QT_NO_CAST_FROM_ASCII 호환
  - openSUSE ninja/autostart 경로 조건부 처리
- [x] /release 스킬에 멀티 배포판 배포 파이프라인 추가
- [x] README 배포판 배지 + 설치 가이드 업데이트

## 알려진 이슈

- AllScreens/FollowActive: 실제 듀얼 모니터에서 검증 필요
- QML fade/slide 전환 애니메이션 미구현 (현재 instant show/hide)
- Per-screen 설정 UI 페이지 미구현 (백엔드만 완료)
- PipeWire 글로벌 스트림 캡 미구현
- SettingsWindow: findAndTrackConfigWindow() 폴링 루프를 ConfigurationView.configViewItem 직접 읽기로 리팩터링 필요 (docs/kde/settings-window-patterns.md 참조)

## 다음 작업

- SettingsWindow 리팩터링 (폴링 → configViewItem 직접 참조)
- M8 전체 릴리즈 검토 (v0.8.0)
- M9: Widget System + System Tray
