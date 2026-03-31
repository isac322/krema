# Headless GUI Testing for KDE Plasma 6 + Qt 6 + QML Dock

**Verified on**: Qt 6.11.0, KWin 6.6.3 (Plasma 6.5.5), Manjaro Linux

## Overview

Headless testing for a dock application has two fundamentally different layers:

1. **Pure QML/C++ unit tests** — logic, geometry, animations. No compositor needed. Use `QT_QPA_PLATFORM=offscreen`.
2. **Layer-shell integration tests** — surface creation, Wayland protocol, screen geometry. Requires a real Wayland compositor. Use `kwin_wayland --virtual`.

Do not mix these. Trying to create a layer-shell surface under `offscreen` will silently fail or crash at the Wayland socket connection step.

---

## 1. Qt Platform Plugins for Headless Testing

### `QT_QPA_PLATFORM=offscreen`

**Plugin**: `/usr/lib/qt6/plugins/platforms/libqoffscreen.so`

- Supports OpenGL / QRhi: yes (uses `QPlatformOpenGLContext`, RHI flush)
- QML rendering: yes, with software or GPU fallback
- No display, no Wayland socket required
- Suitable for: C++ unit tests, QML component tests, animation logic, non-Wayland UI tests
- Not suitable for: layer-shell surfaces, KWin window management, multi-screen

**Rendering backend**: By default, `offscreen` attempts OpenGL. In CI without GPU hardware:
```
export QSG_RHI_BACKEND=null      # Qt 6 Null RHI — disables real rendering
export QT_QPA_PLATFORM=offscreen
```

`QSG_RHI_BACKEND=null` uses `QRhi::Null` which accepts all draw calls without GPU. Items still layout and signal correctly.

### `QT_QPA_PLATFORM=minimal`

**Plugin**: `/usr/lib/qt6/plugins/platforms/libqminimal.so`

- Supports OpenGL: limited (only `createPlatformOffscreenSurface`)
- QML rendering: minimal, no real compositing
- Even more restricted than `offscreen`
- Use only for pure signal/slot or model tests with no visual rendering

### QRhi Null Backend Detail

`QRhi::Implementation` enum (from `/usr/include/qt6/QtGui/6.11.0/QtGui/rhi/qrhi.h`):
```cpp
enum Implementation {
    Null,       // no-op, accepts all calls — for testing
    Vulkan,
    OpenGLES2,
    D3D11,
    Metal,
    D3D12
};
```

Control via environment:
- `QSG_RHI_BACKEND=null` — force Qt Quick to use Null RHI
- `QSG_RHI_BACKEND=vulkan` — force Vulkan (useful in CI with `lavapipe` Mesa software Vulkan)

---

## 2. KWin Virtual Compositor

### `kwin_wayland --virtual`

KWin 6 supports a virtual framebuffer mode that creates a real Wayland compositor without any physical display or DRM device.

```
kwin_wayland --virtual --no-lockscreen \
    --width 1920 --height 1080 \
    --socket wayland-test-0
```

**What it provides**:
- Full Wayland compositor including `zwlr_layer_shell_v1` (layer-shell)
- Real Wayland socket file at `$XDG_RUNTIME_DIR/<socket-name>`
- KWin D-Bus interface (`org.kde.KWin`) at `/KWin`
- Virtual outputs with configurable size
- AT-SPI accessibility bus (when started alongside `at-spi-bus-launcher`)

**Confirmed layer-shell support**: Qt ships `liblayer-shell.so` Wayland shell integration plugin at `/usr/lib/qt6/plugins/wayland-shell-integration/liblayer-shell.so`. This plugin connects to the compositor's `zwlr_layer_shell_v1` interface. KWin's virtual backend exposes all the same Wayland protocols as the real backend.

KWin virtual backend source includes `LayerShellV1Integration` / `LayerShellV1Window` (headers at `/usr/include/kwin/layershellv1window.h`, `/usr/include/kwin/layershellv1integration.h`).

### Session Setup Pattern

From the verified kwin-mcp session management (`/home/bhyoo/projects/python/kwin-mcp/src/kwin_mcp/session.py`):

