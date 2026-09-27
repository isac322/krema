# Work State

> 세션 간 작업 상태 전달 파일. 각 세션 종료 시 갱신.
> Manual handoff note — updated during releases; not auto-imported.

## 현재 마일스톤

M9 진행 예정 (Widget System + System Tray) — v0.9.0 릴리즈 완료

## 완료된 항목

- [x] M1-M7: 전체 완료
- [x] 접근성 5단계 구현 + 키보드 내비게이션
- [x] E2E 테스트 인프라 (10개 메커니즘 PoC)
- [x] v0.7.0 릴리즈
- [x] v0.8.0 릴리즈 (2026-09-28): M8 멀티 모니터/Per-Screen/Follow Active/가상 데스크톱 + 크로스 배포판 패키징 수정 포함
- [x] v0.9.0 릴리즈 (2026-09-28): Parabolic hover zoom(Zoom style 옵션), 세로/상단 독 hover·tooltip 수정, 설정 persist 수정, Unity LauncherEntry 배지 복구
- [x] M8a-M8d: 가상 데스크톱, 멀티 모니터, Per-Screen 설정, Follow Active
- [x] 멀티 배포판 패키징 인프라 구축
  - COPR (Fedora 42/43/Rawhide): 6/6 빌드 성공
  - OBS (openSUSE Tumbleweed/Slowroll/Leap 16.0, Fedora 42/43/44/Rawhide, Debian 13, Ubuntu 25.04-26.04): 21/21 builds succeeded
  - Launchpad PPA (Ubuntu 25.10 questing, 26.04 resolute): Published
  - AUR: 기존 운영 유지
- [x] 크로스 배포판 빌드 호환성
  - LayerShellQt setDesiredSize 컴파일 시점 감지 (KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE)
  - QString QT_NO_CAST_FROM_ASCII 호환
  - openSUSE ninja/autostart 경로 조건부 처리
- [x] /release 스킬에 멀티 배포판 배포 파이프라인 추가
- [x] README 배포판 배지 + 설치 가이드 업데이트
- [x] SettingsWindow 리팩터링: 폴링 루프 → configViewItem 직접 참조
- [x] macOS식 hover zoom — `ZoomStyle` 설정으로 2가지 스타일 제공: Parabolic (기본값: 확대된 아이콘이 이웃을 밀어내고 독 배경이 커지며, 포인터 아래 아이콘은 포인터 아래에 유지; 독 중앙에서는 배경 가장자리와 먼 아이콘이 정지, 끝 쪽으로 갈수록 그 끝 방향으로만 부드럽게 성장), In place (예전 동작, 제자리 확대·겹침 허용)
  - 계산: `krema::computeDockZoom` (src/utils/zoomcalculator.h), QML은 `DockView.zoomLayout()` 결과만 사용; Parabolic 출력은 커서의 직접 함수라 별도 스무딩 애니메이션 불필요
  - 이전 anchored 모델의 흔들림 제거: 슬롯마다 커서를 고정(pin)하던 방식 대신 배경 성장분을 양쪽으로 연속적으로 분배
  - 화면을 거의 채우는 독: 이동/성장은 남는 공간으로 제한, 시각 배율은 유지(초과분은 겹침)
- [x] Docker GUI runtime smoke infrastructure
  - 외부 OBS/osc 산출물을 distro별 컨테이너에 설치 후 host KWin virtual Wayland socket에 연결해 실행
  - 대상: openSUSE Tumbleweed/Slowroll/Leap 16.0, Fedora 42/43/44/Rawhide, Debian 13, Ubuntu 25.04/25.10/26.04
