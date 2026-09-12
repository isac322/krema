# Plan: Panel Span & Alignment

## 1. Objective
Enable fixed, minimum, and full-screen dock lengths while allowing users to align the physical panel to the screen and the contents within the panel.

## 2. Panel Span Modes
- **Fit Content:** Proportional to content count.
- **Fixed Width:** Exact pixel value (e.g., 600px).
- **Minimum Width:** At least X pixels, grows if content overflows.
- **Fill Screen:** Spans the entire screen edge.

## 3. Panel-to-Screen Alignment
If the `Panel Span` is smaller than the total screen length, the Panel must align itself to the screen edge:
- **Start:** Panel anchors to the beginning of the primary axis (Left/Top).
- **Center:** Panel centers itself along the screen edge.
- **End:** Panel anchors to the end of the primary axis (Right/Bottom).

## 4. Content-to-Panel Alignment
If the Panel is wider than the total content (Islands + Gaps), the C++ engine applies one of the following alignments:
- **Start:** Islands cluster at the beginning of the Panel.
- **Center:** Islands cluster in the center of the Panel.
- **End:** Islands cluster at the end of the Panel.
- **Justify:** The `_islandGap` is dynamically expanded until the first and last Islands are flush with the Panel's internal padding, distributing the content across the entire Panel width.

## 5. Overflow Handling (Fixed Width Resolution)
If the user selects "Fixed Width" and the total content exceeds that width, the C++ engine enforces one of two user-selectable constraints:
- **Mode A (Squish):** The `targetContentSize` of all items is dynamically scaled down until the total width matches the fixed panel width perfectly.
- **Mode B (Arrow Scroll):** The items maintain their true size. The C++ engine exposes a `canvasOffsetX` to pan the icons. QML renders clickable `<` and `>` arrows at the edges.
