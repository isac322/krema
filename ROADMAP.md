# Krema 개발 로드맵

> KDE Plasma 6를 위한 경량 독(Dock). Latte Dock의 정신적 후속작.

---

## Milestone 1: 기반 구축 ✅

- [x] 프로젝트 구조 (CMake + ECM + C++23)
- [x] Wayland 플랫폼 백엔드 (LayerShellQt — wlr-layer-shell)
- [x] DockPlatform 추상화 레이어 (Wayland/X11 분리 설계)
- [x] BackgroundStyle 추상화 (PanelInheritStyle — Plasma 패널 스타일 상속)
- [x] DockModel (LibTaskManager 래퍼 — TasksModel 기반)
- [x] QML 독 UI (패러볼릭 줌, 인디케이터 점, 툴팁)
- [x] 멀티 배포판 패키징 설정 (Arch, RPM, DEB, OBS)
- [x] TasksModel C++ 초기화 (classBegin/componentComplete)
- [x] TaskIconProvider (QIcon::fromTheme → QML Image)
- [x] 독 숨김/가시성 모드 4종 (AlwaysVisible, AlwaysHidden, DodgeWindows, SmartHide)
- [x] QML 슬라이드/페이드 애니메이션 (숨기기/표시)

---

## Milestone 2: 컨텍스트 메뉴 + 설정 영속화 ⬅️ 다음

독을 실제로 사용할 수 있게 만드는 첫 번째 단계.
우클릭 메뉴로 기본 조작을 제공하고, 설정을 파일에 저장하여 재시작 후에도 유지.

- [ ] 우클릭 컨텍스트 메뉴 (QML Popup/Menu)
  - [ ] 핀/언핀 토글
  - [ ] 닫기 (모든 윈도우)
  - [ ] 새 인스턴스 실행
  - [ ] 앱 이름 + 구분선 표시
- [ ] KConfig 기반 설정 관리
  - [ ] 핀 런처 목록 영속 저장/불러오기
  - [ ] 가시성 모드 저장
  - [ ] 아이콘 크기, 줌 배율, 간격, 모서리 반경 저장
  - [ ] 독 위치 (Bottom/Top/Left/Right) 저장
- [ ] 시작 시 설정 파일에서 복원

---

## Milestone 3: 키보드 단축키 + 실행 애니메이션

전역 단축키로 독을 제어하고, 앱 실행 시 시각적 피드백 제공.

- [ ] 전역 단축키 등록 (KGlobalAccel)
  - [ ] 독 표시/숨기기 토글
  - [ ] Super+숫자(1-9)로 N번째 앱 활성화
  - [ ] Super+Shift+숫자로 새 인스턴스 실행
- [ ] 앱 실행 바운스 애니메이션 (런처 클릭 시 아이콘 튀어오름)
- [ ] 스타트업 노티피케이션 연동 (실행 중 표시)

---

## Milestone 4: 드래그 앤 드롭

독 아이템 재정렬과 파일 드래그 지원.

- [ ] 독 아이템 순서 드래그 재정렬
- [ ] 파일 드래그: 앱 위에 드롭하면 해당 앱으로 열기
- [ ] URL/데스크톱 파일 드래그로 런처 추가

---

## Milestone 5: 설정 UI

사용자가 GUI로 독을 커스터마이즈할 수 있는 설정 다이얼로그.

- [ ] QML 설정 다이얼로그 (별도 윈도우)
  - [ ] 아이콘 크기, 줌 배율, 간격 슬라이더
  - [ ] 가시성 모드 선택 (라디오 버튼)
  - [ ] 독 위치 선택
  - [ ] 배경 스타일 선택
- [ ] 독 컨텍스트 메뉴에서 "설정..." 항목 추가

---

## Milestone 6: 윈도우 프리뷰

마우스 호버 시 윈도우 썸네일을 보여주는 프리뷰 팝업.

- [ ] 마우스 호버 시 윈도우 썸네일 프리뷰 (PipeWire 기반)
- [ ] 그룹화된 앱의 멀티 윈도우 프리뷰 목록
- [ ] 프리뷰에서 클릭하여 윈도우 활성화
- [ ] 프리뷰에서 닫기 버튼

---

## Milestone 7: 배경 스타일 + 주의 애니메이션

시각적 완성도 향상.

- [ ] 추가 배경 스타일
  - [ ] 반투명 (Semi-transparent)
  - [ ] 투명 (Transparent)
  - [ ] Tinted (사용자 지정 색상 + 투명도)
  - [ ] Acrylic/Frosted Glass (흐림 + 노이즈 텍스처)
- [ ] 주의 요구 애니메이션
  - [ ] 아이콘 흔들기 (IsDemandingAttention)
  - [ ] 배지 카운트 표시 (알림 수)
  - [ ] 강조 글로우 효과

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

## Milestone 9: X11 플랫폼 백엔드

X11 세션에서도 독을 사용할 수 있게.

- [ ] X11 DockPlatform 구현 (NET_WM hints)
- [ ] X11 스트럿(strut) 기반 공간 예약
- [ ] X11 입력 영역 관리

---

## Milestone 10: 위젯 시스템 + 시스템 트레이

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
| UI 프레임워크 | Qt 6.6+ / Qt Quick |
| KDE 프레임워크 | KDE Frameworks 6.0+ |
| 윈도우 관리 | LibTaskManager (Plasma) |
| Wayland | LayerShellQt 6.0+ (wlr-layer-shell) |
| 렌더링 | QRhi (Vulkan/OpenGL 자동 선택) |
| 빌드 시스템 | CMake + ECM + Ninja |
| 패키징 | CPack (Arch/RPM/DEB), OBS |
| 테스트 | Catch2 3.x |
| 라이선스 | GPL-3.0-or-later |

## 최소 요구 사항

| 의존성 | 최소 버전 |
|---------|----------|
| Qt | 6.6.0 |
| KDE Frameworks | 6.0.0 |
| LayerShellQt | 6.0.0 |
| CMake | 3.22 |
| Wayland | 1.22 |
| C++ 컴파일러 | GCC 13+ / Clang 17+ |

## 아키텍처 개요

```
┌─────────────────────────────────────────┐
│                QML UI                    │
│  (main.qml, DockItem.qml, animations)   │
├─────────────────────────────────────────┤
│           C++ Backend Layer              │
│  ┌──────────┐  ┌───────────────────┐    │
│  │ DockModel│  │DockVisibility     │    │
│  │(TasksModel│  │Controller         │    │
│  │ wrapper) │  │(4 visibility modes)│    │
│  └──────────┘  └───────────────────┘    │
├─────────────────────────────────────────┤
│         DockView (QQuickView)            │
│  ┌───────────────┐ ┌────────────────┐   │
│  │ BackgroundStyle│ │TaskIconProvider│   │
│  │(PanelInherit)  │ │(QIcon→Pixmap)  │   │
│  └───────────────┘ └────────────────┘   │
├─────────────────────────────────────────┤
│     DockPlatform (Abstract Interface)    │
│  ┌──────────────────┐ ┌──────────────┐  │
│  │WaylandDockPlatform│ │X11DockPlatform│ │
│  │(LayerShellQt)     │ │(NET_WM hints) │ │
│  └──────────────────┘ └──────────────┘  │
└─────────────────────────────────────────┘
```
