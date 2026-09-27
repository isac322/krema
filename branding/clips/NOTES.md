# Krema product clips

Short, real screen recordings of Krema for the landing page (`website/media/`).
Every frame comes from the compositor output of a headless KDE Plasma 6 session
that runs Krema built from this repository. Nothing is drawn, simulated, or
composited by hand; the pipeline only crops and encodes the recording.

| File | Shows | Length | Size (webm / mp4 / webp) |
|---|---|---|---|
| `zoom` | The pointer sweeps across the 8 icons and back. The icons magnify on a parabolic curve (MaxZoomFactor 1.6), with in-scene tooltips, then settle. | 7.9 s | 147 / 144 / 23 KB |
| `launch` | A click on the Firefox launcher. The icon bounces, Firefox's window opens (splash), and the running dot appears. The window then closes. | 7.9 s | 87 / 105 / 23 KB |
| `attention` | Three `notify-send` notifications for Konsole. The badge counts 1, 2, 3, the Wiggle attention animation plays, and the badge clears when the notifications close. | 6.9 s | 38 / 56 / 23 KB |
| `settings` | Icon size 80 → 96 → 80 with the spin box in Krema's settings window. The dock resizes live. | 8.0 s | 84 / 94 / 29 KB |
| `autohide` | Auto-hide: the dock slides in when the pointer reaches the bottom edge, zooms under it, and slides out after the pointer leaves. | 6.4 s | 77 / 89 / 17 KB |

All clips are 1280×640 (2:1), a 1:1 pixel crop of the 1920×1080 screen at
(320, 440), so they are sharp at 1280 CSS px (not 2× retina). They play at
30 fps and have no audio. Each clip starts and ends in the same resting
state, so it loops cleanly. Posters (`.webp`) are the first frame of each clip,
so a poster never jumps when the video starts.

## Environment

Same session as `branding/screenshots/` (see `../screenshots/NOTES.md`), with
these differences:

