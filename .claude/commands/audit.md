Review current code changes against the anti-pattern rules in `.agents/rules/` using the arch-auditor agent:

1. Launch the `arch-auditor` agent with this task:
   "Audit the current code changes in the Krema project. Run `git diff --cached` first; if empty, run `git diff HEAD`. Check all changes against the anti-pattern rules in `.agents/rules/` (`wayland-surfaces.md`, `qml-dock-ui.md`, `kde-ui-and-config.md`, `async-state.md`, `performance.md`). Also cross-reference with docs/kde/lessons-learned.md. Report all violations or confirm clean."

2. Report the agent's findings to the user
3. If violations found: suggest specific fixes for each
4. If clean: confirm all checks passed
