# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""KWin as the oracle: window list/state via KWin scripting, screenshots via
ScreenShot2.

KWin scripts cannot return values over D-Bus directly, so :func:`evaluate`
exports a one-shot D-Bus object in this process and the script reports back
with ``callDBus()``.
"""

from __future__ import annotations

import base64
import itertools
import json
import os
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from gi.repository import Gio, GLib

from . import dbus

_IFACE_XML = """
<node>
  <interface name="org.kde.krema.e2e.Oracle">
    <method name="result">
      <arg type="s" name="token" direction="in"/>
      <arg type="s" name="json" direction="in"/>
    </method>
  </interface>
</node>
"""
_ORACLE_PATH = "/Oracle"
_ORACLE_IFACE = "org.kde.krema.e2e.Oracle"
_counter = itertools.count()
_results: dict[str, str] = {}
_registered = False


def _ensure_oracle() -> str:
    global _registered
    bus = dbus.session_bus()
    if not _registered:
        info = Gio.DBusNodeInfo.new_for_xml(_IFACE_XML).interfaces[0]

        def on_call(_conn, _sender, _path, _iface, method, params, invocation):
            token, payload = params.unpack()
            _results[token] = payload
            invocation.return_value(None)

        bus.register_object(_ORACLE_PATH, info, on_call, None, None)
        _registered = True
    return bus.get_unique_name()


def _pump_until(token: str, timeout: float) -> str:
    ctx = GLib.MainContext.default()
    deadline = time.monotonic() + timeout
    while token not in _results:
        if time.monotonic() > deadline:
            raise TimeoutError(f"KWin script {token} did not report back within {timeout}s")
        if not ctx.iteration(False):
            time.sleep(0.005)
    return _results.pop(token)


def evaluate(js: str, timeout: float = 10.0) -> Any:
    """Run ``js`` inside KWin's scripting engine and return what it reports.

    The script body gets a ``report(value)`` function; call it exactly once
    with any JSON-serializable value. KWin's JS API (``workspace``,
    ``workspace.windowList()``, ``workspace.activeWindow`` ...) is available.
    """
    service = _ensure_oracle()
    token = f"t{os.getpid()}_{next(_counter)}"
    plugin = f"krema_e2e_{token}"
    script = (
        "function report(v) {"
        f" callDBus({json.dumps(service)}, {json.dumps(_ORACLE_PATH)}, {json.dumps(_ORACLE_IFACE)},"
        f" 'result', {json.dumps(token)}, JSON.stringify(v === undefined ? null : v)); }}\n"
        "try {\n" + js + "\n} catch (e) { report({__error__: String(e)}); }\n"
    )
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
        f.write(script)
        path = f.name
    try:
        script_id = dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "loadScript", "ss", path, plugin)
        dbus.call("org.kde.KWin", f"/Scripting/Script{script_id}", "org.kde.kwin.Script", "run")
        payload = _pump_until(token, timeout)
    finally:
        try:
            dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", "s", plugin)
        except GLib.Error:
            pass
        os.unlink(path)
    value = json.loads(payload)
    if isinstance(value, dict) and "__error__" in value:
        raise RuntimeError(f"KWin script error: {value['__error__']}")
    return value


@dataclass(frozen=True)
class Window:
    """Snapshot of one KWin window (including layer-shell surfaces)."""

    internal_id: str
    title: str
    #: Wayland app_id for Qt apps == desktop file id (e.g. org.kde.krema.testwindow)
    app_id: str
    resource_class: str
    pid: int
    active: bool
    minimized: bool
    #: frameGeometry (incl. server-side decoration) in global screen pixels
    x: int
    y: int
    width: int
    height: int
    #: clientGeometry (the Wayland surface; AT-SPI coordinates are relative to it)
    client_x: int
    client_y: int
    client_width: int
    client_height: int
    #: KWin's "normal window" type. Note: krema's layer-shell surfaces (dock,
    #: preview) also report True; they have skip_taskbar=True and no desktops.
    normal_window: bool
    dock: bool
    special_window: bool
    skip_taskbar: bool
    desktops: tuple[str, ...]
    output: str

    @property
    def geometry(self) -> tuple[int, int, int, int]:
        return (self.x, self.y, self.width, self.height)

    @property
    def client_geometry(self) -> tuple[int, int, int, int]:
        return (self.client_x, self.client_y, self.client_width, self.client_height)


_WINDOWS_JS = """
const active = workspace.activeWindow;
report(workspace.windowList().map(w => ({
  internal_id: String(w.internalId),
  title: w.caption,
  app_id: w.desktopFileName || '',
  resource_class: String(w.resourceClass || ''),
  pid: w.pid,
  active: w === active,
  minimized: !!w.minimized,
  x: Math.round(w.frameGeometry.x), y: Math.round(w.frameGeometry.y),
  width: Math.round(w.frameGeometry.width), height: Math.round(w.frameGeometry.height),
  client_x: Math.round(w.clientGeometry.x), client_y: Math.round(w.clientGeometry.y),
  client_width: Math.round(w.clientGeometry.width), client_height: Math.round(w.clientGeometry.height),
  normal_window: !!w.normalWindow,
  dock: !!w.dock,
  special_window: !!w.specialWindow,
  skip_taskbar: !!w.skipTaskbar,
  desktops: (w.desktops || []).map(d => String(d.id)),
  output: w.output ? String(w.output.name) : '',
})));
"""


def windows() -> list[Window]:
    """Every window KWin manages, in stacking order (bottom to top)."""
    return [Window(**{**w, "desktops": tuple(w["desktops"])}) for w in evaluate(_WINDOWS_JS)]


def app_windows() -> list[Window]:
    """Application toplevels as a taskbar would list them (normal windows that
    do not skip the taskbar), e.g. fixture windows and krema's Settings."""
    return [w for w in windows() if w.normal_window and not w.skip_taskbar]


