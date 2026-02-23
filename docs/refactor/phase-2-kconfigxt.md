# Phase 2: KConfigXT 마이그레이션

> 상태: `done`
> 선행 조건: Phase 1 완료
> 예상 소요: 1~2일

## 목표

수동 KConfig 보일러플레이트(~230줄)를 KConfigXT 자동 생성으로 교체.
속성별 개별 NOTIFY 시그널 확보 → Phase 3에서 `applySettings()` 제거 가능.

## 현재 문제

`src/config/docksettings.cpp`의 모든 setter가 동일한 패턴:
```cpp
void DockSettings::setIconSize(int size) {
    m_config->group(s_group).writeEntry("IconSize", size);
    save();
    Q_EMIT settingsChanged();  // ← 단일 시그널
}
```

이 단일 `settingsChanged()` 시그널이 `Application::applySettings()`를 트리거하면,
변경되지 않은 속성까지 모든 서브시스템에 push됨.

---

## 변경 상세

### 1. KConfigXT 스키마 파일 생성

**새 파일:** `src/config/krema.kcfg`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<kcfg xmlns="http://www.kde.org/standards/kcfg/1.0"
      xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
      xsi:schemaLocation="http://www.kde.org/standards/kcfg/1.0
                          http://www.kde.org/standards/kcfg/1.0/kcfg.xsd">
  <kcfgfile name="kremarc"/>

  <group name="General">
    <entry name="IconSize" type="Int">
      <label>Icon size in pixels</label>
      <default>48</default>
      <min>24</min>
      <max>96</max>
    </entry>

    <entry name="IconSpacing" type="Int">
      <label>Spacing between icons</label>
      <default>4</default>
      <min>0</min>
      <max>16</max>
    </entry>

    <entry name="MaxZoomFactor" type="Double">
      <label>Maximum parabolic zoom factor</label>
      <default>1.6</default>
      <min>1.0</min>
      <max>2.0</max>
    </entry>

    <entry name="CornerRadius" type="Int">
      <label>Panel corner radius</label>
      <default>12</default>
      <min>0</min>
      <max>24</max>
    </entry>

    <entry name="Floating" type="Bool">
      <label>Whether the dock floats above the screen edge</label>
      <default>true</default>
    </entry>

    <entry name="VisibilityMode" type="Int">
      <label>Dock visibility mode (0=Always, 1=Hidden, 2=Dodge, 3=Smart)</label>
      <default>0</default>
    </entry>

    <entry name="Edge" type="Int">
      <label>Screen edge (0=Top, 1=Bottom, 2=Left, 3=Right)</label>
      <default>1</default>
    </entry>

    <entry name="ShowDelay" type="Int">
      <label>Delay before showing dock (ms)</label>
      <default>200</default>
    </entry>

    <entry name="HideDelay" type="Int">
      <label>Delay before hiding dock (ms)</label>
      <default>400</default>
    </entry>

    <entry name="BackgroundOpacity" type="Double">
      <label>Background opacity</label>
      <default>0.6</default>
      <min>0.1</min>
      <max>1.0</max>
    </entry>

    <entry name="PreviewEnabled" type="Bool">
      <label>Enable window preview on hover</label>
      <default>true</default>
    </entry>

    <entry name="PreviewThumbnailSize" type="Int">
      <label>Preview thumbnail width</label>
      <default>200</default>
      <min>120</min>
      <max>320</max>
    </entry>

    <entry name="PreviewHoverDelay" type="Int">
      <label>Delay before showing preview (ms)</label>
      <default>500</default>
    </entry>

    <entry name="PreviewHideDelay" type="Int">
      <label>Delay before hiding preview (ms)</label>
      <default>200</default>
    </entry>

    <entry name="PinnedLaunchers" type="StringList">
      <label>Pinned launcher URLs</label>
      <default>applications:org.kde.dolphin.desktop,applications:org.kde.konsole.desktop,applications:org.kde.kate.desktop,applications:systemsettings.desktop</default>
    </entry>
  </group>
</kcfg>
```

### 2. KConfigXT 코드 생성 설정

**새 파일:** `src/config/krema.kcfgc`

```ini
File=krema.kcfg
ClassName=KremaSettings
Mutators=true
DefaultValueGetters=true
GenerateProperties=true
ParentInConstructor=true
Singleton=false
```

핵심: `GenerateProperties=true` → 각 entry에 대해:
- `Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY iconSizeChanged)`
- setter가 값 변경 시 자동으로 `iconSizeChanged()` 시그널 emit + KConfig에 저장

### 3. CMakeLists.txt 변경

**파일:** `src/CMakeLists.txt`

```cmake
# 기존 소스 목록에서 제거:
#    config/docksettings.h
#    config/docksettings.cpp

