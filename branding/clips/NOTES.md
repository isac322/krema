# Krema product clips

Short screen recordings of Krema for the landing page (`website/media/`).
Every frame comes from the compositor output of a headless KDE Plasma 6 session
running Krema built from this repository. Nothing is drawn or composited by
hand. The pipeline only trims and encodes the recording.

The landing clips come from the GPU session on an RK3588 board (rock5bp, see
"GPU session"): KWin on the Mali GPU, real time, VPU-encoded intermediate.
The Xvfb session (`session.sh`, slowed clock, QPainter) is the fallback for a
machine without a GPU; it cannot show window previews.

| File | Shows |
|---|---|
| `zoom` | The pointer sweeps across the 8 icons and back. Icons magnify on a parabolic curve (MaxZoomFactor 1.6) and the neighbors move aside; text tooltips. |
| `launch` | A click on Kate. The icon bounces while Kate starts, its window opens (620×440), and the running dot appears. The window then closes. |
| `attention` | Three `notify-send` notifications for Konsole. The badge counts 1, 2, 3, the Wiggle animation plays, and the badge clears when the notifications close. |
| `wheel` | Three Dolphin windows. Scrolling over the Dolphin icon brings each window to the front in turn. |
| `middle` | Xvfb session only, review only: a middle click on the running Konsole opens a second window. |
| `reorder` | Kate is dragged two places to the right and then back. |
| `settings` | Icon size 72 → 88 → 72 with the spin box in Krema's settings window. The dock resizes live. |
| `styles` | Background style Tinted, Acrylic, Transparent, then Panel Inherit, picked in the settings window. |
| `dodge` | Dodge mode: a Kate window dragged over the dock makes it slide away, and dragging it back brings the dock back. |
| `keyboard` | Meta+F5 focuses the dock, the arrow keys move the focus, and Escape leaves. |
| `autohide` | Auto-hide: the dock slides in at the bottom edge, zooms under the pointer, and slides out after the pointer leaves. |
| `previews` | Hover over the running Konsole: the preview popup opens with a live PipeWire thumbnail of the window (`top` refreshing). The pointer moves onto the thumbnail and leaves. |
| `groups` | Three Konsole windows with slowly updating output. The popup shows three live thumbnails; a click on the middle one brings that window to the front, and after the popup opens again a click on the third brings it back on top, restoring the starting stack. |
| `progress` | Dolphin (pinned, not running) reports a transfer over the Unity LauncherEntry API: a count badge (3, 2, 1) and a progress bar filling to 100 %, both cleared at the end. |

Sizes and durations are printed by `encode.sh`. Clips are 2560×1280 (2×
density of a 1280×640 logical crop), 60 fps constant frame rate resampled
from the variable-rate capture, no audio, at most 1.8 MB per file. The
wallpaper is Roast Contours (`branding/wallpaper/roast-contours.png`); its
thin lines band at high compression, so every video is encoded to use its
whole 1.8 MB budget. Each clip starts and ends in the same resting state, so
it loops cleanly. Posters (`.webp`) are the first frame.

`stills.sh` retakes `branding/screenshots/{dock-overview,dock-zoom,settings}.png`
in its own GPU session at 1920×1080, scale 1:
`W=1920 H=1080 SCALE=1 clips/thumbs.sh up`, then
`docker exec krema-thumbs bash /clips/stills.sh` (PNGs in `out/stills/`).
Optimize them with `oxipng -o 4 --strip safe`.

## Xvfb session: the launch-clip jitter (root cause)

The previous clips were recorded with the whole session on a slowed clock
(libfaketime, `FAKETIME='+0 x0.2'`). In the old `launch` clip the bouncing
icon and Firefox's splash logo jumped back and forth.

