# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""KWin as the oracle: window list/state via KWin scripting, screenshots via
ScreenShot2.

KWin scripts cannot return values over D-Bus directly, so :func:`evaluate`
queues a one-shot command in one persistent private KWin script. The script
reports the command result back to this process with ``callDBus()``.
"""

from __future__ import annotations

import atexit
import base64
import io
import itertools
import json
import os
import subprocess
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from uuid import uuid4

from PIL import Image
from gi.repository import Gio, GLib

from . import dbus, env

_IFACE_XML = """
<node>
  <interface name="org.kde.krema.e2e.Oracle">
    <method name="ready"/>
    <method name="result">
      <arg type="s" name="token" direction="in"/>
      <arg type="s" name="json" direction="in"/>
    </method>
    <method name="take">
      <arg type="s" name="token" direction="out"/>
      <arg type="s" name="source" direction="out"/>
    </method>
  </interface>
</node>
"""
_ORACLE_PATH = "/Oracle"
_ORACLE_IFACE = "org.kde.krema.e2e.Oracle"
_KGLOBALACCEL_SERVICE = "org.kde.kglobalaccel"
_KGLOBALACCEL_PATH = "/kglobalaccel"
_KGLOBALACCEL_IFACE = "org.kde.KGlobalAccel"
_KGLOBALACCEL_COMPONENT_IFACE = "org.kde.kglobalaccel.Component"
_counter = itertools.count()


class _Oracle:
    def __init__(self) -> None:
        self.bus = dbus.session_bus()
        self.registration_id = 0
        self.plugin: str | None = None
        self.script_path: str | None = None
        self.component_path: str | None = None
        self.action: str | None = None
        self.pending: tuple[str, str] | None = None
        self.current_token: str | None = None
        self.results: dict[str, str] = {}
        self.ready = False
        self.startup: dict[str, Any] = {
            "components": [], "matches": {}, "api_errors": {},
        }
        info = Gio.DBusNodeInfo.new_for_xml(_IFACE_XML).interfaces[0]

        def on_call(_conn, _sender, _path, _iface, method, params, invocation):
            if method == "ready":
                self.ready = True
                invocation.return_value(None)
                return
            if method == "result":
                token, payload = params.unpack()
                if token == self.current_token and token not in self.results:
                    self.results[token] = payload
                invocation.return_value(None)
                return
            if method == "take":
                command = self.pending
                self.pending = None
                invocation.return_value(
                    GLib.Variant("(ss)", command if command is not None else ("", ""))
                )
                return
            invocation.return_dbus_error(
                "org.kde.krema.e2e.Oracle.UnknownMethod",
                f"unknown method {method}",
            )

        self.registration_id = self.bus.register_object(
            _ORACLE_PATH, info, on_call, None, None
        )

    def _component_for_action(self) -> str | None:
        try:
            components = dbus.call(
                _KGLOBALACCEL_SERVICE,
                _KGLOBALACCEL_PATH,
                _KGLOBALACCEL_IFACE,
                "allComponents",
            )
        except GLib.Error as error:
            self.startup["api_errors"]["allComponents"] = str(error)
            return None
        self.startup["components"] = components
        self.startup["matches"] = {}
        for path in components:
            method = "shortcutNames"
            try:
                names = dbus.call(
                    _KGLOBALACCEL_SERVICE, path, _KGLOBALACCEL_COMPONENT_IFACE, method,
                )
                if self.action not in names:
                    continue
                self.startup["matches"][path] = None
                method = "allShortcutInfos"
                infos = dbus.call(
                    _KGLOBALACCEL_SERVICE, path, _KGLOBALACCEL_COMPONENT_IFACE, method,
                )
            except GLib.Error as error:
                self.startup["api_errors"][f"{path}/{method}"] = str(error)
                continue
            matching = [info for info in infos if info[0] == self.action]
            self.startup["matches"][path] = matching
            if any(not any(info[6]) for info in matching):
                return path
        return None

    def _wait_until_ready(self) -> None:
        ctx = GLib.MainContext.default()
        deadline = time.monotonic() + 10.0
        while True:
            if self.ready:
                component = self._component_for_action()
                if component is not None:
                    self.component_path = component
                    return
            if time.monotonic() > deadline:
                if not self.ready:
                    self._component_for_action()
                raise TimeoutError(
                    f"KGlobalAccel action {self.action} did not become ready within 10s; "
                    f"ready={self.ready}; startup={json.dumps(self.startup)}"
                )
            if not ctx.iteration(False):
                time.sleep(0.005)

    def start(self) -> None:
        token = uuid4().hex
        plugin = f"krema_e2e_oracle_{token}"
        self.action = f"krema_e2e_action_{token}"
        script = (
            "function dispatch() {\n"
            f"  callDBus({json.dumps(self.bus.get_unique_name())}, "
            f"{json.dumps(_ORACLE_PATH)}, {json.dumps(_ORACLE_IFACE)}, 'take', "
            "function(token, source) {\n"
            "    if (!token) return;\n"
            "    const report = function(value) {\n"
            f"      callDBus({json.dumps(self.bus.get_unique_name())}, "
            f"{json.dumps(_ORACLE_PATH)}, {json.dumps(_ORACLE_IFACE)}, "
            "'result', token, JSON.stringify(value === undefined ? null : value));\n"
            "    };\n"
            "    try { new Function('report', source)(report); }\n"
            "    catch (e) { report({__error__: String(e)}); }\n"
            "  });\n"
            "}\n"
            f"registerShortcut({json.dumps(self.action)}, {json.dumps(self.action)}, "
            "'', function() { dispatch(); });\n"
            f"callDBus({json.dumps(self.bus.get_unique_name())}, "
            f"{json.dumps(_ORACLE_PATH)}, {json.dumps(_ORACLE_IFACE)}, 'ready');\n"
        )
        try:
            file = tempfile.NamedTemporaryFile(
                "w", suffix=".js", prefix="krema-e2e-oracle-", delete=False
            )
            self.script_path = file.name
            with file:
                file.write(script)
            script_id = dbus.call(
                "org.kde.KWin",
                "/Scripting",
                "org.kde.kwin.Scripting",
                "loadScript",
                "ss",
                self.script_path,
                plugin,
            )
            if script_id < 0:
                raise RuntimeError(f"KWin did not load oracle script {plugin}")
            self.plugin = plugin
            dbus.call(
                "org.kde.KWin",
                f"/Scripting/Script{script_id}",
                "org.kde.kwin.Script",
                "run",
            )
            self._wait_until_ready()
            os.unlink(self.script_path)
            self.script_path = None
        except BaseException:
            self.close()
            raise

    def evaluate(self, js: str, timeout: float) -> Any:
        token = f"t{os.getpid()}_{next(_counter)}"
        self.current_token = token
        self.pending = (token, js)
        try:
            dbus.call(
                _KGLOBALACCEL_SERVICE,
                self.component_path,
                _KGLOBALACCEL_COMPONENT_IFACE,
                "invokeShortcut",
                "s",
                self.action,
            )
            payload = self._pump_until(token, timeout)
        finally:
            self.pending = None
            self.results.pop(token, None)
            self.current_token = None
        value = json.loads(payload)
        if isinstance(value, dict) and "__error__" in value:
            raise RuntimeError(f"KWin script error: {value['__error__']}")
        return value

    def _pump_until(self, token: str, timeout: float) -> str:
        ctx = GLib.MainContext.default()
        deadline = time.monotonic() + timeout
        while token not in self.results:
            if time.monotonic() > deadline:
                raise TimeoutError(
                    f"KWin script {token} did not report back within {timeout}s"
                )
            if not ctx.iteration(False):
                time.sleep(0.005)
        return self.results.pop(token)

    def close(self) -> None:
        if self.plugin is not None:
            try:
                dbus.call(
                    "org.kde.KWin",
                    "/Scripting",
                    "org.kde.kwin.Scripting",
                    "unloadScript",
                    "s",
                    self.plugin,
                )
            except GLib.Error:
                pass
            self.plugin = None
        try:
            if self.script_path is not None:
                try:
                    os.unlink(self.script_path)
                except FileNotFoundError:
                    pass
                self.script_path = None
        finally:
            if self.registration_id:
                self.bus.unregister_object(self.registration_id)
                self.registration_id = 0


_oracle: _Oracle | None = None
# D-Bus callbacks never acquire this gate while evaluate pumps their context.
_evaluation_lock = threading.Lock()


def _ensure_oracle() -> _Oracle:
    global _oracle
    if _oracle is None:
        oracle = _Oracle()
        oracle.start()
        _oracle = oracle
    return _oracle


def _close_oracle() -> None:
    global _oracle
    if _oracle is not None:
        _oracle.close()
        _oracle = None


atexit.register(_close_oracle)


def evaluate(js: str, timeout: float = 10.0) -> Any:
    """Run ``js`` inside KWin's scripting engine and return what it reports.

    The script body gets a ``report(value)`` function; call it exactly once
    with any JSON-serializable value. KWin's JS API (``workspace``,
    ``workspace.windowList()``, ``workspace.activeWindow`` ...) is available.
    """
    if not _evaluation_lock.acquire(blocking=False):
        raise RuntimeError("KWin oracle does not support overlapping evaluate calls")
    try:
        return _ensure_oracle().evaluate(js, timeout)
    finally:
        _evaluation_lock.release()


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


def bounds(*rects: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    """Bounding (x, y, width, height) of ``rects``, e.g. to capture the
    area several pixel checks read with one :func:`screenshot`."""
    x0 = min(r[0] for r in rects)
    y0 = min(r[1] for r in rects)
    x1 = max(r[0] + r[2] for r in rects)
    y1 = max(r[1] + r[3] for r in rects)
    return (x0, y0, x1 - x0, y1 - y0)


def screenshot(path: str | Path, area: tuple[int, int, int, int] | None = None) -> Image.Image:
    """Capture the screen via KWin ScreenShot2 (through the webdriver's
    screenshotter helper) and return it as an opaque RGB image in screen
    coordinates. The captured pixels are also written as PNG to ``path``
    (parents created). Raises ScreenshotUnavailable when KWin runs without
    OpenGL.

    Without ``area`` this is the whole active output (CaptureActiveScreen).
    With ``area`` (x, y, width, height, e.g. a Rect) KWin renders and reads
    back only that part of the screen (CaptureArea; the helper takes the area
    through tools/pipelined-tree.patch), several times cheaper on llvmpipe.
    The result is still a full-screen image, black outside ``area``, so
    callers index it with screen coordinates. The PNG then holds only
    ``area``. Inside ``area`` the pixels equal a full capture's for the
    areas the tests use (items, rows, menus, the popup, dock-sized bands;
    compared on debian-13 and fedora-43). On fedora-43 a 1024x400 preview
    surface or a 900x540 window differed from a full capture in a few
    antialiased corner pixels, so checks over such surfaces capture the
    whole screen.

    The image is flattened onto black: KWin 6.7 returns RGBA with the
    empty desktop transparent and translucent surfaces (the dock panel) at
    their own alpha, while KWin 6.3 returns the same frame already composited
    on black. Flattening makes every pixel oracle see the same image."""
    if not can_capture():
        raise ScreenshotUnavailable(
            "KWin is compositing with QPainter (no DRM render node in the container); "
            "ScreenShot2 needs OpenGL. Provide /dev/dri (e.g. modprobe vgem) to enable screenshots."
        )
    box = None
    if area is not None:
        box = (max(0, area[0]), max(0, area[1]), min(env.SCREEN_WIDTH, area[0] + area[2]), min(env.SCREEN_HEIGHT, area[1] + area[3]))
    flat = None
    if box is None or (box[2] > box[0] and box[3] > box[1]):
        # The helper's "0 0 0 0" means the active output.
        args = (box[0], box[1], box[2] - box[0], box[3] - box[1]) if box else (0, 0, 0, 0)
        proc = subprocess.run(["selenium-webdriver-at-spi-screenshotter", *map(str, args)], capture_output=True, timeout=30)
        if proc.returncode != 0 or not proc.stdout:
            raise RuntimeError(f"screenshot failed: {proc.stderr.decode(errors='replace')[-2000:]}")
        image = Image.open(io.BytesIO(base64.b64decode(proc.stdout))).convert("RGBA")
        flat = Image.new("RGB", image.size, (0, 0, 0))
        flat.paste(image, mask=image.getchannel("A"))
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    if box is None:
        flat.save(out, compress_level=1)
        return flat
    canvas = Image.new("RGB", (env.SCREEN_WIDTH, env.SCREEN_HEIGHT), (0, 0, 0))
    if flat is not None:
        canvas.paste(flat, box[:2])
    (canvas if flat is None else flat).save(out, compress_level=1)
    return canvas