```bash
dbus-run-session bash -c '
  echo "DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS"

  # Start AT-SPI accessibility bus
  /usr/lib/at-spi-bus-launcher --launch-immediately &
  AT_SPI_PID=$!
  sleep 0.2

  # Pre-register the Wayland socket for D-Bus activation
  dbus-update-activation-environment WAYLAND_DISPLAY=wayland-test QT_QPA_PLATFORM=wayland

  # Start KWin WITHOUT WAYLAND_DISPLAY (must not try to nest)
  env -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM \
      KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
      KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 \
      kwin_wayland --virtual --no-lockscreen \
      --width 1920 --height 1080 \
      --socket wayland-test &
  KWIN_PID=$!

  # Wait for socket to appear
  while [ ! -e "$XDG_RUNTIME_DIR/wayland-test" ]; do sleep 0.1; done
  sleep 0.3
  echo "READY"
  wait $KWIN_PID
'
```

Then launch Krema in the virtual session:
```bash
WAYLAND_DISPLAY=wayland-test \
QT_QPA_PLATFORM=wayland \
DBUS_SESSION_BUS_ADDRESS=<address-from-above> \
KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
./krema
```

### Required Environment Variables

| Variable | Value | Purpose |
|----------|-------|---------|
| `WAYLAND_DISPLAY` | socket name | Connect app to virtual compositor |
| `QT_QPA_PLATFORM` | `wayland` | Use Wayland Qt backend (not offscreen) |
| `KWIN_WAYLAND_NO_PERMISSION_CHECKS` | `1` | Allow layer-shell without portal |
| `KWIN_SCREENSHOT_NO_PERMISSION_CHECKS` | `1` | Allow KWin screenshot D-Bus without portal |
| `KDE_FULL_SESSION` | `true` | Activate KDE-specific code paths |
| `KDE_SESSION_VERSION` | `6` | Signal KDE 6 session |
| `XDG_SESSION_TYPE` | `wayland` | Session type hint |
| `XDG_CURRENT_DESKTOP` | `KDE` | Desktop environment hint |
| `ATSPI_DBUS_IMPLEMENTATION` | `dbus-daemon` | Prevent AT-SPI from reusing host bus |

**Critical**: `WAYLAND_DISPLAY` must be **unset** when starting KWin with `--virtual`. KWin must not try to nest inside another compositor. Only set it for client apps launched after KWin is ready.

---

## 3. Qt Test Framework for QML

### C++ Side: `QtQuickTest`

**Header**: `/usr/include/qt6/QtQuickTest/quicktest.h`
**Library**: `libQt6QuickTest.so`
**CMake**: `find_package(Qt6 COMPONENTS QuickTest)`

Key entry points:
```cpp
// quicktest.h
int quick_test_main(int argc, char **argv, const char *name, const char *sourceDir);
int quick_test_main_with_setup(int argc, char **argv, const char *name,
                               const char *sourceDir, QObject *setup);

// Macros (for main() definition)
QUICK_TEST_MAIN(name)
QUICK_TEST_MAIN_WITH_SETUP(name, QuickTestSetupClass)

// Polling helpers
bool QQuickTest::qWaitForPolish(const QQuickItem *item, int timeout = 5000);
bool QQuickTest::qWaitForPolish(const QQuickWindow *window, int timeout = 5000);
```

### QML Side: `TestCase` and `SignalSpy`

Import: `import QtTest 1.2`
Source: `/usr/lib/qt6/qml/QtTest/TestCase.qml`, `SignalSpy.qml`

```qml
import QtQuick 2.0
import QtTest 1.2

TestCase {
    name: "MyTest"
    when: windowShown

    function test_something() {
        compare(2 + 2, 4)
        verify(someItem.visible)
    }

    // Data-driven test
    function test_table_data() {
        return [
            { tag: "case1", input: 1, expected: 2 },
            { tag: "case2", input: 3, expected: 6 },
        ]
    }
    function test_table(data) {
        compare(data.input * 2, data.expected)
    }

    // Benchmarks: prefix with "benchmark_"
    function benchmark_once_create() { ... }
}
```

