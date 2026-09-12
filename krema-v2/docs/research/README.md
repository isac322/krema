# Krema Research Station

> **Mission:** To serve as the "Black Box" of Krema’s engineering. Every feature, bug, and architectural shift has a documented origin story here.

## 1. Directory Structure
- `variables/`: The **Central Variable Registry**. Every sizing, spacing, and geometric unit (`_unit...`) must be defined here.
- `layers/`: The **Layer Registry**. Maps every visual component to its specific ID and code location (Rule 21).
- `trials/`: **Forensic logs** of experimental code (Successes and failures).
- `forensics/`: Root-cause analysis for any bug or desync that occurs during development.

## 2. Research Protocol
- **Non-Subtractive History:** Never delete research or trial logs. Always append new findings to the bottom.
- **Chronological Order:** New findings must be added at the end, ensuring we maintain a historical path of development.
- **Trial Documentation:** Every attempted fix for a bug MUST be documented as a "Trial," describing the strategy, the outcome, and the reason for success or failure.

## 3. Maintenance
- This folder is the "laboratory." It is not for user-facing documentation; it is for the maintainers and the developers to ensure 100% architectural traceability.
