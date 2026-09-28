# KWin Scripting over D-Bus

Verified against the KWin 6.7.5 sources (`src/scripting/scripting.{h,cpp}`,
`src/scripting/workspace_wrapper.h`, `src/scripting/org.kde.kwin.Script.xml`).
No D-Bus introspection XML for these interfaces is installed.

Krema uses this to learn about global pointer motion: Wayland delivers
pointer events only to the surface under the pointer, so a client cannot see
the pointer moving over other windows (`src/platform/kwinpointermotionwatcher.cpp`).

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
