# Panel-motion PR capture

This entrypoint records the real, built Krema beside a genuine Plasma panel.
It adapts the private Xvfb → nested KWin → plasmashell session used for panel-gap
QA, and the `branding/clips/clips.sh` X11 recording and pointer-input conventions.
It does not depend on a scratch session script or a pre-existing session.

Each run records its source revision, binary hash, stage observations, and
encoder metadata. Inspect the rendered transitions and cursor before attaching
the videos to a PR. Encoded frame rate alone does not prove compositor cadence.

## Interface

Run on Linux, inside the environment containing the built product and Plasma:

```sh
PANEL_MOTION_SOURCE=/absolute/path/to/krema \
PANEL_MOTION_REVISION=<final-source-revision-or-omit-when-git-is-readable> \
KREMA_BINARY=/absolute/path/to/build/bin/krema \
TEST_WINDOW_BINARY=/absolute/path/to/build/bin/krema-test-window \
OUT=/absolute/path/outside/krema/panel-motion-capture-01 \
DISPLAY=:99 \
bash /absolute/path/to/krema/branding/clips/panel-motion/capture.sh
```

There are no positional arguments.

| Variable | Contract |
|---|---|
| `KREMA_BINARY` | Required: executable built from the source tree selected by `PANEL_MOTION_SOURCE`. The script does not build or install it. |
| `OUT` | Required: a new directory outside the checkout. Existing directories are refused, not overwritten. All logs, configuration, raw recordings, encodes, and the cleanup ledger go here. |
| `DISPLAY` | Optional, default `:99`: unused local X display, without a screen suffix. An existing socket or lock causes refusal. |
| `PANEL_MOTION_SOURCE` | Optional: checkout root, defaulting to four directories above this script. Must contain the existing `tests/appium/krema_e2e` helpers. |
| `PANEL_MOTION_REVISION` | Optional provenance override for read-only/container worktrees whose Git metadata is unavailable. Recorded verbatim as `source_sha`, with `source_dirty` reported as `unknown`. Without this override, Git is used when available; an unavailable Git checkout is recorded as `source_sha: unknown` rather than fabricating a revision. |

The script always creates a private D-Bus session and private HOME/XDG paths. It
starts its own Xvfb, KWin, activity manager, plasmashell, fixture, and Krema. It
removes default panels only inside that private Plasma session. Child process
groups stop on normal completion or an exception; artifact directories remain.
Use a fresh `OUT` and a free display for every invocation. Do not run this as a
user's desktop session launcher.

## Dependencies

- Bash; `dbus-run-session`; Git unless `PANEL_MOTION_REVISION` is supplied.
- Python 3.11 or newer (`hashlib.file_digest`), PyGObject with Gio/GLib, Pillow,
  and the Qt 6 runtime. The Python packages and Qt library are required by the
  existing `krema_e2e.dbus`, `kwin`, and `env` helpers; Appium/WebDriver is not used.
- KDE Plasma 6: `kwin_wayland` with its X11-windowed backend and scripting D-Bus
  API; `plasmashell` with its `evaluateScript` API; `kactivitymanagerd`; Breeze
  cursor assets; the Kickoff, Icons-only Task Manager, and Digital Clock widgets.
- `Xvfb`, `xdpyinfo`, and `xdotool` with XTEST support.
- FFmpeg/ffprobe with `x11grab`, `utvideo`, PNG support, and at least one real H.264 encoder: `libx264` (preferred) or `libopenh264` (fallback).
- Krema's build/runtime dependencies, the built Krema executable, the built
  `krema-test-window` fixture, and installed KDE application desktop/icon assets
  for Dolphin, Kate, and Konsole.

No DRM device, GPU, PipeWire service, screencast portal, or ScreenShot2 capture is
used. This is a real-time software-rendered Xvfb capture, not evidence about
hardware rendering, thumbnails, or a production Wayland driver's frame rate.
The old clip session's libfaketime settings are explicitly discarded.

## Recorded sequence

Both clips show the full 1280 × 720 output with the real pointer embedded.
Krema starts with Floating off, the Panels reference, 48 px icons, and previews
and zoom disabled. The Plasma panel is non-floating and always visible.

1. `panel-height`: Always Visible mode. Show the 48 px panel, request 72 px, then
   restore 48 px. Request another 72 → 48 px reversal with a 60 ms pause after
   the taller panel maps to exercise interrupted panel-follow movement. The
   sidecar records the request/mapping times, not proof of rendered interruption.