# KConfigXT 생성 추가 (소스 목록 위에):
kconfig_add_kcfg_files(KREMA_KCFG_SRCS config/krema.kcfgc)

# krema_lib 소스에 추가:
add_library(krema_lib STATIC
    ...
    ${KREMA_KCFG_SRCS}
    ...
)
```

### 4. 삭제 파일

- `src/config/docksettings.h`
- `src/config/docksettings.cpp`

### 5. Application 코드 변경

**파일:** `src/app/application.h`

```cpp
// 기존:
#include "config/docksettings.h"
// 변경:
#include "config/kremasettings.h"  // KConfigXT 생성 파일

// 멤버 타입 변경:
std::unique_ptr<KremaSettings> m_settings;  // was DockSettings
```

**파일:** `src/app/application.cpp`

```cpp
// 기존:
m_settings = std::make_unique<DockSettings>();
// 변경:
m_settings = std::make_unique<KremaSettings>();
```

`qmlRegisterSingletonInstance` 호출은 그대로 유지 (타입명만 변경):
```cpp
qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "DockSettings", m_settings.get());
```
> QML에서는 여전히 `DockSettings`로 접근 — QML 쪽 변경 없음

### 6. 시그널 연결 변경

**파일:** `src/app/application.cpp`

KConfigXT가 생성하는 개별 시그널은 `<propertyName>Changed()` 형태.
**이 Phase에서는 기존 `applySettings()` 패턴을 유지**하되, 시그널 연결만 변경:

```cpp
// 기존:
connect(m_settings.get(), &DockSettings::settingsChanged, this, &Application::applySettings);

// Phase 2에서는 KremaSettings의 개별 시그널을 applySettings에 묶음:
// (Phase 3에서 applySettings 자체를 제거할 예정)
auto *s = m_settings.get();
connect(s, &KremaSettings::iconSizeChanged, this, &Application::applySettings);
connect(s, &KremaSettings::iconSpacingChanged, this, &Application::applySettings);
connect(s, &KremaSettings::maxZoomFactorChanged, this, &Application::applySettings);
connect(s, &KremaSettings::cornerRadiusChanged, this, &Application::applySettings);
connect(s, &KremaSettings::floatingChanged, this, &Application::applySettings);
connect(s, &KremaSettings::backgroundOpacityChanged, this, &Application::applySettings);
connect(s, &KremaSettings::visibilityModeChanged, this, &Application::applySettings);
connect(s, &KremaSettings::edgeChanged, this, &Application::applySettings);
connect(s, &KremaSettings::showDelayChanged, this, &Application::applySettings);
connect(s, &KremaSettings::hideDelayChanged, this, &Application::applySettings);
connect(s, &KremaSettings::previewHideDelayChanged, this, &Application::applySettings);
```

pinnedLaunchers는 별도:
```cpp
connect(s, &KremaSettings::pinnedLaunchersChanged, ...);
```

### 7. 다른 C++ 파일에서 DockSettings 참조 변경

**파일:** `src/shell/settingswindow.h`
```cpp
// DockSettings → KremaSettings
class KremaSettings;  // forward declaration
```

**파일:** `src/shell/settingswindow.cpp`
```cpp
#include "config/kremasettings.h"  // was docksettings.h
```

### 8. QML 변경 없음

QML에서는 Phase 1에서 등록한 `DockSettings` 싱글톤 이름을 계속 사용.
KConfigXT가 생성하는 `KremaSettings` 클래스의 프로퍼티 이름이 기존 `DockSettings`와 동일하므로 QML 바인딩에 변경 없음.

---

## 호환성

- 동일한 파일: `~/.config/kremarc`
- 동일한 그룹: `[General]`
- 동일한 키 이름: `IconSize`, `Floating` 등
- 기존 사용자 설정이 그대로 읽힘

---

## 검증

1. `cmake --build build` 성공
2. 생성된 `kremasettings.h`에 `iconSizeChanged()` 등 개별 시그널 존재 확인
3. 기존 `~/.config/kremarc` 파일의 설정이 정상 로딩되는지 확인
4. kwin-mcp: 설정 UI에서 값 변경 → 독에 즉시 반영 확인
5. Grep으로 `DockSettings` → `docksettings.h` include 잔존 0건
