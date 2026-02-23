# Krema 개발 로드맵

> KDE Plasma 6 전용 경량 독(Dock). Latte Dock의 정신적 후속작.
>
> **핵심 원칙:** Qt/Wayland 범용 프로그램이 아닌, **KDE Plasma에 종속된** 프로그램.
> KDE Frameworks, LibTaskManager, Kirigami 등 KDE/Plasma 공식 라이브러리를 적극 활용한다.

---

## Milestone 1: 기반 구축 ✅

- [x] 프로젝트 구조 (CMake + ECM + C++23)
- [x] Wayland 플랫폼 백엔드 (LayerShellQt — wlr-layer-shell)
- [x] DockPlatform 추상화 레이어 (Plasma Wayland 전용)
- [x] BackgroundStyle 추상화 (PanelInheritStyle — Plasma 패널 스타일 상속)
- [x] DockModel (LibTaskManager 래퍼 — TasksModel 기반)
- [x] QML 독 UI (패러볼릭 줌, 인디케이터 점, 툴팁)
- [x] 멀티 배포판 패키징 설정 (Arch, RPM, DEB, OBS)
- [x] TasksModel C++ 초기화 (classBegin/componentComplete)
- [x] TaskIconProvider (QIcon::fromTheme → QML Image)
- [x] 독 숨김/가시성 모드 4종 (AlwaysVisible, AlwaysHidden, DodgeWindows, SmartHide)
- [x] QML 슬라이드/페이드 애니메이션 (숨기기/표시)

---

## Milestone 2: 컨텍스트 메뉴 + 설정 영속화 ✅

독을 실제로 사용할 수 있게 만드는 첫 번째 단계.
우클릭 메뉴로 기본 조작을 제공하고, 설정을 파일에 저장하여 재시작 후에도 유지.

- [x] 우클릭 컨텍스트 메뉴 (C++ QMenu — KDE Breeze 네이티브)
  - [x] 핀/언핀 토글
  - [x] 닫기 (모든 윈도우)
  - [x] 새 인스턴스 실행
  - [x] 앱 이름 + 구분선 표시
- [x] KWin Wayland 프로토콜 접근 (.desktop 파일 + X-KDE-Wayland-Interfaces)
- [x] 마우스 휠: 앱 아이콘 호버 시 동일 앱 창 간 전환 (단일 창은 포커스)
- [x] KConfig 기반 설정 관리
  - [x] 핀 런처 목록 영속 저장/불러오기
  - [x] 가시성 모드 저장/복원
  - [x] 아이콘 크기, 줌 배율, 간격, 모서리 반경 저장/복원
  - [x] 독 위치 (Bottom/Top/Left/Right) 저장/복원
- [x] 시작 시 설정 파일에서 복원

---

## Milestone 3: 키보드 단축키 + 실행 애니메이션 ✅

전역 단축키로 독을 제어하고, 앱 실행 시 시각적 피드백 제공.
KDE Plasma의 글로벌 단축키 프레임워크를 활용.