- Container `fedora:44` (linux/arm64) plus `ffmpeg` (Fedora's `ffmpeg-free`) for
  recording. KWin 6.7.5, plasma-workspace 6.7.5, Qt 6.11.2, Breeze Light.
- Krema is built from this working tree (`Dockerfile`, `local` target).
- Qt Quick in the clients renders with OpenGL on Mesa llvmpipe
  (`LIBGL_ALWAYS_SOFTWARE=1`; buffers go to KWin over `wl_shm`), not
  `QT_QUICK_BACKEND=software`. The software renderer cannot draw `MultiEffect`
  layers, so the numbered notification badge was invisible with it. KWin itself
  still composites with QPainter.
- Pointer theme `breeze_cursors`, size 24.
- The whole session runs on a slowed clock (`SPEED=0.2` by default; see
  "Slowed clock" below).
- `~/.config/kremarc`: `IconSize=80`, `AttentionAnimationDuration=3`,
  `PreviewEnabled=false`, and 8 pinned launchers: Dolphin, Firefox, Konsole,
  Kate, Okular, Gwenview, KCalc, System Settings. Everything else uses the
  defaults (MaxZoomFactor 1.6, Wiggle attention animation, Number badges,
  ShowDelay 200 ms, HideDelay 400 ms). The autohide clip also sets
  `VisibilityMode=1` and restarts Krema.
- A KWin window rule (`~/.config/kwinrulesrc`) opens Firefox at 800,470 with
  size 640×460, inside the crop.

## How it works

| File | Role |
|---|---|
| `Dockerfile` | Fedora 44 + KWin/Plasma/apps + Xvfb, xdotool, ffmpeg. `local` target builds Krema from the named context `krema-src`; `copr` installs the COPR package. |
| `session.sh` | Container entrypoint: sets up the slowed clock, starts Xvfb `:99` 1920×1080 and `kwin_wayland --x11-display :99` (X11-windowed backend), and writes `/tmp/session.env`. |
| `clips.sh` | `setup` starts kactivitymanagerd, plasmashell (panel removed, "Next" wallpaper), Krema and the KWin rule. Then one function per clip drives input and records it. Pointer glides are single `xdotool` command chains (`mousemove … sleep 0.0166 …`, smoothstep easing, 60 Hz). |
| `kwinscript.sh`, `settings.js`, `close-settings.js`, `close-app.js` | KWin scripts over D-Bus that place/close windows. |
| `encode.sh` | Host side: crop, trim, and encode to WebM/MP4/WebP. |

Recording: while a clip function runs, `ffmpeg -use_wallclock_as_timestamps 1
-f x11grab -framerate 60 -video_size 1920x1080 -draw_mouse 1 -i :99 -c:v
utvideo -pix_fmt gbrp` writes a lossless capture of the whole screen, including
the pointer. Each frame is stamped with the session clock when it arrives.
Input goes through `xdotool` into the Xvfb display, so KWin receives it as
ordinary host-window input. Notifications come from `notify-send`, and
plasmashell's notification server forwards them to Krema's
`org.kde.NotificationWatcher`.

### Slowed clock

Recording in real time only works on an idle host: the frame rate drops and
glides stretch as soon as other containers compete for the CPU, and
plasmashell's D-Bus calls time out. `session.sh` therefore preloads
`libfaketimeMT` with `FAKETIME='+0 x$SPEED'` for every process in the session:
Xvfb, KWin, plasmashell, Krema, the launched apps, `xdotool`, `sleep`,
`notify-send`, and the ffmpeg recorder. At `SPEED=0.2`, one second of session
time takes five seconds of wall time. (Python's clock is not affected by
libfaketime, so pointer timing uses `xdotool sleep`, not a Python loop.)
Animations, QTimers (ShowDelay,
HideDelay, attention duration), frame callbacks, pointer glides, and frame
timestamps all use the same clock, so the recording plays back at true speed
with 5× the host headroom per frame. Krema's code is unchanged. This works like
a high-speed camera: the recording is slowed, not rendered frame by frame.

One consequence: app start-up takes its real CPU time, so in session time it
looks `1/SPEED` times faster. KCalc or Kate map a window before the first
launch bounce completes. The launch clip therefore uses Firefox, whose cold
start is long enough for several bounces.

The captured screen changes at roughly 40–60 distinct frames per second of
session time. The encodes use 30 fps.

### Clip scripts

- **zoom**: pointer enters from the upper left, sweeps across all 8 icons
  (2.6 s), sweeps back (2.6 s), and leaves the way it came.
- **launch**: pointer clicks the Firefox launcher, then leaves the dock
  immediately. The icon bounces until Firefox's first window maps, then the
  running dot appears. At the end, `close-app.js` closes the window, so the dot
  goes away again.
- **attention**: pointer outside the crop. Three `notify-send -t 60000
  --hint=string:desktop-entry:org.kde.konsole` calls 0.9 s apart. The badge
  counts 1, 2, 3 and each increase starts the Wiggle attention animation. Then
  `CloseNotification` on the three ids clears the badge.
- **settings**: before recording, the context menu (right-click, then
  Up Up Up Return = "Settings...") opens the settings window, and
  `settings.js` places it. Recorded: four clicks on the Icon size spin box's up
  arrow (80 → 96) and four on the down arrow (back to 80). The dock resizes
  live. The Krema settings window itself shows up as a ninth dock entry with
  the Zoom K icon.
- **autohide**: `VisibilityMode=1`. The dock is hidden, the pointer reaches the
  bottom edge, the dock slides in (after ShowDelay), the pointer brushes the
  icons, leaves, and the dock slides out.

### Regenerate

```sh
# From the repository root.
docker build --build-context krema-src=. -t krema-clips branding/clips
mkdir -p /tmp/krema-clips-out && chmod 777 /tmp/krema-clips-out
docker run -d --name krema-clips --hostname fedora --shm-size 1g \
  -v "$PWD/branding/clips:/clips:ro" -v /tmp/krema-clips-out:/out \
  krema-clips dbus-run-session -- bash /clips/session.sh
sleep 8   # wait for READY in `docker logs krema-clips`
docker exec krema-clips bash /clips/clips.sh all      # ~2 min; raw .mkv in /tmp/krema-clips-out/raw
docker rm -f krema-clips
nix shell nixpkgs#ffmpeg -c branding/clips/encode.sh /tmp/krema-clips-out/raw
```

Raw captures are about 60–90 MB/s of lossless video. Five clips need about
3–4 GB in `/tmp/krema-clips-out/raw`. `clips.sh all` has to run in a fresh
container: the launch clip relies on Firefox's first (cold) start.

Encoding (`encode.sh`): `crop=1280:640:320:440,fps=30`, BT.709 yuv420p.
- WebM: `libvpx-vp9 -b:v 0 -crf N -row-mt 1 -deadline good -cpu-used 1 -g 240`
- MP4: `libx264 -profile:v high -preset veryslow -crf N -g 240 -movflags +faststart`
- Poster: first frame, `libwebp -quality 82`

For each format, `encode.sh` starts at a low CRF (VP9 30, x264 23) and raises
it until the file fits 1.2 MB. The trim points (`START`/`DUR` in `encode.sh`)
were picked from contact sheets so each clip starts and ends at rest.

## Limitations

- **No window-preview clip.** KWin composites with QPainter here (no DRM render
  node in Docker on macOS), so it refuses screencasts and Krema's preview popup
  only shows the app-icon placeholder. The launch clip moves the pointer off
  the dock right after the click to avoid that popup: `src/qml/main.qml`'s
  `onRowsInserted` handler opens the preview when a hovered launcher's window
  appears, and it does not check `PreviewEnabled`.
- **Short launch bounce.** Firefox on Wayland sends no startup notification,
  so Krema bounces its icon for about 1 s (manual-launch bridge plus the
  500 ms safety timer), then waits at rest until the window maps. Firefox's
  first paint (white, then grey, then its splash) is in the clip as it happened.
- **No blur.** QPainter compositing has no Blur or Background Contrast effect.
- **Not 2× retina.** 1:1 at 1280 px. A 2× capture (Xvfb 3840×2160) is too
  heavy for llvmpipe in this setup.
- **Trim points depend on the recording.** Launch timing depends on
  Firefox's cold start. After regenerating, rebuild the contact sheets and
  re-pick `START`/`DUR` in `encode.sh`.
