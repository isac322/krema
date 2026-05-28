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

## 알려진 이슈

- AllScreens/FollowActive: 실제 듀얼 모니터에서 검증 필요
- QML fade/slide 전환 애니메이션 미구현 (현재 instant show/hide)
- Per-screen 설정 UI 페이지 미구현 (백엔드만 완료)
- PipeWire 글로벌 스트림 캡 미구현
- 현재 OBS 원격 DEB artifacts는 `libkirigami2-6`, `kpipewire` Depends 때문에 Debian 13/Ubuntu 25.04/25.10/26.04에 설치 불가; 수정된 `packaging/obs/debian.control`로 재빌드 필요. 임시 repack 검증에서는 Debian 13/Ubuntu 25.04/25.10/26.04 4개 전부 GUI smoke 통과
- 현재 OBS 원격 openSUSE RPM artifacts는 `kf6-kirigami-addons`/`kpipewire`/`plasma-workspace`/`layer-shell-qt` Requires가 Tumbleweed/Leap 16.0/Slowroll 패키지명과 불일치; 수정된 `packaging/obs/krema.spec`로 재빌드 필요. 기존 artifact + `krema-suse-compat-provides` 조합으로는 Tumbleweed/Leap 16.0 GUI smoke 통과
- Slowroll OBS artifact는 x86_64만 제공되며 현재 arm64 Docker 호스트는 binfmt에 `qemu-x86_64` 핸들러가 없어 amd64 컨테이너 실행이 `exec format error`로 막힘. Arch에서는 `yay -S qemu-user-static qemu-user-static-binfmt` 후 `systemctl restart systemd-binfmt`(또는 재로그인) 하면 `tests/docker/run-smoke.sh opensuse-slowroll <package-dir>`로 검증 가능. x86_64 컴팩트 RPM은 `/tmp/opencode/krema-fixed-artifacts/opensuse-slowroll/`에 준비됨 (`krema-0.7.0-18.1.x86_64.rpm`, `krema-suse-compat-provides-0.7.0-1.x86_64.rpm`)

## 다음 작업

- M8 전체 릴리즈 검토 (v0.8.0)
- M9: Widget System + System Tray
- 수정된 `packaging/obs/debian.control`/`packaging/obs/krema.spec`로 OBS artifact 재빌드 후 `tests/docker/run-smoke.sh <target> <package-dir>`로 Debian/Ubuntu/openSUSE 전체 GUI smoke 재실행 (현재는 임시 repack/compat-provides 경로로만 통과)
- Arch 호스트에 `qemu-user-static` + `qemu-user-static-binfmt` 설치 후 `tests/docker/run-smoke.sh opensuse-slowroll /tmp/opencode/krema-fixed-artifacts/opensuse-slowroll`로 Slowroll smoke 마지막 1개 검증
