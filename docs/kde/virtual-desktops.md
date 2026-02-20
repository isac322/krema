# KDE 가상 데스크탑

> 출처: `/usr/include/taskmanager/virtualdesktopinfo.h`

## VirtualDesktopInfo 클래스

| 프로퍼티/메서드 | 타입 | 설명 |
|---------------|------|------|
| `currentDesktop` | QVariant | 현재 활성 데스크탑 ID |
| `numberOfDesktops` | int | 데스크탑 총 개수 |
| `desktopIds` | QVariantList | 모든 데스크탑 ID 리스트 |
| `desktopNames` | QStringList | 모든 데스크탑 이름 리스트 |
| `desktopLayoutRows` | int | 데스크탑 레이아웃 행 수 |
| `position(desktop)` | quint32 | 데스크탑의 순서 위치 (-1 = 유효하지 않음) |
| `navigationWrappingAround` | bool | 순환 탐색 활성화 여부 |

### 시그널
- `currentDesktopChanged` — 데스크탑 전환 시
- `numberOfDesktopsChanged` — 데스크탑 수 변경 시
- `desktopIdsChanged` — 데스크탑 ID 리스트 변경 시
- `desktopNamesChanged` — 이름 변경 시

## 데스크탑 ID 형식

**플랫폼에 따라 다름** (헤더 문서에 명시):
- **Wayland**: `QString` (구체적 형식은 KWin 구현에 의존)
- **X11**: `uint > 0`

`QVariant`로 래핑되므로 플랫폼 독립적으로 비교 가능.

## 창의 데스크탑 소속 확인 패턴

```cpp
const QVariant currentDesktop = virtualDesktopInfo->currentDesktop();

// 1. 모든 데스크탑에 표시되는 창은 항상 포함
if (idx.data(AbstractTasksModel::IsOnAllVirtualDesktops).toBool()) {
    // 이 창은 현재 데스크탑에도 있음
}

// 2. 특정 데스크탑에 있는지 확인
const auto desktops = idx.data(AbstractTasksModel::VirtualDesktops).toList();
if (desktops.contains(currentDesktop)) {
    // 이 창은 현재 데스크탑에 있음
}
```

## 주의사항

- `IsOnAllVirtualDesktops == true`인 창은 항상 가시성 판단에 포함해야 함
- `VirtualDesktops` role은 `QVariantList`를 반환 — 창이 여러 데스크탑에 동시에 존재 가능 (Wayland)
- X11에서는 하나 또는 전체 데스크탑만 가능
- 데스크탑 전환 시 모델 데이터가 비동기적으로 업데이트될 수 있으므로 디바운스 권장

## ActivityInfo 클래스

> 출처: `/usr/include/taskmanager/activityinfo.h`

| 프로퍼티/메서드 | 타입 | 설명 |
|---------------|------|------|
| `currentActivity` | QString | 현재 활성 활동 ID |
| `numberOfRunningActivities` | int | 실행 중인 활동 수 |
| `runningActivities()` | QStringList | 실행 중인 활동 ID 리스트 |
| `activityName(id)` | QString | 활동 이름 |
| `activityIcon(id)` | QString | 활동 아이콘 이름/경로 |

### 시그널
- `currentActivityChanged`
- `numberOfRunningActivitiesChanged`
- `namesOfRunningActivitiesChanged`
