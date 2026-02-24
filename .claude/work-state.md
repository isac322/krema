# Work State

> 세션 간 작업 상태 전달 파일. 각 세션 종료 시 갱신.
> CLAUDE.md에서 @-import로 로딩됨. 세션 시작 시 1회 로딩 (세션 중 수정해도 현재 세션에는 미반영).

## 현재 마일스톤

M7: 배경 스타일 + 시각 개선 (⬅️ 현재)

## 완료된 항목

- [x] M6: 윈도우 프리뷰 전체 완료
- [x] 아키텍처 리팩토링 Phase 0-6 전체 완료
- [x] 설정 리그레션 수정 (KConfigXT load() 누락 + 멀티 엔진 싱글톤 수정)
- [x] 문서 최신화 (phase-2/3 상태, CLAUDE.md KConfigXT, ROADMAP M6→M7)
- [x] M7 배경 스타일 구현 + 검증 완료
- [x] 접근성 대폭 개선 (5단계 전체 구현 + kwin-mcp 검증 완료)

## 접근성 구현 상세

5단계 접근성 구현 완료 + kwin-mcp 라이브 테스트 통과:
- Phase 1: AT-SPI Accessible 속성 (main.qml, DockItem, PreviewPopup, PreviewThumbnail)
- Phase 2: 키보드 내비게이션 (Meta+F5 진입, 좌우 화살표, Enter/Escape, Down→프리뷰)
- Phase 3: 시각적 포커스 링 + 바운스 애니메이션 Kirigami.Units.longDuration 전환
- Phase 4: Accessible.announce() 스크린 리더 공지 (QT_MIN_VERSION 6.8.0)
- Phase 5: 프리뷰 팝업 키보드 접근 (좌우 썸네일 이동, Enter 활성화, Delete 닫기)

문서: docs/kde/accessibility-guide.md, docs/kde/accessibility-audit.md

### 아키텍처 결정
- Layer-shell 서피스 간 포커스 전환은 Wayland 비동기 특성으로 신뢰할 수 없음
- 해결: 독 서피스가 KeyboardInteractivityExclusive를 항상 보유, 프리뷰 키보드 네비게이션도 독에서 처리
- PreviewController C++ 프로퍼티 (previewKeyboardActive, focusedThumbnailIndex)로 프리뷰 QML 바인딩

### C++ 변경
- DockPlatform: setKeyboardInteractivity(bool) 추가
- WaylandDockPlatform: Exclusive/None 토글 구현
- DockVisibilityController: setKeyboardActive() — 키보드 활성 시 숨김 방지
- DockShell: focusDock() — Meta+F5 글로벌 단축키 핸들러
- PreviewController: startPreviewKeyboardNav(), navigatePreviewThumbnail(delta), activatePreviewThumbnail(), closePreviewThumbnail() 등 키보드 네비게이션 메서드
- application.cpp: "focus-dock" KGlobalAccel 단축키 (Meta+F5) 등록
- dockview.cpp: KLocalization::setupLocalizedContext() 추가 (i18n() QML 지원)
- CMakeLists.txt: QT_MIN_VERSION 6.6.0 → 6.8.0

## E2E 테스트 인프라 (완료)

10개 상호작용 메커니즘 PoC 완료. 결과:
- 4개 완전 테스트 가능: 프리뷰 hover, 미들 클릭, 드래그 리오더, 설정 UI
- 5개 대안 필요 (B): 에지 트리거→D-Bus, 메뉴→스크린샷 좌표, 스크롤→스크린샷, 썸네일 클릭→좌표 변환, Close→키보드
- 1개 스크린샷 전용 (C): 툴팁

수정 완료:
- main.qml: dockMouseArea에 Accessible.ignored 추가
- AppearancePage.qml: 슬라이더 delegates에 Accessible.name 추가
- docs/kwin-mcp-issues.md: 9개 이슈 문서화 (D-01~D-09)
- tests/e2e/ 시나리오 7개 + README 갱신 (PoC 결과 반영)

## 다음 작업

- E2E + 접근성 수정 커밋
- M7 나머지 항목 (주의 요구 애니메이션, 아이콘 크기 정규화)
