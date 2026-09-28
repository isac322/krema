---
description: "Apply when creating, sizing, or positioning layer-shell/Wayland surfaces, or setting input regions, hit boxes, or keyboard interactivity."
---

# Wayland Surfaces

Background and bug histories: `docs/kde/lessons-learned.md`.

## Surface lifecycle
- Always null-check `screen()`; it is `nullptr` on virtual compositors.
- A layer-shell width of 0 means "fill the screen", not an error.
- Popup surfaces intercept pointer events; render tooltips in-scene instead.
- Unset `QT_WAYLAND_SHELL_INTEGRATION` after the dock is initialized.

## Surface sizing
- Surface height MUST include animation overflow (zoom, bounce, etc.).
- Formula: `surfaceHeight = panelHeight + max(ceil(iconSize * (maxZoomFactor - 1.0)), tooltipReserve) + floatingPadding`.

## Input region / hit box
- Always set the input region explicitly: an empty `QRegion` accepts ALL input, not none.
- Input areas (`QRegion` mask, `MouseArea`, `HoverHandler`, …) MUST match the visible size of the actual UI component.
- Enlarging an input area "for convenience" steals clicks from other apps and UI. If a margin is needed, keep it minimal (≤ 40 px) and document the reason in a comment.

## Layer-shell keyboard focus
- Keyboard focus transfer between layer-shell surfaces is unreliable (asynchronous Wayland round-trips).
- In multi-surface setups (dock + preview), a single surface holds `KeyboardInteractivityExclusive`.
- Drive keyboard navigation on other surfaces through C++ properties and QML bindings, not focus transfer.
