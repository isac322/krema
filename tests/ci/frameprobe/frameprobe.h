// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

namespace krema::testing
{

/**
 * Frame-stepped capture hook for CI.
 *
 * Compiled into krema only when the CMake option KREMA_TEST_HOOKS is ON, and
 * inert unless KREMA_PROBE_NDJSON is set at runtime. When active it:
 *
 *  - installs a fixed-step QAnimationDriver so animation time advances exactly
 *    KREMA_PROBE_STEP_MS per captured frame, decoupled from the wall clock;
 *  - drives rendering itself, one frame per event-loop turn;
 *  - writes one NDJSON row per frame describing every QQuickItem in every
 *    Qt Quick window (geometry, scale, opacity, visibility);
 *  - optionally saves the rendered pixels of each frame as PNG;
 *  - replays a scripted input scenario keyed to frame numbers.
 *
 * Environment:
 *   KREMA_PROBE_NDJSON         output NDJSON path (activates the probe)
 *   KREMA_PROBE_DIR            directory for per-frame PNGs (optional)
 *   KREMA_PROBE_STEP_MS        virtual ms per frame (default 16)
 *   KREMA_PROBE_MAX_FRAMES     stop and quit after N frames (default 600)
 *   KREMA_PROBE_SETTLE_FRAMES  frames rendered before frame 1 is recorded (default 30)
 *   KREMA_PROBE_SCRIPT         JSON array of frame-keyed input actions
 */
class FrameProbe
{
public:
    static void installIfEnabled();
};

} // namespace krema::testing