def active_window() -> Window | None:
    """The window KWin considers active, or None."""
    return next((w for w in windows() if w.active), None)


def activate(internal_id: str) -> None:
    """Make KWin activate a window (setup helper, not an assertion)."""
    evaluate(
        f"const w = workspace.windowList().find(w => String(w.internalId) === {json.dumps(internal_id)});"
        "if (w) { w.minimized = false; workspace.activeWindow = w; } report(!!w);"
    )


def set_minimized(internal_id: str, minimized: bool) -> None:
    """Minimize or unminimize a window through KWin (setup helper)."""
    evaluate(
        f"const w = workspace.windowList().find(w => String(w.internalId) === {json.dumps(internal_id)});"
        f"if (w) w.minimized = {json.dumps(minimized)}; report(!!w);"
    )


def cursor_pos() -> tuple[int, int]:
    """Pointer position as KWin sees it."""
    p = evaluate("report({x: workspace.cursorPos.x, y: workspace.cursorPos.y});")
    return (int(p["x"]), int(p["y"]))


class ScreenshotUnavailable(RuntimeError):
    """KWin cannot capture: it composites with QPainter because the container
    has no DRM render node (/dev/dri). See README "Screenshots and previews"."""


def compositing_type() -> str:
    """KWin's compositing type from supportInformation: "OpenGL" (render node
    available: screenshots and PipeWire screencasts work) or "QPainter"."""
    info = dbus.call("org.kde.KWin", "/KWin", "org.kde.KWin", "supportInformation")
    for line in info.splitlines():
        if line.startswith("Compositing Type:"):
            return line.split(":", 1)[1].strip()
    return "unknown"


def can_capture() -> bool:
    """Whether ScreenShot2 and screencasting (KPipeWire thumbnails) work."""
    return compositing_type() != "QPainter"


def screenshot(path: str | Path) -> Path:
    """Capture the whole virtual screen via KWin ScreenShot2 (through the
    webdriver's screenshotter helper) and write a PNG to ``path`` (parents
    created). Raises ScreenshotUnavailable when KWin runs without OpenGL."""
    if not can_capture():
        raise ScreenshotUnavailable(
            "KWin is compositing with QPainter (no DRM render node in the container); "
            "ScreenShot2 needs OpenGL. Provide /dev/dri (e.g. modprobe vgem) to enable screenshots."
        )
    proc = subprocess.run(["selenium-webdriver-at-spi-screenshotter", "0", "0", "0", "0"], capture_output=True, timeout=30)
    if proc.returncode != 0 or not proc.stdout:
        raise RuntimeError(f"screenshot failed: {proc.stderr.decode(errors='replace')[-2000:]}")
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(base64.b64decode(proc.stdout))
    return out
