# Phase 5: DockShell 추출 (Application 디커플링)

> 상태: `done`
> 선행 조건: Phase 3, Phase 4 완료
> 예상 소요: 2일

## 목표

하나의 독 인스턴스에 필요한 모든 객체를 `DockShell`로 캡슐화.
Application을 God Object에서 얇은 조립 계층으로 단순화.
M8(멀티 모니터)에서 `DockShell`을 화면별로 복수 생성 가능하게 준비.

---

## 새 클래스: DockShell

**새 파일:** `src/shell/dockshell.h`

```cpp
#pragma once
#include "platform/dockplatform.h"
#include <QObject>
#include <memory>

namespace krema {

class KremaSettings;
class DockModel;
class DockView;
class DockActions;
class DockContextMenu;
class DockVisibilityController;
class PreviewController;
class SettingsWindow;

class DockShell : public QObject {
    Q_OBJECT
public:
    explicit DockShell(KremaSettings *settings, DockModel *model, QObject *parent = nullptr);
    ~DockShell() override;

    void initialize(DockPlatform::Edge edge, DockPlatform::VisibilityMode visibilityMode);

    [[nodiscard]] DockView *view() const;
    [[nodiscard]] DockActions *actions() const;
    [[nodiscard]] DockContextMenu *contextMenu() const;
    [[nodiscard]] PreviewController *previewController() const;
    [[nodiscard]] SettingsWindow *settingsWindow() const;

private:
    void connectSettingsSignals();
    void connectMenuSignals();

    KremaSettings *m_settings;
    DockModel *m_model;

    std::unique_ptr<DockView> m_view;
    std::unique_ptr<DockActions> m_actions;
    std::unique_ptr<DockContextMenu> m_contextMenu;
    PreviewController *m_previewController = nullptr;
    std::unique_ptr<SettingsWindow> m_settingsWindow;
};

} // namespace krema
```

**새 파일:** `src/shell/dockshell.cpp`

`initialize()` 메서드에 현재 `Application::run()`의 독 인스턴스 관련 코드를 이동:

```cpp
void DockShell::initialize(DockPlatform::Edge edge, DockPlatform::VisibilityMode visibilityMode) {
    // 1. 플랫폼 생성
    auto platform = DockPlatformFactory::create();

    // 2. DockActions + DockContextMenu 생성
    m_actions = std::make_unique<DockActions>(m_model, this);
    m_contextMenu = std::make_unique<DockContextMenu>(m_model, m_actions.get(), this);

    // 3. DockView 생성 (settings 포인터 전달)
    m_view = std::make_unique<DockView>(std::move(platform), m_settings);

    // 4. PreviewController 생성
    m_previewController = new PreviewController(m_model, m_view.get(), m_settings, m_view.get());

    // 5. DockView 초기화 (QML 로딩)
    m_view->initialize(m_model->tasksModel(), m_model->virtualDesktopInfo(),
                       m_model->activityInfo(), edge, visibilityMode);

    // 6. PreviewController 초기화
    m_previewController->setHideDelay(m_settings->previewHideDelay());
    m_previewController->initialize();

    // 7. 설정 시그널 연결
    connectSettingsSignals();

    // 8. SettingsWindow 생성
    m_settingsWindow = std::make_unique<SettingsWindow>(m_settings, this);
    connectMenuSignals();
}
```

`connectSettingsSignals()`:
```cpp
void DockShell::connectSettingsSignals() {
    auto *s = m_settings;

    // 표면 크기 갱신
    connect(s, &KremaSettings::iconSizeChanged, m_view.get(), [this]() { m_view->updateSize(); });
    connect(s, &KremaSettings::maxZoomFactorChanged, m_view.get(), [this]() { m_view->updateSize(); });
    connect(s, &KremaSettings::floatingChanged, m_view.get(), [this]() {
        m_view->updateSize();
        Q_EMIT m_view->floatingPaddingChanged();
    });

    // 배경
    connect(s, &KremaSettings::backgroundOpacityChanged, m_view.get(), [this]() {
        Q_EMIT m_view->backgroundColorChanged();
    });

    // 플랫폼
    connect(s, &KremaSettings::edgeChanged, this, [this]() {
        m_view->platform()->setEdge(static_cast<DockPlatform::Edge>(m_settings->edge()));
    });
    connect(s, &KremaSettings::visibilityModeChanged, this, [this]() {
        m_view->visibilityController()->setMode(
            static_cast<DockPlatform::VisibilityMode>(m_settings->visibilityMode()));
    });

    // 딜레이
    connect(s, &KremaSettings::showDelayChanged, this, [this]() {
        m_view->visibilityController()->setShowDelay(m_settings->showDelay());
    });
    connect(s, &KremaSettings::hideDelayChanged, this, [this]() {
        m_view->visibilityController()->setHideDelay(m_settings->hideDelay());
    });
    connect(s, &KremaSettings::previewHideDelayChanged, this, [this]() {
        m_previewController->setHideDelay(m_settings->previewHideDelay());
    });

    // 핀 런처 동기화
    connect(m_actions.get(), &DockActions::pinnedLaunchersChanged, this, [this]() {
        m_settings->setPinnedLaunchers(m_model->pinnedLaunchers());
    });
}
```