- [x] OBS 원격 artifact 기반 Docker GUI smoke 검증 (10/11 통과)
  - `origin/master` rebase 후 OBS 21/21 succeeded artifact 재다운로드
  - Runtime images built (arm64): Fedora 42/43/44/Rawhide, openSUSE Tumbleweed/Leap 16.0, Debian 13, Ubuntu 25.04/25.10/26.04
  - 통과 (status 143 = timeout 후에도 프로세스 alive): openSUSE Tumbleweed, openSUSE Leap 16.0, Fedora 42, Fedora 43, Fedora 44, Fedora Rawhide, Debian 13, Ubuntu 25.04, Ubuntu 25.10, Ubuntu 26.04
  - 미검증: openSUSE Slowroll (아래 알려진 이슈 참조)
  - Debian/Ubuntu: OBS DEB Depends를 `qml6-module-org-kde-kirigami`/`qml6-module-org-kde-pipewire`로 수정 (packaging/obs/debian.control), 임시 repack DEB로 4개 타겟 smoke 통과
  - openSUSE: spec의 Requires를 Tumbleweed/Leap 패키지명으로 분리 (packaging/obs/krema.spec), 기존 artifact + `krema-suse-compat-provides` + `dbus-1-daemon` 포함 runtime image로 2개 타겟 smoke 통과

- [x] E2E 자동화 하니스 (tests/appium, tests/qml)
  - `tests/appium/run-e2e.sh`: unprivileged Docker 컨테이너 안에서 `kwin_wayland --virtual` + AT-SPI + PipeWire 세션을 띄우고 real input(fake-input)으로 실제 독을 구동. pytest 스위트 `test_01..07` + `test_smoke.py`가 `tests/e2e/scenarios/`의 48개 TC 전부 커버 (오라클: AT-SPI, KWin window list, kremarc, ScreenShot2 스크린샷, AT-SPI 이벤트). 멀티모니터는 `KREMA_E2E_OUTPUT_COUNT=2`. 캡처가 필요한 테스트는 DRM render node 필요 (`modprobe vgem`; macOS는 Lima VM 사용, OrbStack/Docker Desktop VM에는 vgem/vkms 없음)
  - `tests/qml/` (Tier 1, ctest label `qml`): 실제 QML 파일을 offscreen + mock C++ 백엔드로 검증
  - 문서: `tests/appium/README.md` (Local setup, env vars, Coverage matrix, known bugs), `tests/e2e/README.md` (자동화 반영 + kwin-mcp 한계 테이블 갱신), `docs/kde/lessons-learned.md` §11
  - Harness notes: 세션당 하나의 장수명 inputsynth가 KWin fake-input pointer device를 세션 전체에 등록해 둠 (`krema_e2e/input.py`의 `hold_pointer_capability`, `pytest_plugin.py`의 세션 fixture `_pointer_capability`). 호출마다 device를 만들면 KWin이 seat pointer capability를 잃고 Qt가 leave 없이 wl_pointer를 release → 느린 CI에서 stuck hover flake가 발생. `KREMA_E2E_DOCKER_ARGS`로 docker run 옵션 전달 가능 (예: `--cpus=2`로 CI 재현).

## 알려진 이슈

