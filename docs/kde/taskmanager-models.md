# KDE TaskManager 모델 계층

> 출처: `/usr/include/taskmanager/` 헤더 + plasma-workspace 6.5.5 소스

## 모델 개요

| 클래스 | 부모 클래스 | 역할 |
|--------|-----------|------|
| `AbstractTasksModel` | `QAbstractListModel` | 추상 베이스. 역할(Role) enum 정의 |
| `WaylandTasksModel` | `AbstractWindowTasksModel` | Wayland 창 목록 (플랫폼 고유) |
| `XWindowTasksModel` | `AbstractWindowTasksModel` | X11 창 목록 (플랫폼 고유) |
| `WindowTasksModel` | `QIdentityProxyModel` | 플랫폼 추상화. Wayland/X11 모델을 래핑 |
| `LauncherTasksModel` | `AbstractTasksModel` | .desktop 런처 목록 |
| `StartupTasksModel` | `AbstractTasksModel` | 시작 알림(startup notification) |
| `ConcatenateTasksProxyModel` | `QConcatenateTablesProxyModel` | 여러 소스 모델 결합 |
| `TaskFilterProxyModel` | `QSortFilterProxyModel` | 데스크탑/화면/활동/상태별 필터링 |
| `TaskGroupingProxyModel` | `QAbstractProxyModel` | 앱 기준 그룹핑 (트리 구조 생성) |
| `FlattenTaskGroupsProxyModel` | `KDescendantsProxyModel` | 그룹 트리를 플랫 리스트로 변환 |
| `TasksModel` | `QSortFilterProxyModel` | 최종 통합 모델 (정렬 + 추가 필터링) |

## 프록시 체인 (TasksModel 내부)

`TasksModel::Private::initModels()`에서 구성되는 실제 체인:

```
WindowTasksModel  ─┐
StartupTasksModel ─┼→ ConcatenateTasksProxyModel
LauncherTasksModel─┘          │
                              ▼
                    TaskFilterProxyModel
                     (데스크탑/화면/활동/상태 필터)
                              │
                              ▼
                   TaskGroupingProxyModel
                     (앱 기준 그룹핑 → 트리)
                              │
                              ▼
               ┌─── groupInline == true ────┐
               │                            │
               ▼                            ▼
  FlattenTaskGroupsProxyModel        (직접 연결)
    (트리 → 플랫 리스트)                    │
               │                            │
               └────────────┬───────────────┘
                            ▼
                       TasksModel
                 (QSortFilterProxyModel)
                 (최종 정렬 + filterAcceptsRow)
```

`TasksModel`의 소스 모델은 `updateGroupInline()`에서 동적으로 전환됨:
- `groupInline == true` + 그룹핑 활성: `FlattenTaskGroupsProxyModel`
- 그 외: `TaskGroupingProxyModel` 직접

## WindowTasksModel vs TasksModel 사용 기준

### TasksModel (독의 태스크바 표시용)
- 런처 + 윈도우 + 스타트업 통합
- 필터링/그룹핑/정렬 내장
- `filterByVirtualDesktop`, `filterByActivity` 등 필터 프로퍼티 제공
- 그룹핑 시 `IsGroupParent` 항목이 생기며, 대표 데이터는 자식 중 하나를 반영

### WindowTasksModel (가시성 판단용)
- 순수한 창 목록만 제공 (런처/스타트업 없음)
- 그룹핑/필터링 없이 모든 개별 창을 1:1로 나열
- 가시성 판단은 **모든 개별 창**의 geometry를 검사해야 하므로 적합
- Krema에서는 별도 인스턴스 생성: `m_windowModel = new TaskManager::WindowTasksModel(this)`
- 데스크탑/활동 필터링은 수동으로 구현 (프록시 체인 없으므로)

## 주요 역할(Role) — AbstractTasksModel::AdditionalRoles

### 창 식별/상태
| Role | 타입 | 설명 |
|------|------|------|
| `IsWindow` | bool | 윈도우 태스크인지 |
| `IsStartup` | bool | 스타트업 태스크인지 |
| `IsLauncher` | bool | 런처 태스크인지 |
| `IsGroupParent` | bool | 그룹 부모 항목인지 |
| `ChildCount` | int | 그룹 내 태스크 수 |

### 창 상태
| Role | 타입 | 설명 |
|------|------|------|
| `IsActive` | bool | 현재 활성 창 |
| `IsMinimized` | bool | 최소화 상태 |
| `IsMaximized` | bool | 최대화 상태 |
| `IsFullScreen` | bool | 전체화면 상태 |
| `IsKeepAbove` | bool | 항상 위 |
| `IsKeepBelow` | bool | 항상 아래 |
| `IsHidden` | bool | 숨겨진 상태 (최소화와 다름) |
| `SkipTaskbar` | bool | 태스크바에 표시하지 않을 것 |

### 위치/데스크탑
| Role | 타입 | 설명 |
|------|------|------|
| `Geometry` | QRect | 창의 스크린 좌표 |
| `ScreenGeometry` | QRect | 창이 위치한 스크린의 geometry |
| `VirtualDesktops` | QVariantList | 소속 가상 데스크탑 ID 리스트 |
| `IsOnAllVirtualDesktops` | bool | 모든 데스크탑에 표시 |
| `Activities` | QStringList | 소속 활동 ID 리스트 |

### 앱 정보
| Role | 타입 | 설명 |
|------|------|------|
| `AppId` | QString | KService storage ID (.desktop 파일명) |
| `AppName` | QString | 앱 이름 |
| `LauncherUrl` | QUrl | 실행 가능한 URL |
| `AppPid` | int | 프로세스 ID (주의: kwin_wayland의 PID일 수 있음) |
