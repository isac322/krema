# Drag and Drop

## Features
- dnd-reorder: Drag dock items to reorder position
- dnd-pin-on-drop: Dropping an item at a new position pins it
- dnd-visual-feedback: Visual feedback during drag (placeholder, opacity change)
- dnd-file-drop: Drop file onto app icon to open with that app

## Affected Files
- src/qml/main.qml
- src/qml/DockItem.qml
- src/models/dockactions.h
- src/models/dockactions.cpp
- src/models/dockmodel.h
- src/models/dockmodel.cpp

---

## TC DND-001: Drag Reorder Dock Items

**Precondition:** Dock visible with 3+ items.
**Important:** Krema drag requires press-hold (300ms) then move (10px+).
Use `mouse_button_down` + sleep + `mouse_move` + `mouse_button_up` sequence,
NOT `mouse_drag` (which doesn't support hold delay).
**Steps:**
1. Show dock and note item order via `accessibility_tree`
2. `mouse_move` to first item center (screen coordinates)
3. Wait 300ms
4. `mouse_button_down(x, y)` — press on first item
5. Wait 400ms (hold timer 300ms + buffer)
6. `mouse_move(x + 80, y)` — drag to third item position (>10px threshold)
7. `screenshot` — verify drag ghost and drop indicator visible
8. `mouse_button_up(x + 80, y)` — drop
9. Wait 300ms
10. `accessibility_tree` — verify item order changed

**Expected:**
- During drag: ghost icon follows cursor (opacity 0.8), drop indicator line appears
- After drop: item moved to new position, order persists
- AT-SPI button order reflects new arrangement

**Verified in PoC:** Dolphin moved from position 1 to position 3.
AT-SPI confirmed order change: [Konsole, Kate, Dolphin, 시스템 설정, KCalc].

**Verification:** accessibility_tree (button order changed), screenshot (drag ghost visible during drag)

---

## TC DND-002: Reorder Persists After Restart

**Precondition:** DND-001 completed (items reordered).
**Steps:**
1. Note current item order via `screenshot`
2. Restart krema
3. Wait 2000ms for startup
4. `screenshot` — verify order preserved

**Expected:**
- Pin order saved to KConfig
- After restart, dock items appear in the reordered position

**Verification:** screenshot (same order after restart)

---

## TC DND-003: Drag Visual Feedback

**Precondition:** Dock with multiple items, visible.
**Steps:**
1. `mouse_button_down` on a dock item (screen coordinates)
2. Wait 400ms (hold timer)
3. `mouse_move` slowly to the right (>10px, step by step)
4. `screenshot` — capture mid-drag state
5. `mouse_button_up` at new position

**Expected:**
- Dragged item: opacity reduced at original position
- Ghost icon: follows cursor at 80% opacity (Image with source icon)
- Drop indicator: 2px wide highlight-colored line at insertion point
- Other items: base scale (zoom disabled during drag)

**Verified in PoC:** Ghost icon and reduced-opacity source item visible in screenshot.

**Verification:** screenshot (ghost icon + opacity change + drop indicator line)

---

## TC DND-004: Cancel Drag Returns to Original Position

**Precondition:** Dock with multiple items.
**Steps:**
1. `screenshot` — capture initial order
2. Start dragging an item
3. `keyboard_key Escape` or drag outside dock area
4. Wait 300ms
5. `screenshot` — verify original order restored

**Expected:**
- Item returns to original position
- No reorder occurs

**Verification:** screenshot (order unchanged)
