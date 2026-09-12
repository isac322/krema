@.claude/work-state.md
@.claude/rules/token-efficiency.md

# Krema Development Rules

## 1. Project Identity & Vision
Krema is a dedicated dock for **KDE Plasma 6 (Wayland)**. It is the "spiritual successor" to Latte Dock, focused on performance, visual smoothness, and deep KDE integration.

## 2. Session Protocol (Mandatory)
Before answering any prompts, the AI must:
1. **Check Work State:** Read `.claude/work-state.md` to identify current tasks and known issues.
2. **Verify Progress:** Check `ROADMAP.md` for the current milestone (marked with ⬅️).
3. **On Session Close:** Update `work-state.md` and `ROADMAP.md` with progress.
4. **Transparency & Proposal Phase:** Before any file edit, the AI must provide a "Refactor Proposal":
   - **Identified Logic:** What specific lines look "optimizable"?
   - **Functional Assessment:** What does this logic currently achieve (e.g., handles separators, fixes Electron icons)?
   - **Optimization Strategy:** How will the new code preserve this EXACT behavior while being more efficient?
   - **Confirmation:** Wait for user approval before applying.

## 3. Development Workflow
- **Never guess KDE APIs.** If an API is unknown, verify it via `/usr/include/` headers.
- **Scale Check:** - Small: 1-2 files, existing APIs.
  - Medium: New KDE APIs or 3+ files (Requires header verification).
  - Large: New modules or 3+ new files (Requires architectural audit).
- **Testing:** Use **kwin-mcp** for scenario execution. Work is "Done" when all scenarios pass and `git diff` shows no regression.

## 4. Technical Standards
- **Stack:** C++23, Qt 6, KDE Frameworks 6 (KF6).
- **Performance:** 60fps mandatory. Use GPU-native paths (PipeWire DMA-BUF, QRhi). Avoid CPU→GPU copies.
- **KDE Integration:** Prefer KDE APIs. Use `Kirigami.Theme` and `Kirigami.Units`. No hardcoded colors/sizes.
- **Architecture:** - **Functional Preservation:** Optimization must be "Non-Subtractive."
  - **Respect the "Mess":** If code handles a specific app (Steam, Neshi), it is a "Functional Constraint," not "Technical Debt." Refactor syntax, but never the logic branch.

## 5. Wayland & Layer-Shell Rules
- **Surfaces:** `surfaceHeight` must account for animation overflow (zoom/bounce).
- **Input Region:** Must be explicitly set; an empty QRegion accepts ALL input (bad).
- **Lifecycle:** ALWAYS null-check `screen()` (nullptr on virtual compositors).

## 6. SEO & Strategy
- **Keywords:** latte dock alternative, kde plasma 6 dock, kde dock wayland.
- **Latte Dock:** Refer to Krema as a "spiritual successor"—never a "fork" or "clone."

## 7. Anti-Patterns to Prevent
- **JS Timers:** Do not use for business logic; use Qt/KDE signals instead.
- **JS Array Assignment:** Do not replace `Repeater.model` with raw JS arrays (destroys delegates).
- **Hitbox Cumulative Math:** Never use `currentEdge += itemSize` for hovers.
- **The Ghost Grid Rule:** Always use "Center-Distance Math" for hitboxes to preserve gaps and prevent binding loops.

## 8. Functional Invariants (Mandatory Preservation)
- **State Stability (The Safe Floor):** No optimization may allow UI-critical variables (width, height, scale, overflow) to reach 0 or null unless destroying the dock.
- **Heterogeneous Model Logic:** Docks are not uniform. Logic must account for separators and indicators. Assuming all items are identical Icons is a BUG.
- **Verified Feature Integrity:** Working features (Steam icons, dash indicators) must be flagged in the "Proposal Phase" if their code path changes.