Available assertions in `TestCase`:
- `verify(expr)`, `compare(a, b)`, `fuzzyCompare(a, b, delta)`
- `tryVerify(expr, timeout)`, `tryCompare(obj, prop, val, timeout)`
- `fail(msg)`, `skip(msg)`, `expectFail(tag, msg)`

Input simulation (requires `when: windowShown`):
- `mouseClick(item, x, y)`, `mousePress`, `mouseRelease`, `mouseMove`, `mouseDClick`
- `keyClick(key)`, `keyPress`, `keyRelease`
- `keySequence(seq)` — Qt 6 only

### `SignalSpy` QML Type

```qml
SignalSpy {
    id: spy
    target: someObject
    signalName: "clicked"
}

// In test:
compare(spy.count, 0)
someObject.click()
compare(spy.count, 1)
spy.wait(1000)  // block until signal or timeout
```

### C++ `QSignalSpy`

**Header**: `/usr/include/qt6/QtTest/qsignalspy.h`

```cpp
QSignalSpy spy(myObject, &MyClass::mySignal);
myObject->doSomething();
QCOMPARE(spy.count(), 1);
QVERIFY(spy.wait(5000));  // blocks until signal emitted (std::chrono overload in Qt 6)
```

Key method: `bool wait(std::chrono::milliseconds timeout = std::chrono::seconds{5})`

### Input Simulation Utilities (C++)

**Header**: `/usr/include/qt6/QtTest/qtestmouse.h`, `qtestkeyboard.h`

```cpp
// Mouse (bypasses platform plugin, works with offscreen)
QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
QTest::mouseRelease(&window, Qt::LeftButton);
QTest::mouseMove(&window, QPoint(100, 100));

// Keyboard
QTest::keyClick(&window, Qt::Key_Return);
QTest::keyPress(&window, Qt::Key_A);
QTest::keyRelease(&window, Qt::Key_A);
```

These use `QWindowSystemInterface::handleMouseEvent` internally, bypassing the platform plugin layer — they work under `offscreen` and `minimal`.

### Advanced: QQuickTest View Utils

**Header**: `/usr/include/qt6/QtQuickTestUtils/6.11.0/QtQuickTestUtils/private/viewtestutils_p.h`

```cpp
// Create a QQuickView for testing
QQuickView *view = QQuickViewTestUtils::createView();

// Initialize and show a QML URL
QByteArray err;
bool ok = QQuickTest::initView(*view, QUrl("qrc:/my_component.qml"), true, &err);
bool shown = QQuickTest::showView(*view, QUrl("qrc:/my_component.qml"));

// Pointer input helpers (Qt 6.x)
QQuickTest::pointerPress(device, window, pointId, QPoint(50, 50));
QQuickTest::pointerMove(device, window, pointId, QPoint(60, 60));
QQuickTest::pointerRelease(device, window, pointId, QPoint(60, 60));
```

### `qmltestrunner` Command-Line Tool

**Binary**: `/usr/bin/qmltestrunner`

Run QML tests without writing a C++ main:
```bash
QT_QPA_PLATFORM=offscreen qmltestrunner -input tests/qml/
```

Supports JUnit XML output for CI:
```bash
qmltestrunner -input tests/qml/ -o results.xml,junitxml
```

---

## 4. Wayland-Specific Challenges

### Layer-Shell Under `offscreen`

Layer-shell surfaces **cannot** be created when `QT_QPA_PLATFORM=offscreen`. The layer-shell Qt plugin (`liblayer-shell.so`) requires a live Wayland connection to `zwlr_layer_shell_v1`. Without a compositor, `LayerShellQt::Window::get(window)` will have no effect and the window will fall back to a regular Qt window.

**Implication for Krema tests**: Any test that exercises `DockView` or `LayerShellQt` directly must run in a `kwin_wayland --virtual` session.

### Screen Geometry Mocking

Under `kwin_wayland --virtual`, the virtual output reports geometry based on `--width` / `--height`. There is no way to add multiple outputs at runtime through the public API (the `OutputBackend::createVirtualOutput` exists at the C++ API level but is not exposed via command-line or D-Bus).

For multi-monitor tests: start KWin with `--output-count N` to create N virtual outputs side by side.

