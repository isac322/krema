# Phase 3: DockView 속성 중복 제거 + applySettings 제거

> 상태: `done`
> 선행 조건: Phase 2 완료
> 예상 소요: 1일

## 목표

DockView가 DockSettings와 중복 보유하는 6개 속성을 제거하고,
QML에서 설정값을 DockSettings 싱글톤에서 직접 읽도록 변경.
`Application::applySettings()` 메서드를 제거하고 개별 시그널 연결로 교체.

## 현재 데이터 흐름 (비효율)

```
QML ←bind→ DockView.iconSize ←push← Application::applySettings() ←signal← DockSettings.settingsChanged()
```

## 개선 후 데이터 흐름

```
QML ←bind→ DockSettings.iconSize (직접 바인딩)
DockSettings.iconSizeChanged →connect→ DockView::updateSize() (C++ 내부 로직만)
```

---

## DockView에서 제거할 속성 (6개)

| Q_PROPERTY | 백킹 필드 | setter | signal |
|------------|----------|--------|--------|
| `iconSize` | `m_iconSize` | `setIconSize()` | `iconSizeChanged()` |
| `iconSpacing` | `m_iconSpacing` | `setIconSpacing()` | `iconSpacingChanged()` |
| `maxZoomFactor` | `m_maxZoomFactor` | `setMaxZoomFactor()` | `maxZoomFactorChanged()` |
| `cornerRadius` | `m_cornerRadius` | `setCornerRadius()` | `cornerRadiusChanged()` |
| `floating` (isFloating) | `m_floating` | `setFloating()` | `floatingChanged()` |
| (setBackgroundOpacity) | `m_backgroundOpacity` | `setBackgroundOpacity()` | (backgroundColorChanged) |

## DockView에 유지할 속성

| Q_PROPERTY | 이유 |
|------------|------|
| `backgroundColor` | BackgroundStyle 색상 + opacity 계산 결과 (파생 값) |
| `floatingPadding` | floating 상태에서 파생 (8 또는 0) |

---

## 변경 상세

### 1. DockView 헤더 변경

**파일:** `src/shell/dockview.h`

**Q_PROPERTY 제거** (line 38-43):
```cpp
// 삭제:
Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY iconSizeChanged)
Q_PROPERTY(int iconSpacing READ iconSpacing WRITE setIconSpacing NOTIFY iconSpacingChanged)
Q_PROPERTY(qreal maxZoomFactor READ maxZoomFactor WRITE setMaxZoomFactor NOTIFY maxZoomFactorChanged)
Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)
Q_PROPERTY(bool floating READ isFloating WRITE setFloating NOTIFY floatingChanged)
```

**유지:**
```cpp
Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY backgroundColorChanged)
Q_PROPERTY(int floatingPadding READ floatingPadding NOTIFY floatingPaddingChanged)
```

**getter/setter 선언 제거:**
```cpp
// 삭제:
int iconSize() const;
void setIconSize(int size);
int iconSpacing() const;
void setIconSpacing(int spacing);
qreal maxZoomFactor() const;
void setMaxZoomFactor(qreal factor);
int cornerRadius() const;
void setCornerRadius(int radius);
bool isFloating() const;
void setFloating(bool floating);
void setBackgroundOpacity(qreal opacity);
```

**시그널 제거:**
```cpp
// 삭제:
void iconSizeChanged();
void iconSpacingChanged();
void maxZoomFactorChanged();
void cornerRadiusChanged();
void floatingChanged();
```

**백킹 필드 제거:**
```cpp
// 삭제:
int m_iconSize = 48;
int m_iconSpacing = 4;
qreal m_maxZoomFactor = 1.6;
int m_cornerRadius = 12;
bool m_floating = true;
qreal m_backgroundOpacity = 0.6;
```

**DockSettings 포인터 추가:**
```cpp
class KremaSettings;  // forward declaration

// 생성자 변경:
explicit DockView(std::unique_ptr<DockPlatform> platform, KremaSettings *settings, QWindow *parent = nullptr);

// 멤버 추가:
KremaSettings *m_settings = nullptr;
```

### 2. DockView 구현 변경

**파일:** `src/shell/dockview.cpp`

**생성자:**
```cpp
DockView::DockView(std::unique_ptr<DockPlatform> platform, KremaSettings *settings, QWindow *parent)
    : QQuickView(parent)
    , m_platform(std::move(platform))
    , m_backgroundStyle(std::make_unique<PanelInheritStyle>())
    , m_settings(settings)
{
    // ...
}
```

**backgroundColor():**
```cpp
QColor DockView::backgroundColor() const {
    QColor color = m_backgroundStyle->backgroundColor();
    color.setAlphaF(m_settings->backgroundOpacity());
    return color;
}
```

**floatingPadding():**
```cpp
int DockView::floatingPadding() const {
    return m_settings->floating() ? s_floatingMargin : 0;
}
```

**panelBarHeight():**
```cpp
int DockView::panelBarHeight() const {
    return m_settings->iconSize() + s_padding * 2 + floatingPadding();
}
```

