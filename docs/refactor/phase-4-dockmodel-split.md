# Phase 4: DockModel 분해

> 상태: `done`
> 선행 조건: Phase 1 완료
> 예상 소요: 2일

## 목표

비대한 DockModel(15+ Q_INVOKABLE)을 3개 클래스로 분리:
- **DockModel** (slim) — 모델 쿼리만
- **DockActions** — 태스크 조작 (activate, pin, drag 등)
- **DockContextMenu** — 컨텍스트 메뉴

---

## DockModel (slim) — 유지할 메서드

**파일:** `src/models/dockmodel.h/.cpp`

Q_PROPERTY 유지:
- `tasksModel` (CONSTANT)
- `pinnedLaunchers` (READ/WRITE/NOTIFY)

Q_INVOKABLE 유지 (읽기 전용):
- `iconName(int index)`
- `launcherUrl(int index)`
- `isDesktopFile(QUrl)`
- `isPinned(int index)`
- `windowIds(int index)`
- `childCount(int index)`
- `taskModelIndex(int index)`

시그널 유지:
- `pinnedLaunchersChanged()`

시그널 제거 (DockActions/DockContextMenu로 이동):
- `taskLaunching(int)`
- `settingsRequested()`
- `aboutRequested()`
- `contextMenuVisibleChanged(bool)`

Q_INVOKABLE 제거 (DockActions로 이동):
- `activate()`, `newInstance()`, `closeTask()`, `togglePinned()`, `cycleWindows()`
- `moveTask()`, `addLauncher()`, `removeLauncher()`, `openUrlsWithTask()`

Q_INVOKABLE 제거 (DockContextMenu로 이동):
- `showContextMenu()`

---

## DockActions — 새 클래스

**새 파일:** `src/models/dockactions.h`

```cpp
#pragma once
#include <QObject>
#include <QUrl>

namespace krema {

class DockModel;

class DockActions : public QObject {
    Q_OBJECT
public:
    explicit DockActions(DockModel *model, QObject *parent = nullptr);

    Q_INVOKABLE void activate(int index);
    Q_INVOKABLE void newInstance(int index);
    Q_INVOKABLE void closeTask(int index);
    Q_INVOKABLE void togglePinned(int index);
    Q_INVOKABLE void cycleWindows(int index, bool forward);
    Q_INVOKABLE bool moveTask(int fromIndex, int toIndex);
    Q_INVOKABLE bool addLauncher(const QUrl &url);
    Q_INVOKABLE bool removeLauncher(int index);
    Q_INVOKABLE void openUrlsWithTask(int index, const QList<QUrl> &urls);

Q_SIGNALS:
    void taskLaunching(int index);
    void pinnedLaunchersChanged();

private:
    DockModel *m_model;
};

} // namespace krema
```

**새 파일:** `src/models/dockactions.cpp`

기존 `DockModel`의 `activate()`, `newInstance()`, `closeTask()`, `togglePinned()`, `cycleWindows()`, `moveTask()`, `addLauncher()`, `removeLauncher()`, `openUrlsWithTask()` 메서드 구현을 그대로 이동.

각 메서드에서 `m_tasksModel` 대신 `m_model->tasksModel()`을 사용:
```cpp
void DockActions::activate(int index) {
    auto *tasksModel = m_model->tasksModel();
    const QModelIndex idx = tasksModel->index(index, 0);
    if (!idx.isValid()) return;

    const bool isLauncher = idx.data(TaskManager::AbstractTasksModel::IsLauncher).toBool();
    const bool isWindow = idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();

    if (isWindow) {
        tasksModel->requestActivate(idx);
    } else if (isLauncher) {
        tasksModel->requestNewInstance(idx);
        Q_EMIT taskLaunching(index);
    }
}
```

---

## DockContextMenu — 새 클래스

**새 파일:** `src/models/dockcontextmenu.h`

```cpp
#pragma once
#include <QObject>

namespace krema {

class DockModel;
class DockActions;

class DockContextMenu : public QObject {
    Q_OBJECT
public:
    explicit DockContextMenu(DockModel *model, DockActions *actions, QObject *parent = nullptr);

    Q_INVOKABLE void showForTask(int index);

Q_SIGNALS:
    void settingsRequested();
    void aboutRequested();
    void visibleChanged(bool visible);

private:
    DockModel *m_model;
    DockActions *m_actions;
};

} // namespace krema
```

**새 파일:** `src/models/dockcontextmenu.cpp`

