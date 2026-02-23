# Phase 6: 유닛 테스트 인프라

> 상태: `done`
> 선행 조건: Phase 3, Phase 4 완료
> 예상 소요: 2~3일

## 목표

핵심 로직을 순수 함수로 추출하고 Catch2 유닛 테스트 작성.
기존 smoke test를 실질적인 테스트 스위트로 확장.

---

## Tier 1: 순수 함수 추출 + 테스트

### 1A. 패러볼릭 줌 계산

**현재 위치:** `src/qml/DockItem.qml` (JS)
```javascript
property real zoomFactor: {
    if (!panelMouseInside) return 1.0
    let dist = Math.abs(panelMouseX - itemCenterX)
    let sigma = iconSize * 1.2
    return 1.0 + (maxZoomFactor - 1.0) * Math.exp(-(dist * dist) / (2 * sigma * sigma))
}
```

**새 파일:** `src/utils/zoomcalculator.h`
```cpp
#pragma once

namespace krema {

/// Compute parabolic zoom factor using Gaussian curve.
/// @param distance Distance from mouse to icon center (pixels)
/// @param iconSize Base icon size (pixels)
/// @param maxZoomFactor Maximum zoom multiplier (e.g. 1.6)
/// @return Zoom factor in range [1.0, maxZoomFactor]
inline double parabolicZoomFactor(double distance, double iconSize, double maxZoomFactor)
{
    if (maxZoomFactor <= 1.0) return 1.0;
    const double sigma = iconSize * 1.2;
    return 1.0 + (maxZoomFactor - 1.0) * std::exp(-(distance * distance) / (2.0 * sigma * sigma));
}

} // namespace krema
```

**DockItem.qml에서 호출로 전환 (선택적):**
C++ 유틸을 QML에서 호출하려면 Q_INVOKABLE 또는 `qmlRegisterSingletonType<ZoomCalculator>` 필요.
**이 Phase에서는 C++ 추출 + 테스트만 하고, QML은 기존 JS 유지** (동작 변경 없음).
향후 QML→C++ 호출 전환은 별도 작업.

**새 파일:** `tests/unit/test_zoom_calculator.cpp`
```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "utils/zoomcalculator.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("Parabolic zoom factor", "[zoom]") {
    SECTION("Distance 0 gives maximum zoom") {
        double f = krema::parabolicZoomFactor(0.0, 48.0, 1.6);
        REQUIRE_THAT(f, WithinAbs(1.6, 0.001));
    }

    SECTION("Large distance gives zoom near 1.0") {
        double f = krema::parabolicZoomFactor(500.0, 48.0, 1.6);
        REQUIRE_THAT(f, WithinAbs(1.0, 0.01));
    }

    SECTION("maxZoomFactor 1.0 always returns 1.0") {
        double f = krema::parabolicZoomFactor(0.0, 48.0, 1.0);
        REQUIRE_THAT(f, WithinAbs(1.0, 0.001));
    }

    SECTION("Zoom decreases with distance") {
        double f10 = krema::parabolicZoomFactor(10.0, 48.0, 1.6);
        double f30 = krema::parabolicZoomFactor(30.0, 48.0, 1.6);
        double f60 = krema::parabolicZoomFactor(60.0, 48.0, 1.6);
        REQUIRE(f10 > f30);
        REQUIRE(f30 > f60);
    }

    SECTION("Symmetry: positive and negative distance give same result") {
        double fPos = krema::parabolicZoomFactor(25.0, 48.0, 1.6);
        double fNeg = krema::parabolicZoomFactor(-25.0, 48.0, 1.6);
        REQUIRE_THAT(fPos, WithinAbs(fNeg, 0.0001));
    }
}
```

### 1B. 표면 크기 계산

**현재 위치:** `src/shell/dockview.cpp` — `updateSize()`, `zoomOverflowHeight()`

**새 파일:** `src/utils/surfacegeometry.h`
```cpp
#pragma once
#include <algorithm>
#include <cmath>

namespace krema {

inline int zoomOverflowHeight(int iconSize, double maxZoomFactor) {
    return static_cast<int>(std::ceil(iconSize * (maxZoomFactor - 1.0)));
}

inline int surfaceHeight(int iconSize, int padding, double maxZoomFactor,
                         int tooltipReserve, int floatingPadding) {
    const int dockHeight = iconSize + padding * 2;
    const int overflowHeight = std::max(zoomOverflowHeight(iconSize, maxZoomFactor), tooltipReserve);
    return dockHeight + overflowHeight + floatingPadding;
}

inline int panelBarHeight(int iconSize, int padding, int floatingPadding) {
    return iconSize + padding * 2 + floatingPadding;
}

} // namespace krema
```

**DockView에서 이 유틸 호출로 전환:**
```cpp
// dockview.cpp
#include "utils/surfacegeometry.h"

void DockView::updateSize() {
    const int h = krema::surfaceHeight(
        m_settings->iconSize(), s_padding, m_settings->maxZoomFactor(),
        s_tooltipReserve, floatingPadding());
    // ...
}
```

