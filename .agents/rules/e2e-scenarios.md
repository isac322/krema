---
description: "Apply when a change under src/ alters user-visible dock behavior and is about to be reported complete."
---

# E2E Scenarios

- A behavior change is complete only after its E2E scenarios pass, including regression scenarios for the affected area.
- After finishing a feature, list the changed `src/` files (`git diff --name-only HEAD`) and find the scenarios whose "Affected Files" include them via the mapping table in `tests/e2e/README.md`.
- If behavior changed, update those scenarios' steps and verification criteria.
- When adding a new `src/` file, add it to the "Affected Files" of the related scenarios.
