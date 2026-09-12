# Krema AI Governance: Compliance Triggers

> **Core Mandate:** All agents must adhere strictly to `ARCHITECTURE Mandate.md`.

## 1. Compliance Protocols
- **Mathematical Integrity (Rule 1-9):** Before proposing any geometric or layout change, verify compliance against the 'Mathematical & Geometric Integrity' section of `ARCHITECTURE Mandate.md`.
- **Layer & Variable Tracing (Rule 21):** Before modifying or adding any UI layer or layout variable, you MUST check the registries in `docs/research/layers/` and `docs/research/variables/`. Update the registries *before* applying the code change.
- **Observability (Rule 13):** Every new functional module must implement the telemetry interface and be registered in the diagnostic registry.
- **Surgical Edits (Rule 20):** Every code change MUST be surgical, verified, and logged in the appropriate `docs/research/` folder.
- **Commit Discipline:** Never commit code without first updating the project's coordination files (`docs/`, `ROADMAP.md`).
