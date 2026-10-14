# KWin Scripting over D-Bus

Verified against the KWin 6.7.5 sources (`src/scripting/scripting.{h,cpp}`,
`src/scripting/workspace_wrapper.h`, `src/scripting/org.kde.kwin.Script.xml`).
No D-Bus introspection XML for these interfaces is installed.

Krema uses this to learn about global pointer motion: Wayland delivers
pointer events only to the surface under the pointer, so a client cannot see
the pointer moving over other windows (`src/platform/kwinpointermotionwatcher.cpp`).

It also uses it to find the panels on the dock's edge
(`src/platform/kwinpanelwatcher.cpp`). On Wayland no client API returns other
surfaces' geometry or the work area: `QScreen::availableGeometry()` equals
`geometry()`, `KX11Extras::workArea()` is X11-only, and
`org_kde_plasma_window_management` skips layer-shell surfaces.

## D-Bus API (service `org.kde.KWin`)

| Object / interface | Method | Notes |
|---|---|---|
| `/Scripting`, `org.kde.kwin.Scripting` | `int loadScript(QString filePath, QString pluginName = {})` | Returns the script id, or `-1` if `pluginName` is already loaded. The script is not started. |
| `/Scripting`, `org.kde.kwin.Scripting` | `bool unloadScript(QString pluginName)` | `deleteLater()`: the name stays taken until KWin's event loop runs, so reuse a fresh name when reloading. |
| `/Scripting`, `org.kde.kwin.Scripting` | `bool isScriptLoaded(QString pluginName)` | |
| `/Scripting`, `org.kde.kwin.Scripting` | `void start()` | Re-reads kwinrc and (re)loads every *installed* script before running all — avoid for ad-hoc scripts. |
| `/Scripting/Script<id>`, `org.kde.kwin.Script` | `run()`, `stop()` | `run()` reads the file in a worker thread (keep it until then). |

Caveat: the id is `scripts.size()` at load time, so after an unload a new
script can get the id (and object path) of a script that is still loaded;
`registerObject` then fails for the new one. Harmless when scripts are loaded
and unloaded one at a time.

The file is read by KWin's process with `QFile`: it must be a real file
(no `qrc:`), readable by the session user.

## JavaScript API used

- `workspace.cursorPos` (`QPoint`), signal `workspace.cursorPosChanged()`
  (connected to `Cursor::posChanged`, fires on every pointer move, also
  fake-input moves).
- `callDBus(service, path, interface, method, args..., callback?)` — async;
  target the client's unique name (`QDBusConnection::baseService()`).
- Signals support `.connect(fn)` / `.disconnect(fn)`.

Panel watcher (verified in the KWin 6.0, 6.3, 6.4 and 6.7 sources, and
against a real plasmashell 6.7.5 panel on `kwin_wayland --virtual`):

- `workspace.windowList()`, signals `windowAdded(w)`, `windowRemoved(w)`,
  `screensChanged()`, `virtualScreenGeometryChanged()`.
- `workspace.screens`: outputs with `name` and `geometry` (`QRect`).
- Window properties `dock` (layer-shell scope `"dock"`; any other scope is a
  normal window), `pid`, `deleted`, `output` (signal `outputChanged`),
  `frameGeometry` (`QRectF`, signal `frameGeometryChanged`). Layer-shell
  panels are in `windowList()`; a floating Plasma panel's frame includes its
  floating gap (44 px panel: 60 px surface, 44 px reserved).
- `new QTimer()` (`singleShot`, `interval`, `timeout`, `start()`) works in JS
  scripts; used to debounce reports.
- There is no per-window exclusive-zone property and no signal when only the
  reserved space changes (e.g. a panel switching to auto-hide), and
  `clientArea(KWin.MaximizeArea, …)` can still be stale inside
  `frameGeometryChanged`. Krema's probe surface (below) turns reservation
  changes into a `frameGeometryChanged` of the probe.

### Probe surface for reserved space

A 1 px layer surface on the **Bottom** layer, anchored to one edge only, is
placed by KWin directly inside the space every always-visible panel reserves
on that edge, regardless of start order, and KWin moves it when that space
changes (auto-hide on/off, panel height, plasmashell restart). On the Top
layer the same surface, started first, takes the screen edge and pushes the
panel inward instead. Panels that auto-hide, dodge windows or let windows go
below reserve nothing, so the probe stays at the screen edge.

A layer surface anchored to an edge and both perpendicular edges (like the
dock) is placed before a custom-length panel on that edge whatever the start
order, so a reserving dock pushes such a panel inward; with exclusive zone
`-1` it stays at the screen edge and leaves the panel alone.

Along the edge, KWin centres the probe in the area left by panels on the
perpendicular edges, so its position along the edge says nothing; Krema makes
it 2x1 on a horizontal edge and 1x2 on a vertical one so a report shows which
edge it was placed for (a report from before an edge change is ignored).

Moving a reservation between two surfaces must happen in one KWin
re-arrange: commit both mapped surfaces in the same flush (or commit the new
holder and destroy the old one together). Two quick re-arranges (the
reservation briefly gone, or briefly doubled) can leave an already-maximized
window at the intermediate size even after MaximizeArea is correct again
(seen on KWin 6.3-6.7 in Distro E2E).
