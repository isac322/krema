# Future Plan: Panel Length & Alignment

## 1. Objective
Support fixed, minimum, and full-screen dock widths along the primary axis.

## 2. Logic
- **Modes:** Fit Content, Fixed, Minimum, Fill Screen.
- **Math:** Update `_actualContentLength` to use `switch` based on `PanelLengthMode`.
- **Alignment:** icons center/start/end within the extended panel.

## 3. UI
Add "Length & Alignment" section with mode selector and alignment picker.
