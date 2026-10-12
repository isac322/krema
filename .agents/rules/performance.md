---
description: "Apply when writing rendering, animation, window-preview/PipeWire, image, or GPU-related code, or when optimizing performance of the dock or the website."
---

# Performance

Krema is a dock: visual smoothness and responsiveness are non-negotiable.

- Use GPU-accelerated APIs (PipeWire: `allowDmaBuf: true`; Qt Quick: hardware QRhi backend).
- Avoid CPU→GPU texture copies; prefer GPU-native paths.
- Fallback chain: GPU → CPU → placeholder. Never crash or freeze.
- Release GPU resources when not visible; cap concurrent PipeWire streams at 4.
- Initialize lazily: do not allocate until first needed.
- Animations run at 60 fps using declarative Qt Quick animations; no per-frame JavaScript updates.
- A performance-only change must keep appearance and behavior identical; any visible or functional difference is a regression.
- Profile before changing anything, then measure the same scenario on the same setup before and after and put the numbers in the PR. Merge only when the measurement shows an improvement.
