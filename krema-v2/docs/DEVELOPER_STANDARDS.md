# Krema: Developer Documentation Standards

> **Goal:** Ensure every line of code is traceable, debuggable, and maintainable.

## 1. Traceability Rules
- **Variable Registry:** All layout, sizing, and geometric variables MUST be documented in `docs/research/variables/` with Name, Owner, Purpose, and Consumers.
- **Layer Registry:** All visual layers and logical regions MUST be registered in `docs/research/layers/` with unique IDs and trace references in the code (Rule 21).
- **Chronological History:** All `research/` and `bugs_report.md` documents MUST be maintained in chronological order (Oldest to Newest), with updates appended to the bottom.

## 2. Technical Writing Standards
- **Precision:** Use technical terminology (e.g., "Wayland surface protocol," "GPU-bound shader") instead of marketing buzzwords.
- **Architectural Intent:** Documents MUST describe the "Why" (mathematical/architectural intent) before the "How" (implementation).
- **English-Only:** All files, logs, and artifacts MUST be in English.

## 3. Visual & Code Documentation
- **Diagrams:** Diagrams and flowcharts must be text-based (e.g., Mermaid.js or ASCII) to be source-controllable.
- **Code Comments:** Every visual layer and logical region MUST be marked with a standardized header: `// --- Layer #: [Name] ---`.