Frame analysis of the old raw recording (`ffprobe` per-frame pts plus a
per-frame tracker of the icon's top edge):

- Frame timestamps were fine: 573 frames at 16/17 ms steps, perfectly CFR.
- The bounce was not. Krema's launch bounce is `Kirigami.Units.longDuration`
  up plus the same down (2 × 200 ms = 400 ms per cycle). In the recording one
  cycle took about 5 frames (83 ms of session time, peaks at frames 126, 129,
  136, 140, 146, 150, 156, 160, 165, 170, 176). 400 ms × 0.2 = 80 ms, so the
  animation ran on *unslowed* time. The recorder sampled a 2.5 Hz wall-clock
  motion 12 times per wall second, at arbitrary phases, so the icon jumped
  between random heights. Firefox's pulsing logo was aliased the same way.
- The Wiggle attention animation (760 ms) was squeezed into about 0.27 s of
  the old attention clip for the same reason.

Cause: Fedora's libfaketime slows `CLOCK_REALTIME` and sleep/poll timeouts,
but leaves `CLOCK_MONOTONIC` alone unless `FAKETIME_DONT_FAKE_MONOTONIC=0`.
Inside the container, Python's `time.monotonic()` measured 1.15 s across a
`sleep 0.2` in the slowed session, while `time.time()` measured 0.23 s.
QTimers and `sleep` were slowed (their timeouts are scaled), which hid the
problem. KWin's render loop, Qt Quick's animation driver and Firefox's
refresh driver all run on the monotonic clock, so every animation ran 5× too
fast relative to the recording.

Fix (`session.sh`):

1. `FAKETIME_DONT_FAKE_MONOTONIC=0`, so the monotonic clock is slowed too and
   every process shares one time base. After the fix the tracked bounce is a
   smooth eased curve at the right speed (top edge 59 → 55 → 52 → 46 → 44 →
   41 → 39 → 37 … and back), and the Wiggle lasts 0.78 s in the recording.
2. `QSG_USE_SIMPLE_ANIMATION_DRIVER=1`: Qt Quick animations follow elapsed
   (slowed) time instead of adding one 16.7 ms vsync tick per rendered frame,
   so a late frame cannot stretch an animation.
3. Capture at 120 fps. KWin repaints at 60 Hz on its own phase. A 60 Hz grab
   beats against it: only about 40 distinct frames per second, the rest
   duplicates. At 120 fps the grab caught 63 distinct frames per second.
   `encode.sh` resamples to 60 fps CFR.
4. `SPEED=0.1` (10× headroom). Pointer glides are sent at 120 Hz.

## Xvfb session environment

- Container `fedora:44` (linux/arm64): KWin 6.7.5, plasma-workspace 6.7.5,
  Qt 6.11.2, Breeze Light, Firefox 156. Krema is built from this working tree
  (`Dockerfile`, `local` target).
- `kwin_wayland --x11-display :99` (X11-windowed backend) on Xvfb
  2560×1440. The output scale is set to 2 with `kscreen-doctor`, so the logical
  screen is 1280×720. (`kwin_wayland --scale 2` only enlarges the host window
  and leaves the output at scale 1.)
- Clients render Qt Quick with OpenGL on Mesa llvmpipe
  (`LIBGL_ALWAYS_SOFTWARE=1`, buffers over `wl_shm`). KWin itself composites
  with QPainter.
- `~/.config/kremarc`: `IconSize=72`, `AttentionAnimationDuration=3`,
  `PreviewEnabled=false`, and 8 pinned launchers: Dolphin, Firefox, Konsole,
  Kate, Okular, Gwenview, KCalc, System Settings. Everything else uses the
  defaults. `autohide` sets `VisibilityMode=1` and `dodge` sets
  `VisibilityMode=2`; both restart Krema.
- KWin window rules place Firefox, Konsole and Kate inside the recorded band.
  Firefox gets a fresh profile that opens the offline start page
  (`firefox_profile` in `clips.sh`).

## GPU session (primary: every landing clip and the stills)

Window thumbnails need KWin screencasts, and those need OpenGL compositing.
`session-gpu.sh` runs KWin's virtual backend on a real GPU and records in
real time (`SPEED=1`, no libfaketime).

- Host: rock5bp (RK3588, Debian 12, Rockchip vendor kernel 6.1, Mali G610
  on the `bifrost_kbase` driver g25p0), a shared homelab node. `thumbs.sh
  up` passes the devices Jellyfin uses for RKMPP (`/dev/dri/*`,
  `/dev/mali0`, `/dev/mpp_service`, `/dev/rga`, each `/dev/dma_heap/*`
  node) with `--group-add` for their groups (video 44, render 105). Not
  privileged; `--memory 6g`, `--cpu-shares 256` (the node's own workloads
  win), `--cpus` from `CPUS` (4 by default, 7 for the shoot), `--ulimit
  core=0` (kded6 crashes at start and would drop a `core` file in the home
  folder). Record only while the node is otherwise idle: a CI build on it
  cut the capture rate by a third.
- Local-only components, never committed or baked into the image:
  `thumbs.sh prepare` downloads Arm's libmali (proprietary, JeffyCN mirror)
  and extracts jellyfin-ffmpeg from the Jellyfin image on the node into
  `hw/`; `thumbs.sh up` mounts them read-only.
- KWin on the GPU: Arm's libmali (g24p0 `wayland-gbm`, JeffyCN mirror, local
  use only, mounted from `hw/mali`) gives KWin EGL + GBM + GLES:
  `supportInformation` reports `Mali-G610`, OpenGL ES 3.2. Mali's GLSL
  compiler rejects KWin's `#if GL_OES_standard_derivatives` (undefined
  macro), so KWin fell back to QPainter; `gles-shim.c` (a
  `libGLESv2.so.2` in front of libmali) rewrites that test to
  `#if defined(...)`. `MALI=0` keeps Mesa (llvmpipe).
