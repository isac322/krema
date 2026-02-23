# Work State

> 세션 간 작업 상태 전달 파일. 각 세션 종료 시 갱신.
> CLAUDE.md에서 @-import로 로딩됨. 세션 시작 시 1회 로딩 (세션 중 수정해도 현재 세션에는 미반영).

## 현재 마일스톤

M6: 윈도우 프리뷰 (⬅️ 현재) — 안정화 + UX 개선 완료, 커밋 대기

## 완료된 항목

- [x] PipeWire 기반 윈도우 썸네일 프리뷰
- [x] 그룹 앱 멀티 윈도우 프리뷰 목록
- [x] 프리뷰 클릭 → 윈도우 활성화
- [x] 프리뷰 닫기 버튼
- [x] M6 안정화: 프리뷰 5개 버그 전면 재수정 + kwin-mcp 7개 시나리오 검증 완료
- [x] M6 버그 3건 추가 수정 (사용자 제보) + kwin-mcp 8개 시나리오 검증 완료
- [x] 프리뷰 줌 유지 (독→프리뷰 전환 시 아이콘 줌 상태 동결)
- [x] 프리뷰 설정 추가 (enabled, thumbnailSize, hoverDelay, hideDelay)
- [x] 설정 UI ConfigurationView 리팩토링 (5페이지 사이드바 네비게이션)
- [x] About Krema/About KDE → 설정 내 통합 (KAboutApplicationDialog 제거)
- [x] 닫기 버튼 아이콘 수정 (window-close-symbolic → window-close, layer.effect 제거)

## kwin-mcp 검증 결과 (7/7 통과)

| # | 시나리오 | 결과 |
|---|---------|------|
| 1 | 줌 유지: 독→프리뷰 이동 시 아이콘 줌 유지 | PASS |
| 2 | 줌 해제: 프리뷰 밖 이동 → 프리뷰 닫힘 + 줌 해제 | PASS |
| 3 | 설정 창: ConfigurationView 사이드바 네비게이션 | PASS |
| 4 | 프리뷰 설정: Window Preview 페이지 표시 | PASS |
| 5 | 리그레션: 좌/우 이탈 → 프리뷰 닫힘 | PASS |
| 6 | About Krema: 앱 정보 페이지 표시 | PASS |
| 7 | 리그레션: 프리뷰 클릭 → 윈도우 활성화 + 프리뷰 닫힘 | PASS |

## 이번 세션 변경 요약

### 변경 0: 닫기 버튼 아이콘 수정
- `window-close-symbolic` → `window-close` (Breeze 호환)
- `layer.effect: MultiEffect` 제거 (아이콘 투명화 방지)
- 반투명 원형 배경 Rectangle 복원

### 변경 1: 프리뷰 줌 유지
- `onExited`: previewController.visible 시 mouseX/zoomActive 유지
- `onVisibleChanged`: 프리뷰 닫힘 + 독에 마우스 없으면 줌 해제

### 변경 2: 설정 UI ConfigurationView 리팩토링
- SettingsDialog.qml → ConfigurationView 기반 (사이드바 네비게이션)
- 3개 페이지 분리: AppearancePage, BehaviorPage, PreviewPage
- About Krema / About KDE 모듈 추가
- settingswindow.cpp/h: ConfigWindow 추적 로직, show(defaultModule) 오버로드
- application.cpp: KAboutApplicationDialog 제거, aboutRequested → 설정 about 페이지

### 변경 3: 프리뷰 설정
- DockSettings: previewEnabled, previewThumbnailSize, previewHoverDelay, previewHideDelay
- main.qml: previewEnabled 체크, tooltipTimer.interval 바인딩
- PreviewThumbnail.qml: thumbnailWidth → dockSettings.previewThumbnailSize
- PreviewController: setHideDelay() 메서드 추가
- application.cpp: dockSettings를 dock engine에도 등록

## 다음 작업

- M6 변경사항 전체 커밋
- M6 완료 표시 후 M7 (배경 스타일 + 시각 개선) 시작
