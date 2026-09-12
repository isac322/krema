# Plan: Panel Extent & Alignment

## 1. Objective
Enable fixed, minimum, and full-screen dock lengths (Rule 8) while allowing users to align the icon row (Start, Center, End).

## 2. Modes (The "Limit" Logic)
- **Fit Content:** Proportional to icon count.
- **Fixed Extent:** Exact pixel value (e.g., 600px).
- **Minimum Extent:** At least X pixels, grows if icons overflow.
- **Fill Screen:** Spans the entire screen edge.

## 3. Alignment
- If the panel extent is larger than the content, `dockRow` follows the `PanelAlignment` setting (Start/Center/End).