- Clients (Krema, plasmashell, apps) stay on llvmpipe: Fedora's Qt wants
  desktop OpenGL (libmali has only GLES) and libmali's Vulkan driver has no
  Wayland WSI (`vkCreateWaylandSurfaceKHR` missing). `LP_NUM_THREADS=2`:
  with one llvmpipe thread per host core the container hit its CPU quota in
  bursts (27 throttled periods in one 20 s probe, down to 1 to 3).
- Output 2560×1440, scale 2 through `kscreen-doctor`. Input: xdotool (and
  `glide.py`, XTest paced on the monotonic clock) on KWin's Xwayland; XTEST
  reaches KWin through libei (`kwinrc [Xwayland] XwaylandEisNoPrompt=true`).
  Every X client gets its own libei device, and about 1 s after the client
  that pressed a button exits, KWin releases that button (the client sees a
  `wl_pointer.button` release while no release was sent). So a press,
  the motion after it and its release must come from one process: drags use
  `glide.py --hold`, never `xdotool mousedown` followed by a glide.
- Recording (`rec_start`): `sessiontool` starts a region screencast with the
  pointer embedded; `record.py` reads it with GStreamer and pipes I420
  Matroska through a 1 MiB FIFO into jellyfin-ffmpeg (taken from the
  Jellyfin image the node already runs, run through that image's own
  loader), which encodes H.264 on the VPU (`h264_rkmpp`, CQP 10). The file
  is VFR with KWin's timestamps, BT.709 limited range; `encode.sh`
  resamples to 60 fps on the Mac.
- What limits the frame rate, measured on a 4 s glide:
  - memfd buffers: KWin reads back inside its render loop, 34 to 41 fps, and
    on GLES the image is upside down. DMA-BUF (`--gl`: GPU blit in KWin,
    GL import + I420 conversion + readback in GStreamer) avoids both.
  - KWin's per-stream limiter counts whole milliseconds from the moment the
    previous frame was queued, drifts, and skips frames: 54 fps even into a
    fakesink. `record.py` asks for `max-framerate=0/1` (no limiter, about
    110 frames a second with 120 Hz input) and keeps frames at least 15 ms
    apart at the source.
  - The default 64 KiB FIFO: even `ffmpeg -c copy` read only about 50 fps
    of 6 MB frames. At 1 MiB the chain reaches 57 fps, the same as `cat`.

### Timing proof (`clips.sh timing`)

A 4.0 s glide (logical x 100 to 1180, smoothstep) and, 1 s later, a 4.0 s
progress ramp from one `unity.py` process. Two runs from a clean build:

| | Script | Recording, run 1 | Run 2 |
|---|---|---|---|
| Glide length (25 % to 75 % crossing, smoothstep) | 4.000 s | 4.003 s | 4.001 s |
| Ramp length (bar fill 10 to 90 %, linear fit) | 4.001 s | 3.924 s | 3.942 s |
| Frame spacing during the glide (median, mode) | | 17 ms, 16 to 17 ms | 17 ms, 16 to 17 ms |
| Capture rate while moving (`record.py`) | | 54.4 fps, 39 gaps > 25 ms | 54.7 fps, 33 |

The ramp fit reads a 114 px bar with rounded ends; its 1.5 to 2 % is about
2 px. The ramp shows about 0.2 s after the script sends it (D-Bus, Krema).
The gaps are single or double missed frames (40 to 57 ms), more while Krema
repaints in llvmpipe (43 to 46 fps during the ramp).

### Regenerate the clips and stills

