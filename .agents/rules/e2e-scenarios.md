---
description: "Apply when a change under src/ alters user-visible dock behavior and is about to be reported complete."
---

# E2E Scenarios

- The scenario specs in `tests/e2e/scenarios/` are the oracle for E2E behavior; each TC's `**Automated:**` lines name the tests that cover it.
- A behavior change is complete only after its E2E scenarios pass, including regression scenarios for the affected area. `tests/appium/README.md` has the coverage matrix and how to run the suite (`tests/appium/run-e2e.sh`); some TCs are covered by `tests/kwin` ctests instead.
- After finishing a feature, list the changed `src/` files (`git diff --name-only HEAD`) and find the scenarios whose "Affected Files" include them via the mapping table in `tests/e2e/README.md`.
- If behavior changed, update those scenarios' steps, expected results, and `**Automated:**` lines, and the matching tests in `tests/appium/`.
- When adding a new `src/` file, add it to the "Affected Files" of the related scenarios and to the mapping table.
