Review current code changes against CLAUDE.md anti-patterns using the arch-auditor agent:

1. Launch the `arch-auditor` agent with this task:
   "Audit the current code changes in the Krema project. Run `git diff --cached` first; if empty, run `git diff HEAD`. Check all changes against the 5 anti-pattern categories (QML-C++ State Boundary, Wayland Surface Lifecycle, Surface Sizing, Mouse/Input Hierarchy, Icon Resources) and Architecture Decisions from CLAUDE.md. Also cross-reference with docs/kde/lessons-learned.md. Report all violations or confirm clean."

2. Report the agent's findings to the user
3. If violations found: suggest specific fixes for each
4. If clean: confirm all checks passed
