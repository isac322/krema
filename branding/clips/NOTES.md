# Krema product clips

Short screen recordings of Krema for the landing page (`website/media/`).
Every frame comes from the compositor output of a headless KDE Plasma 6 session
running Krema built from this repository. Nothing is drawn or composited by
hand. The pipeline only trims and encodes the recording.

| File | Shows |
|---|---|
| `zoom` | The pointer sweeps across the 8 icons and back. Icons magnify on a parabolic curve (MaxZoomFactor 1.6) and show text tooltips. |
| `launch` | A click on Firefox. The icon bounces while Firefox starts, the window opens, and the running dot appears. The window then closes. |
| `attention` | Three `notify-send` notifications for Konsole. The badge counts 1, 2, 3, the Wiggle animation plays, and the badge clears when the notifications close. |
| `wheel` | Three Dolphin windows. Scrolling over the Dolphin icon brings each window to the front in turn. |
| `middle` | A middle click on the running Konsole opens a second Konsole window. Krema's hover popup flashes briefly with placeholder icons (see Limitations), so this clip is for review only. |
| `reorder` | Kate is dragged two places to the right and then back. |
| `settings` | Icon size 72 → 88 → 72 with the spin box in Krema's settings window. The dock resizes live. |
| `styles` | Background style Tinted, Acrylic, Transparent, then Panel Inherit, picked in the settings window. |
| `dodge` | Dodge mode: a Kate window dragged over the dock makes it slide away, and dragging it back brings the dock back. |
| `keyboard` | Meta+F5 focuses the dock, the arrow keys move the focus, and Escape leaves. |
| `autohide` | Auto-hide: the dock slides in at the bottom edge, zooms under the pointer, and slides out after the pointer leaves. |

Sizes and durations are printed by `encode.sh`. Clips are 2560×1280 (2×
density of a 1280×640 logical crop), 60 fps constant frame rate, no audio.
Each one starts and ends in the same resting state, so it loops cleanly.
Posters (`.webp`) are the first frame.

## The launch-clip jitter (root cause)

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

## Environment

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

## How it works

| File | Role |
|---|---|
| `Dockerfile` | Fedora 44 + KWin/Plasma/apps + Xvfb, xdotool, ffmpeg, libfaketime. |
| `session.sh` | Entrypoint: slowed clock, PipeWire, Xvfb, KWin, output scale, `/tmp/session.env`. |
| `clips.sh` | `setup`, then one `clip_*` function per clip that drives input with `xdotool` and records. Any helper can be called by name (`clips.sh open_settings geometry`). |
| `kwinscript.sh` | Runs a KWin script over D-Bus (`clips.sh` generates small scripts to place and close windows). |
| `encode.sh` | Host side: trim, 60 fps CFR, encode WebM/MP4/WebP. |

Recording: `ffmpeg -f x11grab -framerate 120 -video_size 2560x1280 -i
:99+0,160 -draw_mouse 1 -c:v utvideo` writes a lossless capture of the bottom
640 logical px of the screen, including the pointer.

Meta+F5: xdotool's Super modifier does not reach KWin's global shortcut
handling in the X11-windowed backend (Right, Left and Escape do arrive), so
`clip_keyboard` triggers Krema's registered `focus-dock` action, the action
Meta+F5 is bound to, through kglobalaccel's `invokeShortcut`.

### Regenerate

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

Encoding: `fps=60`, Lanczos scale, BT.709 limited-range yuv420p. VP9
(`-crf` 28…40, `-deadline good -cpu-used 2 -row-mt 1`) and H.264 High
(`-crf` 20…30, `-preset veryslow`, faststart). For each clip `encode.sh`
takes the lowest CRF that fits 1.45 MB, and at 2560 wide first. If a clip
does not fit, it falls back to 1920 and then 1600 wide.

## Limitations

- **No window previews, no grouped-window popup.** KWin composites with
  QPainter here and refuses screencasts: `kwin_screencast: error creating
  screencast "Unsupported compositing type"`. OpenGL compositing needs a DRM
  render node. This host (Docker on macOS, OrbStack kernel 7.0.14) has no
  `/dev/dri`: `/sys/class/drm` only holds `version`, the kernel has no vgem or
  vkms driver, and the built-in virtio-gpu driver has no device. Xvfb has no
  DRI3, so the X11-windowed backend cannot create an EGL display either.
  Filming previews needs a host with a GPU or vgem/vkms.
- **No progress-bar clip.** Krema creates `SmartLauncherItem` from the
  `org.kde.plasma.private.taskmanager` QML module. Plasma 6.7 builds that
  module into the task-manager applet plugin, so it no longer exists under
  `qt6/qml`. The import fails silently, and Unity LauncherEntry progress
  (sent with real D-Bus signals) never shows. Krema needs a fix for this.
- **Placeholder popup flashes in `launch` and `middle`.** `main.qml`'s
  `onRowsInserted` opens the hover popup when a hovered entry gains a window,
  even with `PreviewEnabled=false`. The scripts move the pointer off the
  icon right after the click, but the popup still shows for a few frames.
- **Firefox opens full-screen in `launch`.** The KWin rule (position, size,
  maximize forced off) does not hold Firefox 156 at 620×440.
- **No `pin` clip yet.** The drag from Dolphin reaches the dock but the drop
  target (`icon_x 7 + 50`) is outside the dock panel, so nothing is pinned.
  `clip_pin` needs a drop point inside the panel.
- **No blur.** QPainter compositing has no Blur or Background Contrast
  effect, so Acrylic in `styles` shows its tint without blur.
- **Trim points depend on the recording.** Re-check `START`/`DUR` in
  `encode.sh` with a contact sheet after regenerating.
