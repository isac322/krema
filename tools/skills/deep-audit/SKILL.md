---
name: deep-audit
description: Exhaustive auditing of signals, configuration flow, and build artifacts. Use when multiple fix attempts fail to produce any visible change, indicating a fundamental disconnection in the signal chain or build process.
---

# Deep Audit Protocol

This skill provides a rigorous, multi-layered workflow to identify why code changes are failing to take effect.

## Phase 1: Build & Artifact Verification
1.  **Checksum Audit:** Verify that the binary in `build/dev/bin/krema` is actually being updated.
2.  **Symbol Search:** Use `strings` or `nm` on the binary to confirm that new strings or C++ functions are present in the compiled code.
3.  **Resource Audit:** Check if QML changes are being picked up by the `qrc` system.

## Phase 2: Signal Chain Trace
1.  **C++ Bridge Audit:** Verify that `connect()` calls are actually succeeding (check return values).
2.  **Reactivity Pulse:** Use `console.log` with unique IDs in QML to track property propagation from Settings -> Singleton -> Dock.
3.  **Lifecycle Audit:** Confirm if the `DockSettings` singleton instance is shared or duplicated between the Dock and Settings Loader.

## Phase 3: Configuration Persistence
1.  **KConfig Watcher:** Run `tail -f ~/.config/kremarc` while moving sliders to see if disk writes are actually happening.
2.  **Fallback Conflict:** Audit `ScreenSettings::readWithFallback` for logic that might be discarding valid values (like 1.0).

## Diagnostic Tools
- `just run --debug-geom`: Use for geometry-specific issues.
- `audit_signals.py`: (Script) Automated grep-based signal mapping across C++ and QML.