2. `dodge`: Restart Krema in **Always Visible** mode with `ReserveScreenSpace=false`, then configure the genuine Plasma panel as non-floating `hiding='dodgewindows'`. Park the cursor away and place the real decorated fixture fully above the panel; the sidecar records Plasma's exact KWin `hidden` property as false. Drag the fixture's titlebar continuously into the panel edge strip and wait for the Plasma panel's KWin `hidden` property to become true; drag it back above the panel and wait for `hidden=false` again. The sidecar records `hiding`, `floating`, `hidden`, `hidden_type`, and frame geometry for both Plasma and Krema, plus explicit visible/hidden oracle stages. The clip then changes the genuine panel height to 72 px and back to 48 px while Krema follows it. This is not a Krema VisibilityMode dodge simulation.

One-second pauses are deliberate viewing holds. Startup instead waits for
Xvfb to accept clients, KWin's socket/D-Bus/scripting, the activity service,
Plasma's desktop scripting API, mapped panel/fixture/dock windows, and FFmpeg's
first encoded frame. Panel-height steps wait for both the scripting value and
KWin's mapped height. The Plasma dodge steps wait on the actual KWin window
`hidden` property, not on a Krema debug log; a callback or target-state event
alone is not treated as visible proof. Time offsets are from recorder launch,
not exact encoded frame PTS.

## Provenance and artifacts

Capture SHA: **resolved at invocation** into `OUT/capture.json.source_sha`.
The same sidecar records whether the checkout is dirty and hashes the supplied
binary. A binary hash does not prove that it was built from that SHA: the caller
must build the final source state before capture and retain the build evidence.
There is deliberately no invented capture SHA or tool-version number here.

| Artifact under `OUT` | Purpose |
|---|---|
| `panel-height.mkv`, `dodge.mkv` | Lossless Ut Video x11grab masters; requested 120 fps, no audio. |
| `panel-height.mp4`, `dodge.mp4` | H.264, yuv420p, 60 fps CFR, faststart; no overlays, composites, or audio. |
| `panel-height.png`, `dodge.png` | First-frame posters; not replacements for inspecting motion frames. |
| `stages.json` | Named stage markers, wall/monotonic times, panel state, cursor, and KWin window geometry. Time offsets are from recorder launch, not exact encoded frame PTS. |
| `capture.json` | Runtime SHA, dirty-state flag, binary path/hash, selected H.264 encoder, capture parameters, and verification disclaimer. |
| `versions.txt` | Actual Python, selected H.264 encoder, KWin, plasmashell/package, xdotool, FFmpeg, and ffprobe version output from the invocation. |
| `*.progress`, `*.log`, `*.ffprobe.json` | First-frame readiness, component/recorder diagnostics, and encoded stream/container metadata. |
| `cleanup-ledger.json` | The created artifact directory and process groups, commands, creation times, and shutdown times. |
| `home/`, `config/`, `data/`, `cache/`, `runtime/`, `tmp/` | Private session state retained for diagnosis. |

Raw captures can consume several GB. Nothing under `OUT` belongs in a commit.
The script does not upload, open a PR, or remove retained resources.

## Review before attaching to the PR

- First rehearse and establish a real-time timing proof: measure a known-duration
  pointer path in the recorded frames using their PTS. Input scheduling and a
  60 fps encode alone do not establish correct motion timing or distinct frames.
- Review the beginning, intermediate motion frames, and end of each master.
  Confirm the real panel, visible cursor, Floating-off state, height sequence,
  Dodge hide/reveal, and absence of desktop setup or unrelated windows.
- Inspect the reversal frame by frame. If the 72 px panel never maps or movement
  has already finished before the second request, the interrupted-motion scene
  is not verified and must be recaptured after adapting the choreography.
- Verify no snap, panel-gap bounce, or incorrect direction during movement;
  quantify frame spacing, repeated frames, and edge jitter rather than inferring
  smoothness from the requested capture/encode rate. Compare against the base
  revision with the same script and environment when claiming an improvement.
- Inspect all frames and sidecars for private information before publishing.
  Attach the reviewed MP4s to the PR through the normal GitHub attachment flow;
  put the resulting attachment URLs in its body, along with the capture SHA and
  what each clip demonstrates. Do not substitute local artifact paths for URLs.
- Keep the ledger and retained artifacts until the owner signals that the task
  is finished. Confirm identity and ownership before cleanup; never act on a
  process-group ID that may have been reused after its recorded shutdown.
