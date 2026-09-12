# Plan: Fix KConfig PanelHeight Limit

## 1. Objective
Fix the UI Blindness violation (Rule 6) occurring when scaling large icons. The `PanelHeight` setting in `krema.kcfg` has a hardcoded maximum of `150`. The mathematical envelope for large icons (e.g., 96px) can exceed 200px. This causes KConfig to clamp the value, creating a dead zone in the UI slider. We must raise this hard limit so the QML math can govern the true limits.

## 2. Proposed Changes
- **`src/config/krema.kcfg`**:
  - Locate `PanelHeight` entry.
  - Change `<max>150</max>` to `<max>300</max>`.

## 3. Execution
- Exit plan mode.
- Use `replace` to update `krema.kcfg`.
