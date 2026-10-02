# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Deterministic application windows (``krema-test-window`` fixture binary).

One process = one toplevel with a chosen title and Wayland app_id. The two
built-in app ids have installed .desktop files, so krema resolves their
names/icons:

* ``org.kde.krema.testwindow``  -> "Krema Test Window" (utilities-terminal)
* ``org.kde.krema.testwindow2`` -> "Krema Second Test Window" (accessories-text-editor)

Any other app id works too (e.g. ``org.kde.kwrite`` to pose as KWrite).
"""

from __future__ import annotations

import os
import signal
import subprocess
from dataclasses import dataclass, field

from . import env, kwin
from .waits import KWIN_POLL_INTERVAL, wait_until


@dataclass
class TestWindow:
    """A running fixture window. ``window`` is the KWin snapshot taken when
    it appeared; call :meth:`refresh` for current state."""

    __test__ = False  # not a pytest test class

    title: str
    app_id: str
    process: subprocess.Popen
    window: kwin.Window | None = field(default=None)

    @property
    def pid(self) -> int:
        return self.process.pid

    @property
    def internal_id(self) -> str:
        assert self.window is not None
        return self.window.internal_id

    def refresh(self) -> kwin.Window | None:
        """Current KWin state of this window, or None if it is gone."""
        return next((w for w in kwin.windows() if w.pid == self.pid and w.normal_window), None)

    def is_active(self) -> bool:
        w = self.refresh()
        return bool(w and w.active)


class TestWindows:
    """Opens/closes fixture windows and cleans up whatever is left."""

    __test__ = False

    def __init__(self) -> None:
        self._open: list[TestWindow] = []

    def open(
        self,
        title: str,
        app_id: str = env.TEST_APP_ID,
        width: int = 400,
        height: int = 300,
        badge: int | None = None,
        accessible: bool = False,
        color: str | None = None,
        timeout: float = 10.0,
        icon_path: str | os.PathLike[str] | None = None,
    ) -> TestWindow:
        """Start a window and wait until KWin maps it. ``accessible`` exposes
        its widgets on the AT-SPI bus (off by default to keep the bus quiet).
        ``badge`` sends a Unity LauncherEntry count for ``app_id``. ``color``
        (a QColor name, e.g. ``"red"``) fills the window content solidly.
        ``icon_path`` supplies a local test icon to the fixture's Qt window;
        it is a raw-render test stimulus, not a claim about Wayland app_id
        semantics."""
        args = [env.TEST_WINDOW_BINARY, "--app-id", app_id, "--title", title, "--width", str(width), "--height", str(height)]
        if badge is not None:
            args += ["--badge", str(badge)]
        if color is not None:
            args += ["--color", color]
        if icon_path is not None:
            args += ["--icon-path", os.fspath(icon_path)]
        child_env = dict(os.environ)
        if accessible:
            child_env.update(QT_ACCESSIBILITY="1", QT_LINUX_ACCESSIBILITY_ALWAYS_ON="1")
        else:
            # Any value of QT_LINUX_ACCESSIBILITY_ALWAYS_ON enables the bridge;
            # an unusable bus address keeps the window off the a11y bus (same
            # trick as plasma-desktop's appium tests).
            child_env.pop("QT_LINUX_ACCESSIBILITY_ALWAYS_ON", None)
            child_env["AT_SPI_BUS_ADDRESS"] = "unix:path=/nonexistent"
        log = open(env.artifact_path("test-windows.log"), "ab")
        proc = subprocess.Popen(args, env=child_env, stdout=log, stderr=subprocess.STDOUT)
        log.close()
        tw = TestWindow(title=title, app_id=app_id, process=proc)
        self._open.append(tw)
        tw.window = wait_until(
            lambda: next((w for w in kwin.windows() if w.pid == proc.pid and w.title == title), None),
            timeout=timeout,
            interval=KWIN_POLL_INTERVAL,
            message=f"test window {title!r} to be mapped by KWin",
        )
        return tw

    def close(self, tw: TestWindow, timeout: float = 10.0) -> None:
        """SIGTERM the window's process and wait until KWin drops it."""
        self._close([tw], timeout)

    def close_all(self) -> None:
        """Close every window still open, all at once: signal all of them
        before waiting, so their shutdowns overlap."""
        try:
            self._close(list(self._open), 10.0)
        except Exception:  # noqa: BLE001 - best-effort teardown
            for tw in self._open:
                tw.process.kill()
        self._open.clear()

    def _close(self, tws: list[TestWindow], timeout: float) -> None:
        if not tws:
            return
        mapped = {w.pid for w in kwin.windows() if w.normal_window}
        for tw in tws:
            if tw.process.poll() is None:
                # A window closed from outside (krema's Close, the preview's
                # close button) is already gone from KWin, but its process
                # can stay stuck in Qt shutdown (main and Wayland event
                # threads blocked on futexes), where the fixture's SIGTERM
                # -> QCoreApplication::quit has no effect. It has nothing
                # left to close, so it is only reaped.
                tw.process.send_signal(signal.SIGTERM if tw.pid in mapped else signal.SIGKILL)
        for tw in tws:
            try:
                tw.process.wait(timeout)
            except subprocess.TimeoutExpired:
                tw.process.kill()
                tw.process.wait(5)
        pids = {tw.pid for tw in tws}
        wait_until(
            lambda: not any(w.pid in pids and w.normal_window for w in kwin.windows()),
            timeout=timeout,
            interval=KWIN_POLL_INTERVAL,
            message=lambda: f"windows {[tw.title for tw in tws]!r} to disappear",
        )
        for tw in tws:
            if tw in self._open:
                self._open.remove(tw)

    @property
    def open_windows(self) -> list[TestWindow]:
        return list(self._open)

    @staticmethod
    def key_presses(title: str | None = None) -> list[tuple[str, int, int]]:
        """Key presses the fixture windows received, oldest first, as
        ``(window title, Qt key, Qt modifiers)``. A combo KWin consumed as a
        global shortcut does not show up here."""
        path = env.artifact_path("test-windows.log")
        if not path.exists():
            return []
        out = []
        for line in path.read_text(errors="replace").splitlines():
            if not line.startswith("key-press "):
                continue
            head, key, mods = line[len("key-press ") :].rsplit(" ", 2)
            if title is None or head == title:
                out.append((head, int(key.split("=")[1], 16), int(mods.split("=")[1], 16)))
        return out