### Input Simulation Without Hardware

**Under `offscreen`/`minimal`**: `QWindowSystemInterface` methods (used by `QTest::mouseClick` etc.) inject events at the QPA level, bypassing any real hardware. Works correctly headless.

**Under `kwin_wayland --virtual`**: Input goes through the full Wayland input protocol. Use `ydotool` (uinput-based), the KWin D-Bus `org.kde.kwin.VirtualKeyboard` interface, or inject at the Wayland level with `wl_seat`. For kwin-mcp-style testing, AT-SPI action calls trigger interaction without needing raw input injection.

---

## 5. How KDE / Plasma Tests

### KWin Test Infrastructure (Source-Level)

KWin's own test suite uses a dedicated headless test setup that is **not** available as a public binary:
- `kwin_wayland_test` (internal binary, built only when `-DBUILD_TESTING=ON`)
- Custom `WaylandTestBase` class that sets up a mock compositor in-process
- Not available in installed packages — must build KWin from source to access

The installed KWin (`/usr/lib/libkwin.so`) exposes backend abstraction headers at `/usr/include/kwin/core/`, including `OutputBackend`, `InputBackend`, `NoopSession` — but these are for KWin plugin developers, not external test suites.

### kwin-mcp Pattern (Verified Working)

The pattern used by `kwin-mcp` and confirmed to work with Krema:

1. `dbus-run-session` creates an isolated D-Bus session bus
2. `at-spi-bus-launcher` starts the accessibility bus
3. `kwin_wayland --virtual` provides full compositor + layer-shell
4. App connects via `WAYLAND_DISPLAY=<socket> QT_QPA_PLATFORM=wayland`
5. AT-SPI2 accessibility tree gives structural UI access
6. KWin D-Bus API provides compositor-level control

### Plasma Desktop Testing

Plasma's `plasma-desktop` test suites (in KDE source tree) use:
- `KWin` nested in an X11 session (via `DISPLAY=:99 Xvfb :99 &`) for X11 CI
- `kwin_wayland --wayland-display` for Wayland CI running nested under another compositor
- For CI without display: `kwin_wayland --virtual` (same as kwin-mcp)

---

## 6. CI Integration (GitHub Actions)

### Pure Unit Tests (No Compositor Required)

```yaml
- name: Run unit tests
  env:
    QT_QPA_PLATFORM: offscreen
    QSG_RHI_BACKEND: null
    QT_LOGGING_RULES: "qt.rhi.*=false"
  run: ctest --test-dir build -R "unit" --output-on-failure
```

No special packages needed beyond the Qt6 build dependencies.

### QML Component Tests (No Compositor)

```yaml
- name: Install Qt test deps
  run: |
    sudo apt-get install -y qt6-base-dev qt6-declarative-dev \
      libqt6quicktest6-dev  # or equivalent package name

- name: Run QML tests
  env:
    QT_QPA_PLATFORM: offscreen
    QSG_RHI_BACKEND: null
  run: |
    qmltestrunner -input tests/qml/ -o results.xml,junitxml
```

### Integration Tests with Layer-Shell (Compositor Required)

This is the harder case for CI. Options:

**Option A: `kwin_wayland --virtual` in GitHub Actions**

GitHub Actions `ubuntu-latest` runners have a full desktop stack available. However, KWin requires specific KDE runtime packages:

```yaml
- name: Install KWin and dependencies
  run: |
    sudo apt-get install -y \
      kwin-wayland \
      libkf6windowsystem-dev \
      plasma-workspace \
      at-spi2-core \
      dbus-daemon

- name: Run integration tests
  run: |
    dbus-run-session bash -c '
      /usr/lib/at-spi-bus-launcher --launch-immediately &
      sleep 0.3
      env -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM \
        KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
        kwin_wayland --virtual --no-lockscreen \
        --width 1920 --height 1080 \
        --socket wayland-ci &
      KWIN_PID=$!
      while [ ! -e "$XDG_RUNTIME_DIR/wayland-ci" ]; do sleep 0.1; done
      sleep 0.5

      WAYLAND_DISPLAY=wayland-ci \
      QT_QPA_PLATFORM=wayland \
      DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS \
      KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
      ./build/tests/krema_integration_tests

      wait $KWIN_PID
    '
```