```sh
# On the RK3588 host, in a scratch dir:
mkdir -p krema-thumbs-work && cd krema-thumbs-work
(cd /path/to/krema && git archive origin/master CMakeLists.txt src packaging) | (mkdir -p src && tar x -C src)
rsync -a /path/to/krema/branding/clips/ clips/
rsync -a /path/to/krema/branding/wallpaper/ wallpaper/
clips/thumbs.sh prepare     # libmali + jellyfin-ffmpeg into hw/
clips/thumbs.sh build       # image krema-thumbs
uptime                      # wait until the node is otherwise idle
CPUS=7 clips/thumbs.sh up   # session container, SPEED=1, MALI=1
clips/thumbs.sh run all     # setup + every clip; raws in out/raw
W=1920 H=1080 SCALE=1 clips/thumbs.sh up && docker exec krema-thumbs bash /clips/stills.sh
clips/thumbs.sh down        # (clean also removes the image, hw/ and out/)
# On the Mac:
nix shell nixpkgs#ffmpeg -c branding/clips/encode.sh RAW_DIR
```

Shooting, measured on rock5bp (CPUS=7, `bash -x` timestamps): image build
(cached) 2 s, container start 3 to 4 s, `setup` 5.7 s, the 13 GPU clips
162 s, of which 115 s is recording. Most clips take their recording plus
about 1 s; `styles`, `groups` and `dodge` open windows first (8, 7 and 5 s).
Nothing sleeps a fixed time to wait for a program: `start_krema` waits for
the dock's layer surface, KWin scripts for their own marker, windows for
KWin's window list, `rec_start` for the first frame. Copying the raws
(50 MB) to the Mac takes 2 s. Encoding is the long part: 2560×1280 VP9 at
`-cpu-used 2` needs several minutes per clip even with all encodes in
parallel.

Measured capture rate while something moves (`record.py`, CPUS=7, idle
node): zoom 49, launch 48, reorder 52, dodge 56, wheel 52, autohide 50,
progress 53, styles 41, settings 44, previews 42, attention 35, keyboard 34,
groups 32 fps. Attention and keyboard change in discrete steps, so their
figure mostly counts pauses; groups has three live thumbnail streams and
three redrawing terminals. The pointer glides in zoom and launch: median
frame spacing 17 ms, 17 to 20 % of gaps over 25 ms (a missed frame), 2 to 7
over 45 ms; after the 60 fps resample about one tick in five repeats a
frame. The rest of the gap to 60 fps is KWin compositing the llvmpipe
clients' `wl_shm` buffers while the dock redraws (capture falls from about
48 to 30 fps when the pointer is over the dock on a busy node).

Popup coordinates (`THUMB_*`, `CLOSE_*`) and the settings-page coordinates
(`UP_*`, `STYLE_*`) in `clips.sh` were measured with `clips.sh geometry`.
Re-measure them if the layout changes.

## How it works

| File | Role |
|---|---|
| `Dockerfile` | Fedora 44 + KWin/Plasma/apps + Xvfb, xdotool, ffmpeg, libfaketime. Target `gpu` adds Xwayland, GStreamer (PipeWire, GL), `sessiontool` and `gles-shim`. |
| `session.sh` | Entrypoint: slowed clock, PipeWire, Xvfb, KWin, output scale, `/tmp/session.env`. |
| `session-gpu.sh` | Entrypoint of the GPU session: KWin virtual backend on libmali (GLES), Xwayland, scale 2, real time. Sets `GPU=1`, `JF_FFMPEG` and `CAPTURE_GL` in `/tmp/session.env`. |
| `thumbs.sh` | Host side of the GPU session: fetch libmali and jellyfin-ffmpeg, build, run the container with the Rockchip devices, clean up. |
| `gles-shim.c` | `libGLESv2.so.2` in front of libmali that makes KWin's shaders compile on Mali. |
| `sessiontool.c` | Starts a KWin region screencast and prints its PipeWire serial. |
| `record.py` | GStreamer recorder: DMA-BUF + GL readback, source-side frame limiter, capture statistics, `--ready` flag on the first frame, trim marks on SIGUSR1 (`--trim`). |
| `glide.py` | Pointer glide through XTest, every step on its own deadline; `--hold S` makes it a drag (press, hold, glide, release in one X connection). |
| `unity.py` | Sends Unity LauncherEntry updates read from stdin, including self-paced `ramp=FROM:TO:SECONDS` (`progress`, `timing`). |
| `clips.sh` | `setup`, then one `clip_*` function per clip that drives input and records. Any helper can be called by name (`clips.sh open_settings geometry`). The clip list depends on the session; `clips.sh timing` is the timing probe. |
| `stills.sh` | The three product stills in a 1920×1080 GPU session. |
| `kwinscript.sh` | Runs a KWin script over D-Bus (`clips.sh` generates small scripts to place and close windows) and unloads it once its last line has printed a marker. |
| `encode.sh` | Host side: trim to the recorder's marks, 60 fps CFR, encode WebM/MP4/WebP within 1.8 MB, every encode in parallel. |

