---
description: "Apply when editing dock QML: hover/mouse tracking, dock items, icons, or Repeater models."
---

# Dock QML UI

## Mouse / input hierarchy
- Mouse tracking has ONE authoritative level: the panel, not individual items.
- Hover detection MUST check every dimension (X and Y), never a single axis.
- `DockItem.hoverEnabled` must be `false`; the panel handles all hover.

## Icon resources
- `sourceSize` MUST use the maximum zoom resolution: `iconSize * maxZoomFactor`.

## Repeater model lifecycle
- Assigning a new JS array to `Repeater.model` destroys and recreates every delegate.
- Delegates holding GPU resources (PipeWire streams) use a `ListModel` updated with `set()` / `append()` / `remove()`.
- Replacing a JS array model is acceptable only when delegates hold no expensive resources.