- AllScreens/FollowActive: 실제 듀얼 모니터에서 검증 필요
- QML fade/slide 전환 애니메이션 미구현 (현재 instant show/hide)
- 화면 폭이 독과 거의 같을 때 양 끝 아이콘은 확대 시 화면 밖으로 몇 px 나감 (기존 in-place 모드와 동일)
- Per-screen 설정 UI 페이지 미구현 (백엔드만 완료)
- PipeWire 글로벌 스트림 캡 미구현
- 현재 OBS 원격 DEB artifacts는 `libkirigami2-6`, `kpipewire` Depends 때문에 Debian 13/Ubuntu 25.04/25.10/26.04에 설치 불가; 수정된 `packaging/obs/debian.control`로 재빌드 필요. 임시 repack 검증에서는 Debian 13/Ubuntu 25.04/25.10/26.04 4개 전부 GUI smoke 통과
- 현재 OBS 원격 openSUSE RPM artifacts는 `kf6-kirigami-addons`/`kpipewire`/`plasma-workspace`/`layer-shell-qt` Requires가 Tumbleweed/Leap 16.0/Slowroll 패키지명과 불일치; 수정된 `packaging/obs/krema.spec`로 재빌드 필요. 기존 artifact + `krema-suse-compat-provides` 조합으로는 Tumbleweed/Leap 16.0 GUI smoke 통과
- Slowroll OBS artifact는 x86_64만 제공되며 현재 arm64 Docker 호스트는 binfmt에 `qemu-x86_64` 핸들러가 없어 amd64 컨테이너 실행이 `exec format error`로 막힘. Arch에서는 `yay -S qemu-user-static qemu-user-static-binfmt` 후 `systemctl restart systemd-binfmt`(또는 재로그인) 하면 `tests/docker/run-smoke.sh opensuse-slowroll <package-dir>`로 검증 가능. x86_64 컴팩트 RPM은 `/tmp/opencode/krema-fixed-artifacts/opensuse-slowroll/`에 준비됨 (`krema-0.7.0-18.1.x86_64.rpm`, `krema-suse-compat-provides-0.7.0-1.x86_64.rpm`)
- E2E 스위트가 발견한 krema 버그 8건 (strict xfail로 고정; 상세: `tests/appium/README.md` "Known krema bugs"):
  - 프리뷰 input region이 수평 독에서 surface 전체 높이를 덮음 → Delete 후 pointer focus가 보이지 않는 영역에 재진입해 키보드 내비 종료 (KBD-007 pointer-at-centre)
  - 독 밖으로의 포인터 이동이 키보드 내비게이션을 종료하지 않음 (KBD-008)
  - Escape 후 이전 활성 윈도우로 포커스가 돌아가지 않아 SmartHide가 재숨김하지 않음 (KBD-009 smarthide)
  - 포인터 모션 없이 발생한 클릭이 정지 포인터 아래로 나타난/이동한 아이템에 도달하지 않음 — `updateHoveredItem()`가 `dockMouseArea.onPositionChanged`에서만 호출되고 `onClicked`가 stale `root.hoveredIndex` 사용 (`src/qml/main.qml` ~484-498, ~511-574) (MOUSE-001)
  - startup task가 없을 때 pinned launcher 클릭에 launch bounce 없음 (MOUSE-002)
  - Escape가 진행 중인 드래그를 취소하지 않음 (DND-004)
  - Follow active + Mouse trigger가 포인터의 화면으로 독을 이동하지 않음 (SET-008)
  - AlwaysVisible이 exclusive zone을 예약하지 않아 최대화 윈도우가 독 아래로 확장됨 (VIS-001)
- 제품 발견사항: 기본 Focus Dock 단축키 Meta+F5가 KWin 기본값 MoveMouseToFocus와 충돌 → stock KWin에서 실제 Meta+F5가 krema에 도달하지 않음

## 다음 작업

- M9: Widget System + System Tray
- M8b 잔여: PipeWire 글로벌 스트림 캡, 앱 목록 필터 정책 토글(all apps vs per-screen)
- 수정된 `packaging/obs/debian.control`/`packaging/obs/krema.spec`로 OBS artifact 재빌드 후 `tests/docker/run-smoke.sh <target> <package-dir>`로 Debian/Ubuntu/openSUSE 전체 GUI smoke 재실행 (현재는 임시 repack/compat-provides 경로로만 통과)
- Arch 호스트에 `qemu-user-static` + `qemu-user-static-binfmt` 설치 후 `tests/docker/run-smoke.sh opensuse-slowroll /tmp/opencode/krema-fixed-artifacts/opensuse-slowroll`로 Slowroll smoke 마지막 1개 검증
- E2E 스위트 strict xfail 버그 8건 수정 (위 알려진 이슈 참조) — 수정하면 XPASS로 드러나므로 마커 제거 필요
- `.github/workflows/e2e.yml`을 CI에서 실행해 e2e 스위트 검증
