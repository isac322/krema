# Minimized Live Previews KWin Effect

This experimental KWin effect asks the compositor to keep minimized windows
renderable, allowing Krema's PipeWire previews to keep receiving frames when
KWin and the application continue rendering them. It does not change a
window's minimized, focus, or activation state.

Native Krema packages include the effect. KWin enables it by default for
setups without a saved preference; users can turn it off under **System
Settings → Window Management → Desktop Effects → Minimized Live Previews**.
Log out and back in after installing Krema so the running KWin session loads
the new effect. Flatpak builds omit it because a sandboxed application cannot
install a plugin into the host compositor.

## Build from source

The root Krema build includes and installs this effect by default. Install the
KWin development files matching the Plasma/KWin version used by the session,
then use the normal Krema build and install steps. To build only Krema without
the effect, pass `-DKREMA_BUILD_KWIN_EFFECT=OFF` to CMake.

For a standalone user-local install:

```bash
cmake -S kwin-minimized-live-preview \
  -B build/kwin-minimized-live-preview \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build/kwin-minimized-live-preview
cmake --install build/kwin-minimized-live-preview
```

The standalone build needs CMake, Extra CMake Modules, KF6 CoreAddons, Qt 6
Core and Widgets development files, and the KWin development package. Restart
the Plasma session after installing it.

## Limits

This effect holds an offscreen-rendering reference while a window is
minimized, then releases it when the window is restored, closed, or the effect
unloads. It cannot guarantee that every application or graphics driver
continues rendering. If KWin provides no new frames, Krema shows the last
frame it captured. Keeping minimized video windows renderable can increase
CPU/GPU load and battery use. Since the effect uses KWin's C++ API, build it
against the KWin development package matching the target system.
