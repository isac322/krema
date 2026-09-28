---
description: "Apply when waiting for asynchronous state from KDE/Qt models or services, or when adding a QTimer/QML Timer."
---

# Async State Handling

- Never poll with a timer while waiting for asynchronous state; subscribe to KDE/Qt signals (`dataChanged`, `rowsInserted`, …).
- If a KDE API is one-shot (no retry), re-request explicitly from the related signal handler.
- Timers are allowed only for UI delays (debounce, hide delay, …), never for business logic.
