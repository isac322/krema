# Phase 1: QML 싱글톤 등록 (Context Properties 제거)

> 상태: `done`
> 선행 조건: 없음 (Phase 0과 독립, 하지만 0 이후 진행 권장)
> 예상 소요: 1~2일

## 목표

deprecated된 `rootContext()->setContextProperty()` 패턴을 `qmlRegisterSingletonInstance()`로 전환.
모든 QML 파일에서 타입 안전한 싱글톤 import를 사용.

## 왜 qmlRegisterSingletonInstance인가

- `QML_SINGLETON` 매크로는 QML 엔진이 default-construct하는 타입에만 적합
- Krema의 C++ 객체들은 `Application::run()`에서 의존성 인자를 받아 생성됨
- `qmlRegisterSingletonInstance`는 C++에서 생성한 객체를 QML 싱글톤으로 등록
- 전역 QML 타입 시스템에 등록되므로 모든 QQmlEngine에서 자동 접근 가능
- 현재 3곳에서 중복 등록하던 `setContextProperty` 호출이 완전히 제거됨

## QML 모듈 URI

`com.bhyoo.krema` (앱 ID와 일치)

---

## 변경 상세

### 1. 싱글톤 등록 (C++ 측)

**파일:** `src/app/application.cpp`

**추가 include:**
```cpp
#include <QtQml>  // qmlRegisterSingletonInstance
```

**`run()` 메서드 내, 객체 생성 후 등록:**

기존 context property 호출들을 제거하고 싱글톤 등록으로 교체:

```cpp
// 기존 제거:
// m_dockView->rootContext()->setContextProperty("dockModel", m_dockModel.get());
// m_dockView->rootContext()->setContextProperty("dockSettings", m_settings.get());
// m_dockView->rootContext()->setContextProperty("previewController", m_previewController);

// 새로 추가 (객체 생성 직후, QML 로딩 전):
qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockModel", m_dockModel.get());
qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockSettings", m_settings.get());
qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "PreviewController", m_previewController);
```

**파일:** `src/shell/dockview.cpp`

**생성자 (line 32):**
```cpp
// 기존 제거:
// rootContext()->setContextProperty(QStringLiteral("dockView"), this);

// 새로 추가:
qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockView", this);
```

**initialize() 메서드 (line 55):**
```cpp
// 기존 제거:
// rootContext()->setContextProperty(QStringLiteral("dockVisibility"), m_visibilityController);

// 새로 추가:
qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockVisibility", m_visibilityController);
```

**추가 include:**
```cpp
#include <QtQml>
```

**파일:** `src/shell/previewcontroller.cpp`

**initialize() 메서드 (line 75-76):**
```cpp
// 기존 제거:
// m_previewView->rootContext()->setContextProperty(QStringLiteral("previewController"), this);
// m_previewView->rootContext()->setContextProperty(QStringLiteral("dockModel"), m_model);

// 이 줄들을 완전히 삭제. qmlRegisterSingletonInstance는 전역이므로
// 별도 엔진에서도 import만 하면 접근 가능.
```

**파일:** `src/shell/settingswindow.cpp`

**ensureEngine() 메서드 (line 108):**
```cpp
// 기존 제거:
// m_engine->rootContext()->setContextProperty(QStringLiteral("dockSettings"), m_settings);

// 이 줄을 삭제. DockSettings는 이미 전역 싱글톤으로 등록되어 있음.
```

---

### 2. QML 파일 변경 (8개 파일)

모든 QML 파일에 `import com.bhyoo.krema 1.0` 추가하고, lowercase 참조를 PascalCase로 변경.

#### 이름 매핑 테이블

| 기존 (context property) | 새 (singleton) |
|------------------------|---------------|
| `dockView` | `DockView` |
| `dockModel` | `DockModel` |
| `dockSettings` | `DockSettings` |
| `dockVisibility` | `DockVisibility` |
| `previewController` | `PreviewController` |

#### 파일별 변경 목록

**`src/qml/main.qml`:**
- import 추가: `import com.bhyoo.krema 1.0`
- `dockView.` → `DockView.` (iconSize, iconSpacing, maxZoomFactor, cornerRadius, floatingPadding, backgroundColor)
- `dockModel.` → `DockModel.` (tasksModel, iconName, activate, newInstance, showContextMenu, cycleWindows, moveTask, addLauncher, openUrlsWithTask, isDesktopFile, isPinned, removeLauncher)
- `dockVisibility.` → `DockVisibility.` (dockVisible, setHovered, setPanelRect, setInteracting)
- `dockSettings.` → `DockSettings.` (previewEnabled, previewHoverDelay)
- `previewController.` → `PreviewController.` (visible, showPreview, hidePreviewDelayed)
- 주석의 `// C++ context properties: dockView, dockModel, dockVisibility` 삭제 또는 갱신

**`src/qml/DockItem.qml`:**
- import 추가: `import com.bhyoo.krema 1.0`
- `dockModel.iconName(...)` → `DockModel.iconName(...)`

**`src/qml/PreviewPopup.qml`:**
- import 추가: `import com.bhyoo.krema 1.0`
- `previewController.` → `PreviewController.`
- `dockModel.` → `DockModel.`

**`src/qml/PreviewThumbnail.qml`:**
- import 추가: `import com.bhyoo.krema 1.0`
- `dockSettings.` → `DockSettings.`
- `dockModel.` → `DockModel.`
- `previewController.` → `PreviewController.`

**`src/qml/SettingsDialog.qml`:**
- import 추가: `import com.bhyoo.krema 1.0`
- (이 파일은 직접적으로 context property를 참조하지 않으므로 import만 추가)

**`src/qml/settings/AppearancePage.qml`:**
- import 추가: `import com.bhyoo.krema 1.0`
- `dockSettings.` → `DockSettings.`

**`src/qml/settings/BehaviorPage.qml`:**
- import 추가: `import com.bhyoo.krema 1.0`
- `dockSettings.` → `DockSettings.`

**`src/qml/settings/PreviewPage.qml`:**
- import 추가: `import com.bhyoo.krema 1.0`
- `dockSettings.` → `DockSettings.`

---

### 3. 등록 순서 주의사항

`qmlRegisterSingletonInstance`는 **QML 파일 로딩 전**에 호출해야 한다.
현재 `Application::run()`의 순서:

```
1. DockSettings 생성
2. DockModel 생성
3. DockView 생성  ← 여기서 DockView 싱글톤 등록
4. PreviewController 생성
5. ★ 여기서 DockModel, DockSettings, PreviewController 싱글톤 등록 ★
6. DockView::initialize()  ← 여기서 DockVisibility 싱글톤 등록 + QML 로딩
7. PreviewController::initialize()  ← 여기서 PreviewPopup.qml 로딩
```

모든 `qmlRegisterSingletonInstance` 호출이 `setSource()` / `load()` 전에 완료되어야 함.
현재 순서에서 DockView는 생성자에서 등록, initialize()에서 QML 로딩 → OK.
DockVisibility는 initialize()에서 생성 + 등록 후 setSource() → OK.

---

## 검증

1. `cmake --build build` 성공
2. Grep으로 `setContextProperty` 잔존 확인: 0건이어야 함
3. kwin-mcp 세션: 독 표시, 줌, 클릭, 프리뷰, 설정 UI 모두 동작 확인
4. 로그에서 QML 바인딩 경고(undefined reference) 없음 확인