Xvfb session recording: `ffmpeg -f x11grab -framerate 120 -video_size
2560x1280 -i :99+0,160 -draw_mouse 1 -c:v utvideo` writes a lossless capture
of the bottom 640 logical px of the screen, including the pointer.

Meta+F5: xdotool's Super modifier does not reach KWin's global shortcut
handling in the X11-windowed backend (Right, Left and Escape do arrive), so
`clip_keyboard` triggers Krema's registered `focus-dock` action, the action
Meta+F5 is bound to, through kglobalaccel's `invokeShortcut`.

### Regenerate in the Xvfb session (no GPU)

```sh
# From the repository root.
docker build --build-context krema-src=. -t krema-clips branding/clips
mkdir -p /tmp/krema-clips-out && chmod 777 /tmp/krema-clips-out
docker run -d --name krema-clips --hostname fedora --shm-size 2g -e SPEED=0.1 \
  -v "$PWD/branding/clips:/clips:ro" -v /tmp/krema-clips-out:/out \
  krema-clips dbus-run-session -- bash /clips/session.sh
docker exec krema-clips bash /clips/clips.sh all   # about an hour at SPEED=0.1
docker rm -f krema-clips
nix shell nixpkgs#ffmpeg -c branding/clips/encode.sh /tmp/krema-clips-out/raw
```

Raw captures are about 150–300 MB per second of clip. The full set needs
about 40 GB. `clips.sh all` must run in a fresh container: the launch clip
relies on Firefox's first (cold) start.

Encoding: `fps=60`, Lanczos scale to 2560×1280, BT.709 limited-range
yuv420p. Each video is a 2-pass encode at the bitrate that fills 96 % of
1.8 MB over the clip's length: VP9 in constrained quality (`-crf 20`,
`-deadline good`, `-cpu-used 4` for pass 1 and 2 for pass 2, `-row-mt 1
-tile-columns 2`) and H.264 High (`-preset slow`, faststart). The trim comes
from `RAW/<clip>.trim`, written by the recorder (the Xvfb clips `middle`
and `pin` have fixed trims in `encode.sh`). All encodes of all clips run at
once, `JOBS` at a time (default: half the cores), longest first.

## Limitations

- **Xvfb session: no window previews, no grouped-window popup.** KWin
  composites with QPainter there and refuses screencasts (`Unsupported
  compositing type`); Docker on macOS has no `/dev/dri`. `previews` and
  `groups` come from the GPU session.
- **GPU session: about 48 to 56 fps while moving, not 60.** See the capture
  rates above: the clients render in llvmpipe (no GPU path: Fedora's Qt
  wants desktop GL, libmali's Vulkan has no Wayland WSI), and KWin composites
  their `wl_shm` buffers. After the 60 fps resample about one frame in five
  repeats during pointer glides.
- **Xvfb session: Firefox opens nearly full-screen in `launch`** (the KWin
  rule does not hold Firefox 156 at 620×440); the GPU session uses Kate.
- **No `pin` clip yet.** The drag from Dolphin reaches the dock but the drop
  target (`icon_x 7 + 50`) is outside the dock panel, so nothing is pinned.
  `clip_pin` needs a drop point inside the panel.
- **Xvfb session: no blur.** QPainter compositing has no Blur or Background
  Contrast effect, so Acrylic shows its tint without blur.
- **Trim points of the Xvfb clips are fixed.** `middle` and `pin` have no
  recorder marks; re-check their trims in `encode.sh` with a contact sheet
  after regenerating.
- **Krema keeps a stale hovered icon after a drag.** Hover tracking pauses
  while dragging and the drop does not refresh it, so a press right after a
  drop, without moving the pointer, picks the icon that now sits in the
  dragged icon's old slot. `clip_reorder` moves the pointer 6 px before its
  second drag.
- **`encode.sh` can overshoot 1.8 MB.** The 2-pass target (96 % of the
  budget) came out over it for groups (VP9 and H.264), styles and progress
  (H.264) on the last run; those need a lower target before they are
  re-encoded.
