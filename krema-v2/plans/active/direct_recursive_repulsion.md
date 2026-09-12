# Plan: Direct Recursive Repulsion (Rule 15 Alignment)

## 1. Objective
Eliminate the 1-2 frame "Repulsion Lag" by bypassing the standard QML `Flow` layout engine. Implement a **Direct Binding Chain** where each icon's coordinate is mathematically bound to its predecessor, ensuring zoom and push occur in the same frame.

## 2. Technical Strategy
- **Container Transition:** Replace `dockRow` (`Flow`) with a raw `Item`.
- **Recursive Positioning:** 
  - Each `AppIcon` instance will calculate its position based on the preceding item in the `Repeater`.
  - Formula: `x[i] = (i == 0) ? 0 : x[i-1] + width[i-1] + spacing`.
- **Total Content Size:** The `dockRow`'s `implicitWidth/Height` will be bound to the extent of the final icon.
- **Centering Preservation:** Since `implicitWidth` updates instantly via the binding chain, the `dockPanel` centering will remain perfectly synced.

## 3. Proposed Changes
### src/qml/main.qml
- Change `dockRow` from `Flow` to `Item`.
- Implement a helper function or property chain to resolve sibling positions.
- Update `dockRow.implicitWidth/Height` to follow the binding chain.

### src/qml/AppIcon.qml
- No internal changes required (it already uses `currentScale` for width/height).

## 4. Execution Steps
1. Exit Plan Mode.
2. Surgically refactor `main.qml` to use raw `Item` for `dockRow`.
3. Implement the recursive binding logic using QML's `dockRepeater.itemAt(index-1)` pattern.
4. Verify repulsion speed with `--debug-hit`.

## 5. Verification
- Run the app and move the mouse rapidly.
- Verify that icons and their neighbors move **simultaneously**.
- Ensure no overlaps occur during the wave.
