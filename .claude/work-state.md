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

## 설정 리그레션 수정 상세

원인 2건:
1. `KremaSettings` 생성 후 `load()` 미호출 → 모든 값이 kcfg 기본값/0으로 남음
2. `qmlRegisterSingletonInstance`는 단일 QML 엔진만 지원 → 설정 창(별도 엔진)에서 DockSettings가 null

수정:
- `application.cpp`: `m_settings->load()` 추가
- 7개 `qmlRegisterSingletonInstance` → `qmlRegisterSingletonType` + 팩토리 콜백으로 교체
- `settingswindow.cpp`: `open()` → `open(QVariant())` 인자 수정
- `SettingsDialog.qml`: 불필요한 `Component.onCompleted` auto-open 제거

### 검증 결과

- 빌드: 100% 성공
- 유닛 테스트: 6/6 통과
- kwin-mcp 검증:
  - [x] 독 실행 시 kremarc 값(iconSize=56, zoom=1.3 등)으로 표시
  - [x] 설정 창 열기 → 현재 값이 kremarc와 일치
  - [x] 아이콘 크기 변경 → 독에 즉시 반영
  - [x] floating 토글 → 독에 즉시 반영
  - [x] 설정 변경 후 kremarc 파일에 저장 확인
  - [x] 싱글톤/undefined/invokeMethod 에러 0건

## 다음 작업

- 설정 리그레션 수정 + 리팩토링 전체 커밋
- M7 (배경 스타일 + 시각 개선) 시작
