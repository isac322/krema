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

## 아키텍처 리팩토링 (Phase 0-6 전체 완료)

- [x] Phase 0: Quick Wins (deprecated 수정, 앱 ID, 메타데이터)
- [x] Phase 1: QML 싱글톤 등록 (context property 제거)
- [x] Phase 2: KConfigXT 마이그레이션 (설정 보일러플레이트 제거)
- [x] Phase 3: DockView 속성 중복 제거 + applySettings 삭제
- [x] Phase 4: DockModel 분해 (DockActions + DockContextMenu 분리)
- [x] Phase 5: DockShell 추출 (Application 디커플링)
- [x] Phase 6: 유닛 테스트 인프라 (순수 함수 추출 + Catch2)

### 검증 결과

- 빌드: 100% 성공
- 유닛 테스트: 6/6 통과 (appstream, smoke, zoom, geometry, input region, screen rect)
- kwin-mcp: 기존 시나리오 통과 (독 표시, 줌, 클릭, 설정)

## 다음 작업

- 리팩토링 전체 커밋
- M6 완료 표시 후 M7 (배경 스타일 + 시각 개선) 시작