기존 `DockModel::showContextMenu()` 구현을 그대로 이동.
내부에서 `m_model->isPinned()`, `m_actions->togglePinned()`, `m_actions->newInstance()`, `m_actions->closeTask()` 호출.

---

## Application 변경

**파일:** `src/app/application.h`

멤버 추가:
```cpp
std::unique_ptr<DockActions> m_dockActions;
std::unique_ptr<DockContextMenu> m_dockContextMenu;
```

**파일:** `src/app/application.cpp`

객체 생성 및 싱글톤 등록:
```cpp
m_dockActions = std::make_unique<DockActions>(m_dockModel.get(), this);
m_dockContextMenu = std::make_unique<DockContextMenu>(m_dockModel.get(), m_dockActions.get(), this);

qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockActions", m_dockActions.get());
qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockContextMenu", m_dockContextMenu.get());
```

시그널 연결 변경:
```cpp
// 기존:
// connect(m_dockModel.get(), &DockModel::pinnedLaunchersChanged, ...);
// 변경:
connect(m_dockActions.get(), &DockActions::pinnedLaunchersChanged, this, [this]() {
    m_settings->setPinnedLaunchers(m_dockModel->pinnedLaunchers());
});

// 기존:
// connect(m_dockModel.get(), &DockModel::contextMenuVisibleChanged, ...);
// 변경:
connect(m_dockContextMenu.get(), &DockContextMenu::visibleChanged,
        m_dockView->visibilityController(), &DockVisibilityController::setInteracting);

// 기존:
// connect(m_dockModel.get(), &DockModel::settingsRequested, ...);
// 변경:
connect(m_dockContextMenu.get(), &DockContextMenu::settingsRequested, this, [this]() {
    m_settingsWindow->show();
});
connect(m_dockContextMenu.get(), &DockContextMenu::aboutRequested, this, [this]() {
    m_settingsWindow->show(QStringLiteral("about"));
});
```

글로벌 단축키 연결:
```cpp
// 기존:
// m_dockModel->activate(i - 1);
// 변경:
m_dockActions->activate(i - 1);

// 기존:
// m_dockModel->newInstance(i - 1);
// 변경:
m_dockActions->newInstance(i - 1);
```

---

## CMakeLists.txt 변경

**파일:** `src/CMakeLists.txt`

소스 추가:
```cmake
models/dockactions.h
models/dockactions.cpp
models/dockcontextmenu.h
models/dockcontextmenu.cpp
```

---

## QML 변경

### `src/qml/main.qml`

```qml
// 기존 → 변경
DockModel.activate(root.hoveredIndex)      → DockActions.activate(root.hoveredIndex)
DockModel.newInstance(root.hoveredIndex)    → DockActions.newInstance(root.hoveredIndex)
DockModel.showContextMenu(root.hoveredIndex) → DockContextMenu.showForTask(root.hoveredIndex)
DockModel.cycleWindows(root.hoveredIndex, false) → DockActions.cycleWindows(root.hoveredIndex, false)
DockModel.cycleWindows(root.hoveredIndex, true)  → DockActions.cycleWindows(root.hoveredIndex, true)
DockModel.moveTask(...)                    → DockActions.moveTask(...)
DockModel.addLauncher(...)                 → DockActions.addLauncher(...)
DockModel.openUrlsWithTask(...)            → DockActions.openUrlsWithTask(...)
DockModel.isPinned(...)                    → DockModel.isPinned(...)  // 유지 (쿼리)
DockModel.removeLauncher(...)              → DockActions.removeLauncher(...)

// Connections 변경:
// target: DockModel → target: DockActions
Connections {
    target: DockActions  // was DockModel
    function onTaskLaunching(index) { ... }
}
```

변경하지 않는 것들 (DockModel에 유지):
- `DockModel.tasksModel`
- `DockModel.iconName()`
- `DockModel.isDesktopFile()`
- `DockModel.isPinned()`

### `src/qml/PreviewPopup.qml`
- 변경 없음 (DockModel 쿼리만 사용)

### `src/qml/PreviewThumbnail.qml`
- 변경 없음 (DockModel 쿼리만 사용)

---

## 검증

1. `cmake --build build` 성공
2. Grep으로 `DockModel.activate` 등이 QML에서 `DockActions.activate`로 변경됐는지 확인
3. kwin-mcp: 좌클릭 → 앱 활성화, 우클릭 → 컨텍스트 메뉴, 드래그 → 재정렬
4. kwin-mcp: 글로벌 단축키 Meta+1 → 앱 활성화