**Option B: Reuse kwin-mcp**

If `kwin-mcp` is available as a Python package in the CI environment, it handles all the session setup complexity.

**Key constraint**: GitHub Actions runners do not have DRI devices visible (`/dev/dri/` is empty in sandboxed environments). KWin `--virtual` avoids DRM and uses a software framebuffer, so this is not a blocker.

### Required Packages (Arch Linux / Manjaro)

On Arch-based systems (AUR CI, local development):
```
pacman -S qt6-base qt6-declarative qt6-wayland
pacman -S kwin plasma-wayland-protocols
pacman -S at-spi2-core dbus
```

The `kwin` package (6.6.3 on this system) includes `kwin_wayland` binary with `--virtual` support.

### Environment Variable Reference for CI

```bash
# Headless unit / QML tests (no compositor)
QT_QPA_PLATFORM=offscreen
QSG_RHI_BACKEND=null
QT_LOGGING_RULES=qt.rhi.*=false;qt.scenegraph.*=false

# Layer-shell integration tests (kwin --virtual)
WAYLAND_DISPLAY=wayland-ci
QT_QPA_PLATFORM=wayland
KDE_FULL_SESSION=true
KDE_SESSION_VERSION=6
XDG_SESSION_TYPE=wayland
XDG_CURRENT_DESKTOP=KDE
KWIN_WAYLAND_NO_PERMISSION_CHECKS=1
KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
QT_ACCESSIBILITY=1
ATSPI_DBUS_IMPLEMENTATION=dbus-daemon
```

---

## 7. CMake Integration

```cmake
# Unit tests (pure C++/QML, no compositor)
find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)

add_executable(krema_qml_tests tests/qml/main.cpp)
target_link_libraries(krema_qml_tests PRIVATE
    Qt6::Test
    Qt6::QuickTest
    Qt6::Quick
    krema_lib
)

# Define source dir for QML test files
target_compile_definitions(krema_qml_tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/qml"
)

add_test(NAME KremaQmlTests
    COMMAND krema_qml_tests
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(KremaQmlTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=null"
)
```

---

## 8. Applicability to Krema

| Test Type | Platform | Compositor | Use For |
|-----------|----------|------------|---------|
| C++ zoom/geometry math | `offscreen` | None | `ZoomCalculator`, surface geometry, input region |
| QML component layout | `offscreen` + Null RHI | None | Individual dock items, settings pages |
| QML animations | `offscreen` | None | Timing checks, state transitions |
| Layer-shell surface creation | `wayland` | kwin --virtual | DockView construction |
| KWin task model integration | `wayland` | kwin --virtual | TasksModel, window grouping |
| Full dock e2e | `wayland` | kwin --virtual | kwin-mcp test scenarios |

### Current Krema Test Infrastructure

Existing Catch2 unit tests (`tests/unit/`) cover pure C++ math:
- `test_zoom_calculator.cpp`
- `test_surface_geometry.cpp`
- `test_input_region.cpp`

These already run under `QT_QPA_PLATFORM=offscreen` (no display needed for pure math).

The e2e scenarios in `tests/e2e/` are designed for kwin-mcp and require the full virtual compositor setup.

---

## Notes

- Qt 6.11.0 (installed) ships `QRhi::Null` — `QSG_RHI_BACKEND=null` is fully supported.
- `kwin_wayland --virtual` does not require D-Bus session bus but the KWin D-Bus interface (`org.kde.KWin`) is only available when started inside one.
- `dbus-run-session` is the recommended way to create an isolated D-Bus environment for testing — it prevents contamination of the host D-Bus session.
- KWin `--virtual` mode and the layer-shell integration have been verified working together by kwin-mcp (see `/home/bhyoo/projects/python/kwin-mcp/src/kwin_mcp/session.py`).
- `QQuickTest::qWaitForPolish()` replaces the deprecated `qWaitForItemPolished()` (removed in Qt 7).
- `QSignalSpy::wait()` takes `std::chrono::milliseconds` in Qt 6 (also accepts `int` via overload for compatibility).