- [x] 전역 단축키 등록 (KF6::KGlobalAccel)
  - [x] 독 표시/숨기기 토글 (Meta+`)
  - [x] Meta+숫자(1-9)로 N번째 앱 활성화
  - [x] Meta+Shift+숫자로 새 인스턴스 실행
- [x] 앱 실행 바운스 애니메이션 (런처 클릭 시 아이콘 튀어오름)
- [x] 스타트업 노티피케이션 연동 (TasksModel IsStartup role 활용)

---

## Milestone 4: 드래그 앤 드롭 ✅

독 아이템 재정렬과 파일 드래그 지원.
Pin/Unpin 후 독 내 위치가 즉시 변경되지 않는 문제는 드래그 재정렬 구현 시 함께 해결.

- [x] 독 아이템 순서 드래그 재정렬 (Pin 후 위치 이동 포함)
- [x] 파일 드래그: 앱 위에 드롭하면 해당 앱으로 열기
- [x] URL/데스크톱 파일 드래그로 런처 추가

---

## Milestone 5: 설정 UI + KDE Plasma 네이티브 통합 ✅

사용자가 GUI로 독을 커스터마이즈할 수 있는 설정 다이얼로그.
QML UI 전반에 Kirigami/KDE Plasma API를 도입하여 네이티브 통합 강화.

- [x] Kirigami 도입 (QML 하드코딩 → Kirigami.Units/Theme 교체)
- [x] KColorScheme 적용 (QPalette → KDE 색상 체계)
- [x] KDE 테마 색상 전면 반영
  - [x] 인디케이터 점 색상 (light/dark mode 대응)
  - [x] 툴팁 배경/텍스트 색상
  - [x] 독 배경 색상
  - [x] 모든 하드코딩된 색상을 KColorScheme/Kirigami.Theme으로 교체
- [x] ~~KIconThemes 적용~~ (불필요 — QIcon::fromTheme이 KDE Plasma에서 KIconThemes 플러그인을 통해 이미 올바르게 동작)
- [x] i18n 적용 (모든 사용자 표시 문자열에 KLocalizedString)
- [x] Kirigami 기반 설정 다이얼로그 (별도 윈도우)
  - [x] 아이콘 크기, 줌 배율, 간격 슬라이더
  - [x] 가시성 모드 선택 (라디오 버튼)
  - [x] 독 위치 선택
  - [x] 배경 불투명도 슬라이더
  - [ ] 배경 스타일 선택 → M7로 이동
- [x] 독 컨텍스트 메뉴에서 "설정..." 항목 추가

---

## Milestone 6: 윈도우 프리뷰 ✅

마우스 호버 시 윈도우 썸네일을 보여주는 프리뷰 팝업.

- [x] 마우스 호버 시 윈도우 썸네일 프리뷰 (PipeWire 기반)
- [x] 그룹화된 앱의 멀티 윈도우 프리뷰 목록
- [x] 프리뷰에서 클릭하여 윈도우 활성화
- [x] 프리뷰에서 닫기 버튼

---

## Milestone 7: 배경 스타일 + 시각 개선 ⬅️ 현재

시각적 완성도 향상.

- [x] 추가 배경 스타일
  - [x] 반투명 (Semi-transparent)
  - [x] 투명 (Transparent)
  - [x] Tinted (사용자 지정 색상 + 투명도)
  - [x] Acrylic/Frosted Glass (흐림 + 노이즈 텍스처)
  - [x] Mica (시스템 강조색 기반)
  - [x] Adaptive Opacity (창 겹침 시 불투명 전환)
- [ ] 주의 요구 애니메이션
  - [ ] 아이콘 흔들기 (IsDemandingAttention)
  - [ ] 배지 카운트 표시 (알림 수)
  - [ ] 강조 글로우 효과
- [ ] 아이콘 크기 정규화 옵션
  - [ ] 아이콘 내부 패딩 자동 감지 및 스케일링 (기본값: 활성)
  - [ ] 원본 크기 그대로 사용 옵션

---

## Milestone 8: 멀티 모니터 + 가상 데스크톱

다중 화면 환경 지원.

- [ ] 멀티 모니터
  - [ ] 화면별 독 인스턴스
  - [ ] 활성 화면 추적 (독이 활성 화면으로 이동)
  - [ ] 모니터별 독 설정
- [ ] 가상 데스크톱
  - [ ] 현재 가상 데스크톱 필터링
  - [ ] 모든 데스크톱 표시 옵션

---

## Milestone 9: 위젯 시스템 + 시스템 트레이

확장 가능한 위젯 아키텍처.

- [ ] DockWidget 모듈 시스템
  - [ ] 위젯 슬롯 시스템 (좌측/중앙/우측 영역)
  - [ ] 구분선 (Separator) 위젯
  - [ ] 스페이서 (Spacer) 위젯
- [ ] 시스템 트레이 통합
  - [ ] StatusNotifierItem 프로토콜 지원
  - [ ] 시스템 트레이 아이콘 독에 표시
  - [ ] 트레이 아이콘 클릭/우클릭 메뉴

---

## 미래 탐색 과제

- [ ] X11 플랫폼 백엔드 (NET_WM hints, strut, 입력 영역) — KDE Plasma 6는 Wayland 퍼스트이므로 우선순위 낮음
- [ ] Plasmoid 호스팅 가능성 조사
- [ ] Liquid Glass 배경 효과 (다중 레이어 굴절)
- [ ] 앱 메뉴 / 휴지통 위젯
- [ ] CI 파이프라인 (GitHub Actions)
- [ ] 포괄적 테스트 스위트 (Catch2)

---

## 기술 스택

| 항목 | 기술 |
|------|------|
| 언어 | C++23 |
| 데스크톱 환경 | **KDE Plasma 6** (Wayland 전용) |
| UI 프레임워크 | Qt 6.6+ / Qt Quick |
| KDE 프레임워크 | KDE Frameworks 6.0+ (Config, WindowSystem, I18n, CoreAddons, GlobalAccel) |
| KDE Plasma 라이브러리 | LibTaskManager, LayerShellQt, KPipeWire |
| KDE 추가 라이브러리 | Kirigami, Kirigami Addons, KColorScheme, KService, KCrash, KXmlGui |
| 렌더링 | QRhi (Vulkan/OpenGL 자동 선택) |
| 빌드 시스템 | CMake + ECM + Ninja |
| 패키징 | CPack (Arch/RPM/DEB), OBS |
| 테스트 | Catch2 3.x |
| 라이선스 | GPL-3.0-or-later |

## 최소 요구 사항

| 의존성 | 최소 버전 |
|---------|----------|
| **KDE Plasma** | **6.0.0** |
| Qt | 6.6.0 |
| KDE Frameworks | 6.0.0 |
| LayerShellQt | 6.0.0 |
| CMake | 3.22 |
| Wayland | 1.22 |
| C++ 컴파일러 | GCC 13+ / Clang 17+ |

## 아키텍처 개요

```
┌─────────────────────────────────────────┐
│           QML UI (Qt Quick)              │
│  main.qml, DockItem.qml, animations     │
│  Kirigami.Units/Theme, FormCard 설정 UI  │
├─────────────────────────────────────────┤
│           C++ Backend Layer              │
│  ┌──────────┐  ┌───────────────────┐    │
│  │ DockModel│  │DockVisibility     │    │
│  │(LibTask  │  │Controller         │    │
│  │ Manager) │  │(4 visibility modes)│    │
│  └──────────┘  └───────────────────┘    │
│  ┌──────────────┐ ┌────────────────┐    │
│  │ DockSettings │ │TaskIconProvider│    │
│  │ (KConfig)    │ │(QIcon→Pixmap)  │    │
│  └──────────────┘ └────────────────┘    │
│  ┌──────────────────────────────────┐   │
│  │PreviewController (KPipeWire)     │   │
│  │Layer-shell overlay + PipeWire    │   │
│  └──────────────────────────────────┘   │
├─────────────────────────────────────────┤
│     DockView (QQuickView)                │
│  ┌───────────────┐ ┌────────────────┐   │
│  │ BackgroundStyle│ │KWindowEffects  │   │
│  │(6 styles+     │ │(blur/contrast) │   │
│  │ adaptive)     │ │                │   │
│  └───────────────┘ └────────────────┘   │
├─────────────────────────────────────────┤
│   WaylandDockPlatform (LayerShellQt)     │
│   KDE Plasma 6 Wayland 전용              │
└─────────────────────────────────────────┘
```
