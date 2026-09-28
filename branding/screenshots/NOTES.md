# Krema product screenshots

Real captures of Krema running in a headless KDE Plasma 6 session. Nothing in these images is drawn or composited by hand: the wallpaper is rendered by `plasmashell`, the dock by Krema, and every frame is a single grab of the compositor output.

| File | Shows | Size |
|---|---|---|
| `dock-overview.png` | Desktop with Dolphin and Konsole open and Kate minimized. The dock holds 10 pinned launchers; Dolphin, Konsole, and Kate have running indicators. | 1920×1080 |
| `dock-zoom.png` | Parabolic zoom (default `MaxZoomFactor` 1.6) with the pointer over Okular, including the in-scene tooltip | 1920×1080 |
| `settings.png` | The Krema settings window (FormCard UI, Appearance page). The Zoom K app icon shows in its title bar and on its dock entry; other windows are minimized and dimmed in the dock. | 1920×1080 |

All files are 8-bit RGB PNGs optimized with `nix shell nixpkgs#oxipng -c oxipng -o 4 --strip safe`.

The current PNGs come from the GPU session in `branding/clips/` (`stills.sh`, KWin on a Mali GPU at 1920×1080, scale 1, Krema from `origin/master`, Roast Contours wallpaper), with the same composition as below. That build has no brand app icon, so the settings window shows a generic "S" icon in the dock. The `regen/` pipeline below is the older Xvfb variant.

## Environment

- Container: `fedora:44` (linux/arm64, running under OrbStack on macOS)
- Krema: built from this repository's working tree (branch `feat/brand-identity`, base commit `33b0cfb` plus uncommitted brand-identity changes), version string 0.7.0. The build installs the hicolor `com.bhyoo.krema` app icon and sets it as the window icon. The `copr` target installs `krema` from COPR `isac322/krema` instead.
- KDE: KWin 6.7.5, plasma-workspace 6.7.5, Breeze (Fedora defaults: Breeze Light apps, Breeze icons, Noto Sans)
- Wallpaper: Roast Contours (`branding/wallpaper/roast-contours.png`) for the current PNGs; `regen/capture.sh` still sets Plasma's stock `Next`. The default Plasma panel is removed so Krema is the only dock.
- Resolution: 1920×1080 at scale 1
- Krema config (`~/.config/kremarc`): `IconSize=56`; `PinnedLaunchers` = Dolphin, Firefox, Konsole, Kate, Okular, Gwenview, Elisa, KCalc, System Monitor, System Settings. Everything else uses defaults.
- User `alex`, hostname `fedora`, so no root warnings or container IDs show up in the apps.

## How it works

`regen/` holds everything needed to reproduce the images:

- `Dockerfile`: Fedora 44 with KWin, Plasma, the demo apps, Xvfb, scrot, and xdotool. Two targets:
  - `local` (default) runs `dnf builddep packaging/obs/krema.spec`, builds from the named build context `krema-src` (`cmake -G Ninja -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF`), and copies the `cmake --install` output into the image.
  - `copr` installs the released package from COPR `isac322/krema`.
- `session.sh`: container entrypoint. It starts Xvfb on `:99` and runs `kwin_wayland --x11-display :99` as a nested X11-windowed compositor at 1920×1080.
- `capture.sh`: starts kactivitymanagerd, plasmashell, Krema, and the apps, then arranges windows and grabs the three frames into `/out`
- `kwinscript.sh`, `place.js`, `settings.js`: KWin scripts, run over D-Bus, that set window geometry and minimize windows

Pointer and keyboard input goes through `xdotool` on the Xvfb display, so KWin receives it as ordinary host-window input. Frames are grabbed from the Xvfb root window with `scrot`; `-p` includes the pointer in the zoom shot. Qt Quick uses `QT_QUICK_BACKEND=software`.

### Regenerate

```sh
# From the repository root. Local source (default):
docker build --build-context krema-src=. -t krema-shots:regen branding/screenshots/regen
# ...or the released COPR package:
# docker build --target copr -t krema-shots:regen branding/screenshots/regen
mkdir -p /tmp/krema-shots-out
docker run -d --name krema-shots --hostname fedora --shm-size 1g \
  -v "$PWD/branding/screenshots/regen:/regen:ro" -v "$PWD:/repo:ro" -v /tmp/krema-shots-out:/out \
  krema-shots:regen dbus-run-session -- bash /regen/session.sh
sleep 10   # wait for READY in `docker logs krema-shots`
docker exec krema-shots bash /regen/capture.sh
docker rm -f krema-shots
cp /tmp/krema-shots-out/*.png branding/screenshots/
nix shell nixpkgs#oxipng -c oxipng -o 4 --strip safe branding/screenshots/*.png
```

`/out` must be writable by uid 1000 (`chmod 777 /tmp/krema-shots-out` if needed). The committed PNGs came from exactly this pipeline with the `local` target; capturing takes about 70 s once the image is built. `/repo` supplies only the demo files that Konsole lists (`README.md`, `src/`, …). The hover and click coordinates in `capture.sh` assume the resolution, icon size, and launcher list above. If any of those change, recompute them: icon *i* is centered at `x = 690 + 60*i`, `y ≈ 1035`.

## Why Xvfb instead of `kwin_wayland --virtual` + ScreenShot2

`tests/docker/run-smoke.sh` runs `kwin_wayland --virtual` and calls `org.kde.KWin.ScreenShot2.CaptureWorkspace`. That only works on a host with a DRM render node. Docker on macOS has no `/dev/dri`, so KWin falls back to QPainter compositing (`supportInformation`: `Compositing Type: QPainter`), and every ScreenShot2 call then fails with:

```
Call failed: Screenshot got cancelled
(org.kde.KWin.ScreenShot2.Error.Cancelled)
```

KWin's EIS remote-input interface (the one kwin-mcp uses) also failed in this setup with `RuntimeError: No pointer device available from EIS`. Running KWin as an X11-windowed compositor on Xvfb avoids both problems: the composited output is an ordinary X window that scrot can grab, and xdotool drives input.

## Limitations

- **No window-preview shot**: with QPainter compositing, KWin refuses to create screencasts. Krema logs `error creating screencast "Unsupported compositing type"` even with PipeWire and WirePlumber running, so the preview popup shows only the app-icon placeholder. That shot is not shipped because a placeholder thumbnail would be misleading. To produce one, run the pipeline on a host with a GPU or render node (OpenGL compositing), start `pipewire` and `wireplumber` before Krema, and hover a running launcher (for example Dolphin at `x=690`) for more than `PreviewHoverDelay`.
- **No blur or GPU effects**: QPainter compositing has no Blur or Background Contrast, so the dock's translucent background shows the wallpaper without blur.