`connectMenuSignals()`:
```cpp
void DockShell::connectMenuSignals() {
    connect(m_contextMenu.get(), &DockContextMenu::visibleChanged,
            m_view->visibilityController(), &DockVisibilityController::setInteracting);

    connect(m_contextMenu.get(), &DockContextMenu::settingsRequested, this, [this]() {
        m_settingsWindow->show();
    });
    connect(m_contextMenu.get(), &DockContextMenu::aboutRequested, this, [this]() {
        m_settingsWindow->show(QStringLiteral("about"));
    });

    connect(m_settingsWindow.get(), &SettingsWindow::visibleChanged,
            m_view->visibilityController(), &DockVisibilityController::setInteracting);
}
```

---

## Application 단순화

**파일:** `src/app/application.h`

```cpp
class Application : public QApplication {
    Q_OBJECT
public:
    Application(int &argc, char **argv);
    ~Application() override;
    int run();

private:
    void registerGlobalShortcuts();

    std::unique_ptr<KremaSettings> m_settings;
    std::unique_ptr<DockModel> m_dockModel;
    std::unique_ptr<DockShell> m_shell;
    KActionCollection *m_actionCollection = nullptr;
};
```

**파일:** `src/app/application.cpp`

`run()` 메서드 단순화:
```cpp
int Application::run() {
    // Qt Quick style
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    KCrash::initialize();

    // KDE metadata
    KAboutData aboutData(...);
    aboutData.setOrganizationDomain(QStringLiteral("bhyoo.com"));
    KAboutData::setApplicationData(aboutData);
    setDesktopFileName(QStringLiteral("com.bhyoo.krema"));

    initResources();
    LayerShellQt::Shell::useLayerShell();

    // 설정 + 모델 생성
    m_settings = std::make_unique<KremaSettings>();
    m_dockModel = std::make_unique<DockModel>();
    m_dockModel->setPinnedLaunchers(m_settings->pinnedLaunchers());

    // QML 싱글톤 등록
    qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockModel", m_dockModel.get());
    qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockSettings", m_settings.get());

    // 독 인스턴스 생성
    m_shell = std::make_unique<DockShell>(m_settings.get(), m_dockModel.get(), this);

    // DockShell 내부에서 생성되는 객체들도 싱글톤으로 등록
    qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockView", m_shell->view());
    qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockActions", m_shell->actions());
    qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockContextMenu", m_shell->contextMenu());
    qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "PreviewController", m_shell->previewController());

    m_shell->initialize(
        static_cast<DockPlatform::Edge>(m_settings->edge()),
        static_cast<DockPlatform::VisibilityMode>(m_settings->visibilityMode()));

    // DockVisibility는 initialize 안에서 생성되므로 여기서 등록
    qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockVisibility",
                                 m_shell->view()->visibilityController());

    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
    registerGlobalShortcuts();

    return exec();
}
```

---

## CMakeLists.txt 변경

**파일:** `src/CMakeLists.txt`

소스 추가:
```cmake
shell/dockshell.h
shell/dockshell.cpp
```

---

## 검증

1. `cmake --build build` 성공
2. `Application::run()` 코드가 ~30줄 이내로 단순화됐는지 확인
3. kwin-mcp: 전체 기능 (줌, 클릭, 프리뷰, 설정, 드래그, 단축키) 동작 확인
4. 향후 M8에서 `DockShell`을 복수 생성 가능한 구조인지 코드 리뷰
