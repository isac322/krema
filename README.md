# Krema

KDE Plasma 6용 Latte Dock 대체 독(dock).

## 빌드 의존성 (Arch Linux / Manjaro)

### 빌드 도구

| 패키지 | 용도 |
|--------|------|
| `cmake` | 빌드 시스템 |
| `extra-cmake-modules` | KDE CMake 모듈 (ECM) |
| `ninja` | CMake 백엔드 (Make 대비 빠른 증분 빌드) |
| `ccache` | 컴파일 캐시 (재빌드 속도 향상) |
| `just` | 명령 실행기 (npm scripts / Makefile 대체) |
| `gcc` | C++23 컴파일러 (>= 14) |

### Qt6

| 패키지 | 용도 |
|--------|------|
| `qt6-base` | Qt6 Core, GUI, Widgets, DBus, Network |
| `qt6-declarative` | QML 엔진 (Qt Quick) |
| `qt6-wayland` | Qt6 Wayland 플랫폼 플러그인 |

### KDE Frameworks 6

| 패키지 | 용도 |
|--------|------|
| `kwindowsystem` | 창 목록 추적, 활성 창 감지, 가상 데스크톱 |
| `kconfig` | 독 설정 저장/로드 |
| `kcoreaddons` | KDE 핵심 유틸리티 |
| `ki18n` | 다국어 지원 (i18n) |

### Wayland

| 패키지 | 용도 |
|--------|------|
| `layer-shell-qt` | Wayland layer-shell 프로토콜 (독 위치 고정) |
| `wayland` | Wayland 클라이언트 라이브러리 |

### 테스트

| 패키지 | 용도 |
|--------|------|
| `catch2` | C++ 단위 테스트 프레임워크 |

### 전체 설치 명령

```bash
sudo pacman -S --needed \
    cmake extra-cmake-modules ninja ccache just gcc \
    qt6-base qt6-declarative qt6-wayland \
    kwindowsystem kconfig kcoreaddons ki18n \
    layer-shell-qt wayland \
    catch2
```

### 전체 제거 명령

위 명령으로 **새로 설치된** 패키지만 제거하려면, 설치 전에 `pacman -Qqe` 목록을 저장해두고 diff로 비교:

```bash
# 설치 전 (설치 전에 실행)
pacman -Qqe > ~/krema-before-packages.txt

# 설치 후, 새로 추가된 패키지만 제거
comm -13 ~/krema-before-packages.txt <(pacman -Qqe | sort) | xargs sudo pacman -Rns
```

## 최소 버전 요구사항

| 의존성 | 최소 버전 |
|--------|----------|
| C++ | C++23 (GCC >= 14, Clang >= 18) |
| CMake | 3.22 |
| Qt6 | 6.6.0 |
| KDE Frameworks 6 | 6.0.0 |
| LayerShellQt | 6.0.0 |
| Wayland | 1.22 |

## 로컬 빌드

```bash
just configure    # cmake --preset dev
just build        # cmake --build --preset dev
just test         # ctest --preset dev
just run          # 실행
just format       # 코드 포맷
just clean        # 빌드 디렉토리 삭제
```

## 크로스 빌드 / 멀티 배포판 빌드 (OBS)

[Open Build Service (OBS)](https://build.opensuse.org/)를 사용하여 여러 배포판 및 아키텍처(x86_64, aarch64 등)의 패키지를 자동으로 빌드합니다.

### 지원 타겟

| 배포판 | 아키텍처 | 패키지 형식 |
|--------|---------|-----------|
| Arch Linux | x86_64, aarch64 | PKGBUILD (.pkg.tar.zst) |
| Fedora 40, 41, 42 | x86_64, aarch64 | RPM (.spec) |
| openSUSE Tumbleweed | x86_64, aarch64 | RPM (.spec) |
| Ubuntu 24.04, 24.10 | amd64, arm64 | DEB (debian.*) |
| Debian Trixie (13) | amd64, arm64 | DEB (debian.*) |

### 패키징 파일 위치

```
packaging/
├── obs/
│   ├── _service           # OBS 소스 자동 가져오기 설정
│   ├── krema.spec         # RPM 빌드 (Fedora, openSUSE)
│   ├── debian.control     # DEB 메타데이터 (Ubuntu, Debian)
│   └── debian.rules       # DEB 빌드 규칙
└── arch/
    └── PKGBUILD           # Arch Linux 패키지
```

### OBS 프로젝트 셋업

```bash
# osc 설치 (Arch)
sudo pacman -S osc

# 프로젝트 체크아웃 및 패키징 파일 업로드
osc checkout home:<username>/krema
cp packaging/obs/* home:<username>/krema/
cd home:<username>/krema
osc add *
osc commit -m "Initial packaging"
```

## 라이선스

GPL-3.0-or-later