**새 파일:** `tests/unit/test_surface_geometry.cpp`
```cpp
#include <catch2/catch_test_macros.hpp>
#include "utils/surfacegeometry.h"

TEST_CASE("Surface geometry calculations", "[geometry]") {
    SECTION("Zoom overflow height") {
        REQUIRE(krema::zoomOverflowHeight(48, 1.6) == 29);  // ceil(48 * 0.6)
        REQUIRE(krema::zoomOverflowHeight(48, 1.0) == 0);
        REQUIRE(krema::zoomOverflowHeight(48, 2.0) == 48);
    }

    SECTION("Surface height includes all components") {
        // iconSize=48, padding=8, zoom=1.6, tooltipReserve=36, floatingPad=8
        int h = krema::surfaceHeight(48, 8, 1.6, 36, 8);
        // dockHeight = 48 + 16 = 64
        // overflow = max(29, 36) = 36
        // total = 64 + 36 + 8 = 108
        REQUIRE(h == 108);
    }

    SECTION("Tooltip reserve wins when zoom overflow is small") {
        int h = krema::surfaceHeight(48, 8, 1.2, 36, 0);
        // overflow = ceil(48 * 0.2) = 10
        // max(10, 36) = 36
        // 64 + 36 + 0 = 100
        REQUIRE(h == 100);
    }

    SECTION("Panel bar height") {
        REQUIRE(krema::panelBarHeight(48, 8, 8) == 72);  // 48+16+8
        REQUIRE(krema::panelBarHeight(48, 8, 0) == 64);  // non-floating
    }
}
```

### 1C. 입력 영역 계산

**현재 위치:** `src/shell/dockvisibilitycontroller.cpp` — `applyInputRegion()`

이 함수는 `m_platform`, `m_dockWindow` 등에 의존하지만, QRegion 계산 로직은 순수 함수로 추출 가능.

**새 파일:** `src/utils/inputregion.h`
```cpp
#pragma once
#include <QRect>
#include <QRegion>

namespace krema {

struct InputRegionParams {
    int surfaceWidth;
    int surfaceHeight;
    int panelX;
    int panelY;
    int panelWidth;
    int panelHeight;
    int zoomOverflowHeight;
    bool visible;
    bool hovered;
    int triggerStripHeight = 4;
    int margin = 4;
};

QRegion computeDockInputRegion(const InputRegionParams &p);

struct DockScreenRectParams {
    int screenX;
    int screenY;
    int screenWidth;
    int screenHeight;
    int surfaceWidth;
    int surfaceHeight;
    int panelX;
    int panelWidth;
    int panelHeight;
    int floatingPadding;
    int edge;  // 0=Top, 1=Bottom, 2=Left, 3=Right
};

QRect computeDockScreenRect(const DockScreenRectParams &p);

} // namespace krema
```

구현은 기존 `applyInputRegion()`과 `dockScreenRect()`의 계산 로직을 그대로 추출.
`DockVisibilityController`에서는 이 유틸 호출로 전환.

**새 파일:** `tests/unit/test_input_region.cpp`
테스트 케이스:
- 숨겨진 상태: 트리거 스트립만 포함
- 표시+미호버: 패널 영역 + 마진
- 표시+호버: 패널 영역 + 줌 오버플로 영역
- 화면 좌표 계산: Bottom 엣지 기준

---

## Tier 2: 상태 머신 테스트

### 2A. DockVisibilityController

**새 파일:** `tests/unit/test_visibility_controller.cpp`

Mock DockPlatform:
```cpp
class MockPlatform : public krema::DockPlatform {
public:
    void setupWindow(QWindow *) override {}
    void setEdge(Edge) override {}
    void setExclusiveZone(int z) override { lastExclusiveZone = z; }
    void setMargin(int) override {}
    void setVisibilityMode(VisibilityMode) override {}
    void setSize(QSize s) override { lastSize = s; }
    void setInputRegion(QRegion r) override { lastRegion = r; }
    Edge edge() const override { return Edge::Bottom; }

    int lastExclusiveZone = 0;
    QSize lastSize;
    QRegion lastRegion;
};
```

테스트 시나리오:
- AlwaysVisible: 초기 상태 `dockVisible == true`
- AlwaysHidden: 초기 `false`, `setHovered(true)` → 타이머 후 `true`
- `setInteracting(true)` → 숨기기 방지
- `toggleVisibility()` 동작

**주의:** `DockVisibilityController`는 생성자에서 `TaskManager::TasksModel*`을 받지만, overlap 감지에만 사용. AlwaysVisible/AlwaysHidden 모드 테스트에는 nullptr 전달 가능 (overlap 검사 안 함).

---

## CMakeLists.txt 변경

**파일:** `tests/unit/CMakeLists.txt`

```cmake
add_executable(krema_unit_tests
    smoke_test.cpp
    test_zoom_calculator.cpp
    test_surface_geometry.cpp
    test_input_region.cpp
    test_visibility_controller.cpp
)

target_link_libraries(krema_unit_tests PRIVATE
    Catch2::Catch2WithMain
    krema_lib
)

target_include_directories(krema_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

catch_discover_tests(krema_unit_tests)
```

**파일:** `src/CMakeLists.txt`

소스 추가:
```cmake
utils/zoomcalculator.h
utils/surfacegeometry.h
utils/inputregion.h
utils/inputregion.cpp
```

---

## 검증

1. `cmake --build build` 성공
2. `ctest --test-dir build --output-on-failure` — 모든 테스트 통과
3. 기존 기능 리그레션 없음 (kwin-mcp로 확인)