**updateSize():**
```cpp
void DockView::updateSize() {
    const int dockHeight = m_settings->iconSize() + s_padding * 2;
    const int overflowHeight = std::max(zoomOverflowHeight(), s_tooltipReserve);
    const int surfaceHeight = dockHeight + overflowHeight + floatingPadding();
    // ... 나머지 동일
}
```

**zoomOverflowHeight():**
```cpp
int DockView::zoomOverflowHeight() const {
    return static_cast<int>(std::ceil(m_settings->iconSize() * (m_settings->maxZoomFactor() - 1.0)));
}
```

**모든 setter 메서드 삭제** (setIconSize, setIconSpacing, setMaxZoomFactor, setCornerRadius, setFloating, setBackgroundOpacity)

### 3. Application 변경

**파일:** `src/app/application.cpp`

**applySettings() 메서드 삭제.**

**run() 메서드에서:**

기존 초기 설정 push 코드 제거:
```cpp
// 삭제:
m_dockView->setIconSize(m_settings->iconSize());
m_dockView->setIconSpacing(m_settings->iconSpacing());
// ... 등 6줄
```

DockView 생성자에 settings 전달:
```cpp
m_dockView = std::make_unique<DockView>(std::move(platform), m_settings.get());
```

개별 시그널 연결 추가:
```cpp
auto *s = m_settings.get();

// DockView 표면 크기 갱신이 필요한 설정들
connect(s, &KremaSettings::iconSizeChanged, m_dockView.get(), [this]() { m_dockView->updateSize(); });
connect(s, &KremaSettings::maxZoomFactorChanged, m_dockView.get(), [this]() { m_dockView->updateSize(); });
connect(s, &KremaSettings::floatingChanged, m_dockView.get(), [this]() {
    m_dockView->updateSize();
    Q_EMIT m_dockView->floatingPaddingChanged();
});

// 배경 색상 갱신
connect(s, &KremaSettings::backgroundOpacityChanged, m_dockView.get(), [this]() {
    Q_EMIT m_dockView->backgroundColorChanged();
});

// 플랫폼 레벨 변경
connect(s, &KremaSettings::edgeChanged, this, [this]() {
    m_dockView->platform()->setEdge(static_cast<DockPlatform::Edge>(m_settings->edge()));
});
connect(s, &KremaSettings::visibilityModeChanged, this, [this]() {
    m_dockView->visibilityController()->setMode(
        static_cast<DockPlatform::VisibilityMode>(m_settings->visibilityMode()));
});

// 딜레이 설정
connect(s, &KremaSettings::showDelayChanged, this, [this]() {
    m_dockView->visibilityController()->setShowDelay(m_settings->showDelay());
});
connect(s, &KremaSettings::hideDelayChanged, this, [this]() {
    m_dockView->visibilityController()->setHideDelay(m_settings->hideDelay());
});
connect(s, &KremaSettings::previewHideDelayChanged, this, [this]() {
    m_previewController->setHideDelay(m_settings->previewHideDelay());
});
```

**applySettings 관련 시그널 연결 삭제:**
```cpp
// 삭제:
connect(m_settings.get(), &DockSettings::settingsChanged, this, &Application::applySettings);
```

**Application::applySettings() 선언도 헤더에서 삭제.**

### 4. QML 바인딩 변경

모든 QML 파일에서 `DockView.iconSize` 등 → `DockSettings.iconSize`로 변경.

**`src/qml/main.qml` 변경 목록:**
- `DockView.iconSize` → `DockSettings.iconSize` (약 10곳)
- `DockView.iconSpacing` → `DockSettings.iconSpacing` (약 2곳)
- `DockView.maxZoomFactor` → `DockSettings.maxZoomFactor` (약 3곳)
- `DockView.cornerRadius` → `DockSettings.cornerRadius` (1곳)
- `DockView.floatingPadding` → `DockView.floatingPadding` (유지 — 파생 값)
- `DockView.backgroundColor` → `DockView.backgroundColor` (유지 — 파생 값)

**`src/qml/DockItem.qml`:**
- `iconSize` prop은 main.qml에서 `DockSettings.iconSize`로 전달됨 (DockItem 자체 변경 없음)
- `maxZoomFactor` prop도 마찬가지

**`src/qml/PreviewThumbnail.qml`:**
- 이미 `DockSettings.previewThumbnailSize` 사용 (변경 없음)

### 5. PreviewController에서 DockView 프로퍼티 참조 변경

**파일:** `src/shell/previewcontroller.cpp`

`doShow()`과 `initialize()`에서 `m_dockView->iconSize()`, `m_dockView->maxZoomFactor()` 호출을
`m_settings->iconSize()`, `m_settings->maxZoomFactor()`로 변경.

PreviewController 생성자에 `KremaSettings*` 파라미터 추가:
```cpp
PreviewController(DockModel *model, DockView *dockView, KremaSettings *settings, QObject *parent);
```

---

## 검증

1. `cmake --build build` 성공
2. Grep으로 `DockView.iconSize` 등이 QML에서 사라졌는지 확인 (DockView에서 제거된 속성들)
3. Grep으로 `applySettings` 잔존 0건
4. kwin-mcp: 설정 UI에서 아이콘 크기 변경 → 독 즉시 반영
5. kwin-mcp: floating 토글 → 독 위치 즉시 변경
