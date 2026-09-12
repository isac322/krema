# Plan: Ultimate Logging & Debugging System

## 1. Background & Motivation
The user requires absolute, "God-mode" visibility into the internal workings of the Krema dock—covering every pixel, animation tick, input region, and Wayland signal. The current `DebugManager` is too basic, lacking clear visual formatting, QML origin tracking, and file-based persistence. Crucially, the system must incur **0% performance overhead** when disabled.

## 2. Scope & Impact
- **Affected Components:** `src/app/application.cpp`, `src/utils/debugmanager.h/cpp`, `src/main.cpp`, and QML files (`AppIcon.qml`, `main.qml`, etc.).
- **Impact:** Introduces a heavily structured, color-coded, and categorical terminal output. Adds automatic file logging. Ensures heavy QML string concatenations are gated behind boolean checks.

## 3. Proposed Solution

### A. Zero-Overhead Constraint Implementation
- **C++:** Continue using `qCDebug()` macros which inherently short-circuit when disabled.
- **QML:** Add `Q_PROPERTY(bool geomEnabled READ geomEnabled CONSTANT)` to `DebugManager`. QML will use `if (KremaDebug.geomEnabled) { KremaDebug.geom(...) }` to bypass JavaScript template string evaluation overhead.

### B. Categorical Organization & Color Coding
Rewrite `kremaLogHandler` in `application.cpp` to format logs as:
`[TIME] [CATEGORY] [ORIGIN] Message`

**Categories & Colors:**
- 🟦 `[GEOM]` (Cyan): Pixel math, panel sizing, interaction boundaries.
- 🟨 `[INPUT]` (Yellow): Wayland regions, `MouseArea` hover states.
- 🟪 `[ANIM]` (Magenta): Zoom physics, transition values (High-volume).
- 🟩 `[SHELL]` (Green): Wayland LayerShell, multimonitor signals.
- 🟧 `[MODEL]` (Yellow/Orange): IPC data, window parsing.
- 🟥 `[SHADER]` (Red): Blur, shadow parameters.
- ⬜ `[APP]` (Bold White): Core lifecycle, initializations.

**Origin Tracking:**
- C++ logs: `[C++]`
- QML logs: `[QML AppIcon.qml:45]` (Extracted from `QMessageLogContext`)

### C. CLI Flag Architecture
Parse arguments in `main.cpp` using `QCommandLineParser`:
- `--debug-geom`
- `--debug-input`
- `--debug-anim`
- `--debug-model`
- `--debug-shell`
- `--debug-shader`
- `--debug-all`
- `--log-to-file` (Tees output to `~/.local/state/krema/logs/krema-latest.log` with ANSI codes stripped).

### D. Implementation Steps
1. **Refactor DebugManager:** Add new categories (`Shader`), boolean properties for QML gating, and file-writing capabilities (stripping ANSI).
2. **Update main.cpp:** Implement `QCommandLineParser` to intercept flags and configure `DebugManager` and `QLoggingCategory` rules.
3. **Rewrite Log Handler:** Implement the column-based, color-coded string formatting in `kremaLogHandler`.
4. **Update QML Calls:** Audit existing QML `KremaDebug` calls to use the `if (KremaDebug.XEnabled)` gating pattern.

## 4. Verification & Testing
- Run `just run --debug-geom` and ensure only Geometry logs appear, formatted cleanly.
- Run `just run --debug-all --log-to-file` and verify the `krema-latest.log` is created and readable without terminal color garbage.
- Ensure terminal output clearly distinguishes between `[C++]` and `[QML]`.