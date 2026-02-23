# Phase 0: Quick Wins

> 상태: `done`
> 선행 조건: 없음
> 예상 소요: 0.5일

## 목표

독립적으로 즉시 수정 가능한 문제들을 처리한다. 다른 Phase에 영향 없음.

---

## 0A. KLocalizedContext → KLocalization 전환

`KLocalizedContext`는 KF6.8에서 deprecated. `KLocalization::setupLocalizedContext()`으로 교체.

### 변경 파일: `src/shell/settingswindow.cpp`

**Before (line 8):**
```cpp
#include <KLocalizedContext>
```

**After:**
```cpp
#include <KLocalization>
```

**Before (line 107):**
```cpp
m_engine->rootContext()->setContextObject(new KLocalizedContext(m_engine));
```

**After:**
```cpp
KLocalization::setupLocalizedContext(m_engine);
```

### CMakeLists.txt 변경 없음
`KF6::I18n`이 이미 링크되어 있으며 `KLocalization`은 같은 모듈에 포함.

---

## 0B. 앱 ID 변경: org.krema → com.bhyoo.krema

### 1. Desktop 파일 이름 변경

**파일명:** `src/org.krema.desktop.in` → `src/com.bhyoo.krema.desktop.in`

파일 내용에서 별도 변경 없음 (desktop 파일 내부에 ID 필드 없음, 파일명이 곧 ID).

### 2. CMakeLists.txt 참조 변경

**파일:** `src/CMakeLists.txt`

**Before (line 73-80):**
```cmake
configure_file(
    org.krema.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.krema.desktop
    @ONLY
)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/org.krema.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)
```

**After:**
```cmake
configure_file(
    com.bhyoo.krema.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/com.bhyoo.krema.desktop
    @ONLY
)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/com.bhyoo.krema.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)
```

### 3. KAboutData 설정 변경

**파일:** `src/app/application.cpp`

**Before (line 60):**
```cpp
setDesktopFileName(QStringLiteral("org.krema"));
```

**After:**
```cpp
aboutData.setOrganizationDomain(QStringLiteral("bhyoo.com"));
setDesktopFileName(QStringLiteral("com.bhyoo.krema"));
```

### 4. Desktop 파일 내 X-KDE-Wayland-Interfaces 유지

`src/com.bhyoo.krema.desktop.in` 내용은 기존과 동일:
```ini
[Desktop Entry]
Type=Application
Name=Krema
GenericName=Dock
Comment=A dock application for KDE Plasma 6
Exec=@KDE_INSTALL_FULL_BINDIR@/krema
Icon=krema
Terminal=false
Categories=Qt;KDE;Utility;
NoDisplay=true
X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,org_kde_plasma_activation_feedback,zkde_screencast_unstable_v1
```

---

## 0C. 버전 동기화

**파일:** `src/app/application.cpp`

**Before (line 57):**
```cpp
KAboutData aboutData(QStringLiteral("krema"), i18n("Krema"), QStringLiteral("0.2.0"), ...);
```

**After:** CMakeLists.txt에서 정의된 `PROJECT_VERSION`을 사용:
```cpp
KAboutData aboutData(QStringLiteral("krema"), i18n("Krema"), QStringLiteral(KREMA_VERSION_STRING), ...);
```

**파일:** `CMakeLists.txt` (루트)에 추가:
```cmake
target_compile_definitions(krema_lib PUBLIC KREMA_VERSION_STRING="${PROJECT_VERSION}")
```

또는 더 간단하게 하드코딩 버전을 `"0.2.1"`로 맞추기:
```cpp
KAboutData aboutData(QStringLiteral("krema"), i18n("Krema"), QStringLiteral("0.2.1"), ...);
```

> 권장: CMake 변수 사용 (향후 버전 변경 시 한 곳만 수정)

---

## 0D. AppStream 메타데이터 생성

### 새 파일: `src/com.bhyoo.krema.metainfo.xml`

```xml
<?xml version="1.0" encoding="utf-8"?>
<component type="desktop-application">
  <id>com.bhyoo.krema</id>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-3.0-or-later</project_license>
  <name>Krema</name>
  <summary>A dock application for KDE Plasma 6</summary>
  <description>
    <p>
      Krema is a lightweight dock for KDE Plasma 6, designed as a spiritual successor
      to Latte Dock. It provides quick access to your favorite applications with
      smooth parabolic zoom animations, window previews via PipeWire, and deep
      integration with KDE Plasma desktop features.
    </p>
  </description>
  <launchable type="desktop-id">com.bhyoo.krema.desktop</launchable>
  <url type="homepage">https://github.com/isac322/krema</url>
  <url type="bugtracker">https://github.com/isac322/krema/issues</url>
  <developer id="com.bhyoo">
    <name>Byeonghoon Yoo</name>
  </developer>
  <provides>
    <binary>krema</binary>
  </provides>
  <content_rating type="oars-1.1"/>
  <releases>
    <release version="0.2.1" date="2026-02-23"/>
  </releases>
</component>
```

> 주의: homepage URL은 실제 리포 URL로 교체 필요. 위는 예시.

### CMakeLists.txt에 install 규칙 추가

**파일:** `src/CMakeLists.txt` 끝에 추가:
```cmake
install(
    FILES com.bhyoo.krema.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)
```

---

## 검증

1. `cmake --build build` 성공
2. `appstreamcli validate src/com.bhyoo.krema.metainfo.xml` 통과 (설치되어 있다면)
3. kwin-mcp 세션으로 독 기본 동작 확인
