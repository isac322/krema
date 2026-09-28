# Plasma panel reference captures

Real captures that section R of the landing page (`website/#roots`,
`css/panel.css`, `js/panel.js`) reproduces in HTML/CSS. The session and
image are the same as `branding/clips/` (`krema-clips` image, `session.sh`,
Fedora 44, KWin/plasma-workspace 6.7.5, Qt 6.11), with `SPEED=1` and
`plasma-nm plasma-pa NetworkManager upower glibc-langpack-en` added.
Captures were taken with `ffmpeg -f x11grab` at `SCALE=1` and `SCALE=2`
on a 1280×720 logical screen.

## Session setup

- Fresh `plasmashell`, so the stock `org.kde.plasma.desktop.defaultPanel`
  layout: Kickoff, Pager, Icons-only Task Manager, margins separator,
  System Tray, Digital Clock, Peek at Desktop.
- Breeze Dark: `LookAndFeelPackage=org.kde.breezedark.desktop`, colour scheme
  BreezeDark, Plasma style `default` (Breeze, follows the colour scheme),
  icons `breeze-dark`. Locale `en_US.UTF-8`. Wallpaper `Next` (Plasma picks
  the dark variant, `images_dark/5120x2880.png`).
- Deviations from a bare first boot, all needed to show what a normal
  desktop shows:
  - 2 virtual desktops. With 1 desktop Plasma 6 hides the Pager.
  - Task Manager launchers set to the apps Krema's clips use: Dolphin,
    Firefox, Konsole, Kate, Okular, Gwenview, KCalc, System Settings (the
    stock list is System Settings, Discover, file manager, browser).
  - Tray `shownItems=battery,clipboard,notifications`. The container has no
    battery, so no battery icon appears. With no audio device and no
    network, volume shows muted and network disconnected.
  - Dolphin and Konsole windows open above y=280 (Konsole active), so the
    task buttons show running and active states and the panel stays
    floating.

## Files

| File | Content |
|---|---|
| `plasma-panel-{1,2}x.png` | Screen y 600–720: the panel at rest |
| `plasma-hover-{firefox,dolphin,konsole}-{1,2}x.png` | x 0–720, y 580–720: pointer on a launcher, a running task, the active task (the KWin pointer is in the image) |
| `plasma-clock-tooltip-{1,2}x.png` | Clock hover tooltip |
| `krema-dock-{1,2}x.png` | Krema (this repo, default settings, same 8 launchers, panel set to auto-hide), y 580–720 |
| `krema-hover-konsole-{1,2}x.png` | Krema zoom with the pointer on Konsole |

## Measurements (logical px)

Panel: floating, 8 px from the left, right and bottom edges, 46 px thick
(`2 × ceil(2.5 × gridUnit / 2)` with gridUnit 18; not 44), radius 5.
Background `#202326` at 84.6% opacity, 1 px border close to opaque
(`rgb(32 36 39 / .99)`, top `.92`). No shadow, no blur here: KWin composites
with QPainter in this container, so the blur and background contrast that a
GPU session adds behind the panel are absent.

| Item | Geometry (from the panel's outer left/top) | Colours |
|---|---|---|
| Kickoff | `start-here-kde-plasma-symbolic` 22 px SVG drawn at 32 px, x 11, y 7 | `#fcfcfc` |
| Pager | 2 cells 67×38, x 50 and 118, y 4 | current `#3daee9` @ 60%, other `#fcfcfc` @ 9.5%; window rects 1 px border |
| Task buttons | 52×46 (full height, over the border), pitch 54, first at x 190; icons 32 px, centred | running `#3b4753` + 3 px top line `#636c76`; active `#2a6486` + `#3aa0d5`; running or active under the pointer `#265572` + `#3daee9`; hovered icon lightens about 25% toward white |
| Task tooltip | panel frame, bottom 1 px above the panel, centred on the button | title 16 px (Qt), subtitle 13 px at 70% |
| Tray | 22 px symbolic icons, centres 260, 230, 200, 170, 142 px from the right edge | `#fcfcfc` |
| Clock | time Noto Sans 17 px, date 13 px, centred 86 px from the right | `#fcfcfc` |
| Peek at Desktop | `user-desktop-symbolic` 22 px, centre 24 px from the right | |

Krema defaults, measured: dock 428×64 (8 icons of 48, spacing 4, padding 8),
radius 12, `rgb(41 43 48 / .6)`, 8 px from the bottom. Icons of inactive
windows and launchers at 80% opacity. Icon normalization scales the Breeze
icons by 1.04–1.175. Running dots 3 px (active 4 px), 4 px under the icon.
Zoom in these captures: Gaussian, σ = 1.2 × icon size, max 1.6, scale from
the item's bottom centre, neighbours do not move (the in-place zoom Krema had
before PR #43). Tooltip 8 px above the dock, shows the window title for
running apps. The site now models the Parabolic default from PR #43
(`src/utils/zoomcalculator.h` `computeDockZoom`): neighbours move aside and
the background grows. `krema-hover-konsole-*.png` therefore no longer matches
the site's zoomed state; recapture it with a build that includes PR #43.

## HTML vs capture

`/tmp/krema-brand-preview/lab/panel/index.html` (review page) shows each
capture next to a headless Chromium render and a difference map. Mean
absolute error per channel (0–255) at 2x: whole panel band 1.1, Kickoff 0.4,
Pager 0.6, tasks 1.3, tray 0.7, Peek 0.1, clock 11.9 (text rasterization:
Qt draws RGB subpixel, hinted glyphs; Chrome draws greyscale), Krema at rest
1.5, Krema zoomed 6.0 (the pointer and the tooltip text differ). 1x is within
0.7 of these numbers.

Known differences on the site: volume and network use the
`audio-volume-high` and `network-wired-activated` icons, since the capture
had neither device; the Krema tooltip shows the app name; the Plasma tooltip
of a running task has no window thumbnail (screencasting is unavailable in
this container, so the capture shows an empty one); the clock shows the
visitor's local time.

## Licences

Breeze icons: LGPL-3.0-or-later (KDE). Dolphin icon: LGPL-3.0-only. The Next
wallpaper in these captures: Krystian Zajdel, CC BY-SA 4.0 (the site now uses
Roast Contours, `branding/wallpaper/`). Noto Sans: OFL-1.1. See
`website/media/breeze/NOTICE.txt`.
