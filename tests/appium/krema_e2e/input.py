# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Real input via KWin's org_kde_kwin_fake_input protocol.

Every call builds a W3C WebDriver action chain and hands it to
``selenium-webdriver-at-spi-inputsynth`` (the same injector the webdriver's
``/actions`` endpoint uses), so events go through KWin's normal input
pipeline: pointer focus, enter/leave, hover, implicit grabs, keyboard focus.

All coordinates are **global screen pixels** (use ``Krema.to_screen()`` /
``Krema.item_center()`` to convert AT-SPI rects).
The inputsynth in the image is patched (tools/inputsynth-fixes.patch): every
``pointerMove`` is absolute, so several moves can share one chain (needed
for drags), and the W3C Meta key is Super (KDE's Meta).

All chains go to one long-lived ``inputsynth --stdin`` per session (see
:class:`_Inputsynth`), so they share one fake-input device, and a chain costs
a pipe round trip instead of a process start. The session holds one more
device open throughout (see :func:`hold_pointer_capability`), so the seat
never loses its pointer capability and clients never drop their
``wl_pointer``, also before the first chain and while that process restarts.
"""

from __future__ import annotations

import atexit
import itertools
import json
import subprocess
import tempfile
import threading
import time
from contextlib import contextmanager
from typing import Iterable, Iterator, Sequence

from selenium.webdriver.common.keys import Keys

from . import kwin
from .waits import wait_until

INPUTSYNTH = "selenium-webdriver-at-spi-inputsynth"

_BUTTONS = {"left": 0, "middle": 1, "right": 2, "back": 3, "forward": 4}

#: Last pointer position we moved to. Every chain first teleports here: an
#: inputsynth process interpolates from (0, 0) until its first move, and the
#: session's process may have been restarted since the last chain.
_pointer: tuple[int, int] | None = None

# Friendly key names -> W3C key values understood by inputsynth.
_KEY_ALIASES = {
    "meta": Keys.META,
    "super": Keys.META,
    "ctrl": Keys.CONTROL,
    "control": Keys.CONTROL,
    "alt": Keys.ALT,
    "shift": Keys.SHIFT,
    "return": Keys.RETURN,
    "enter": Keys.RETURN,
    "esc": Keys.ESCAPE,
    "escape": Keys.ESCAPE,
    "tab": Keys.TAB,
    "space": Keys.SPACE,
    "backspace": Keys.BACKSPACE,
    "delete": Keys.DELETE,
    "left": Keys.LEFT,
    "right": Keys.RIGHT,
    "up": Keys.UP,
    "down": Keys.DOWN,
    "home": Keys.HOME,
    "end": Keys.END,
    "pageup": Keys.PAGE_UP,
    "pagedown": Keys.PAGE_DOWN,
    **{f"f{i}": getattr(Keys, f"F{i}") for i in range(1, 13)},
}


def key_value(name: str) -> str:
    """W3C key value for a friendly name (``"Meta"``, ``"F5"``, ``"Escape"``,
    ``"a"``). Single characters are passed through."""
    if len(name) == 1:
        return name
    try:
        return _KEY_ALIASES[name.lower()]
    except KeyError:
        raise ValueError(f"unknown key name {name!r}") from None


class _Inputsynth:
    """One ``inputsynth --stdin`` process (tools/inputsynth-fixes.patch): a
    single fake-input device that runs every chain of the session. Each
    request is a JSON line ``{"id": n, "actions": [...]}``; the process
    answers ``{"id": n, "ok": true}`` once that chain has finished. Chains
    run concurrently in the process, so a chain started while an earlier
    one sits in a pause (e.g. Escape during a drag) runs right away."""

    def __init__(self) -> None:
        self._stderr = tempfile.TemporaryFile()
        self.proc = subprocess.Popen(
            [INPUTSYNTH, "--stdin"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self._stderr,
            text=True,
            bufsize=1,
        )
        self._ids = itertools.count(1)
        self._lock = threading.Lock()
        self._done: dict[int, threading.Event] = {}
        self._replies: dict[int, dict] = {}
        threading.Thread(target=self._read, name="inputsynth-replies", daemon=True).start()

    def _read(self) -> None:
        assert self.proc.stdout is not None
        for line in self.proc.stdout:
            try:
                reply = json.loads(line)
            except json.JSONDecodeError:
                reply = {"id": None, "ok": False, "error": f"unexpected output {line!r}"}
            with self._lock:
                if reply.get("id") is None:  # a request it could not parse: fail every chain in flight
                    for chain, done in self._done.items():
                        self._replies[chain] = reply
                        done.set()
                elif (done := self._done.get(reply["id"])) is not None:
                    self._replies[reply["id"]] = reply
                    done.set()
        # stdout closed: the process is gone; its chains in flight have no reply.
        with self._lock:
            for done in self._done.values():
                done.set()

    def _write(self, request: dict) -> None:
        assert self.proc.stdin is not None
        with self._lock:
            self.proc.stdin.write(json.dumps(request) + "\n")
            self.proc.stdin.flush()

    def _failure(self) -> RuntimeError:
        """The process died: its exit code and the end of its stderr."""
        try:
            code = self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            code = self.proc.wait()
        self._stderr.seek(0)
        return RuntimeError(f"inputsynth failed ({code}): {self._stderr.read().decode(errors='replace')[-2000:]}")

    def start(self, action_sets: Sequence[dict]) -> int:
        """Start a chain; returns its id for :meth:`resume` and :meth:`wait`."""
        chain = next(self._ids)
        with self._lock:
            self._done[chain] = threading.Event()
        try:
            self._write({"id": chain, "actions": list(action_sets)})
        except (BrokenPipeError, ValueError):  # ValueError: stdin already closed
            with self._lock:
                del self._done[chain]
            raise self._failure() from None
        return chain

    def resume(self, chain: int) -> None:
        """End the chain's pause marked ``"interruptible": True`` (the one in
        progress, or its next one). No-op once the chain has finished."""
        try:
            self._write({"resume": chain})
        except (BrokenPipeError, ValueError):
            pass  # the process is gone: wait() reports it

    def wait(self, chain: int, timeout: float) -> None:
        """Wait for the chain to finish. A chain still running after
        ``timeout`` seconds kills the process (the next chain starts a fresh
        one) and raises :class:`subprocess.TimeoutExpired`, like
        ``subprocess.run(timeout=...)``; a chain the process could not run or
        did not finish because it died raises :class:`RuntimeError`."""
        done = self._done[chain]
        finished = done.wait(max(0.0, timeout))
        with self._lock:
            del self._done[chain]
            reply = self._replies.pop(chain, None)
        if not finished:
            self.proc.kill()
            self.proc.wait()
            raise subprocess.TimeoutExpired([INPUTSYNTH, "--stdin"], timeout)
        if reply is None:
            raise self._failure()
        if not reply.get("ok"):
            raise RuntimeError(f"inputsynth failed: {reply.get('error')}")

    def close(self) -> None:
        """EOF on stdin: the process exits once its chains have finished."""
        try:
            assert self.proc.stdin is not None
            self.proc.stdin.close()
        except BrokenPipeError:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self._stderr.close()


_inputsynth: _Inputsynth | None = None
_inputsynth_lock = threading.Lock()


def _session_inputsynth() -> _Inputsynth:
    """The session's inputsynth, started on first use and again after it died
    (a crash, or a chain that hit its timeout)."""
    global _inputsynth
    with _inputsynth_lock:
        if _inputsynth is None or _inputsynth.proc.poll() is not None:
            if _inputsynth is not None:
                _inputsynth.close()
            _inputsynth = _Inputsynth()
        return _inputsynth


@atexit.register
def _close_inputsynth() -> None:
    global _inputsynth
    with _inputsynth_lock:
        if _inputsynth is not None:
            _inputsynth.close()
            _inputsynth = None


def run_actions(action_sets: Sequence[dict], timeout: float = 30.0) -> None:
    """Execute raw W3C action sets (``{"type": "pointer"|"key"|"wheel", ...}``)
    in order. Low level; prefer the helpers below."""
    synth = _session_inputsynth()
    synth.wait(synth.start(action_sets), timeout)


@contextmanager
def background_actions(action_sets: Sequence[dict], timeout: float = 30.0) -> Iterator[None]:
    """Run raw W3C action sets like :func:`run_actions`, in the background
    while the body runs. Leaving the body (also by an exception) ends the
    chain's pause marked ``"interruptible": True`` (the one in progress, or
    the next one if the chain has not reached it yet;
    tools/inputsynth-fixes.patch) and waits for the chain to finish. So a
    chain can hold a button in such a pause exactly as long as the body
    needs, with the pause's duration as the upper bound. Chains the body
    runs meanwhile run alongside it."""
    deadline = time.monotonic() + timeout
    synth = _session_inputsynth()
    chain = synth.start(action_sets)
    try:
        yield
    finally:
        synth.resume(chain)
        synth.wait(chain, deadline - time.monotonic())


def _pointer_set(actions: list[dict]) -> dict:
    return {"type": "pointer", "id": "mouse", "parameters": {"pointerType": "mouse"}, "actions": actions}


def _move(x: int, y: int, duration: int = 0) -> dict:
    return {"type": "pointerMove", "x": int(x), "y": int(y), "duration": int(duration), "origin": "viewport"}


def _start() -> list[dict]:
    return [_move(*_pointer)] if _pointer is not None else []


def pointer_position() -> tuple[int, int] | None:
    """Last position this module moved the pointer to (None before the first move)."""
    return _pointer


def hold_pointer_capability(park: tuple[int, int]) -> subprocess.Popen:
    """Start a long-lived inputsynth that keeps one fake-input device
    registered with KWin until the returned process is terminated.

    Without it the seat has a pointer only while some inputsynth runs: when a
    call's process exits, KWin drops ``wl_seat`` pointer capability and every
    client (Qt) releases its ``wl_pointer`` *without* a leave, so a surface
    that had the pointer stays hovered. The next call brings the capability
    back and immediately moves the pointer; if the client re-binds
    ``wl_pointer`` only after KWin processed that motion, it never gets the
    enter/leave either (a click can arrive with no hover before it, a move
    away with no leave). A real mouse is never unplugged between moves.

    The process first moves the pointer to ``park`` and waits until KWin
    reports it there, so the device is registered when this returns.
    """
    global _pointer
    f = tempfile.NamedTemporaryFile("w", suffix=".json", prefix="inputsynth-hold-", delete=False)
    with f:
        # 24 h pause: the session ends (and terminates it) long before.
        json.dump({"actions": [_pointer_set([_move(*park), {"type": "pause", "duration": 86_400_000}])]}, f)
    proc = subprocess.Popen([INPUTSYNTH, f.name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_until(
            lambda: proc.poll() is None and kwin.cursor_pos() == tuple(park),
            timeout=10,
            message=lambda: f"keep-alive fake-input device to move the pointer to {park} (exit code {proc.poll()})",
        )
    except BaseException:
        proc.kill()
        raise
    _pointer = (int(park[0]), int(park[1]))
    return proc


def move(x: int, y: int, duration_ms: int = 0) -> None:
    """Move the pointer to (x, y). With ``duration_ms`` > 50 the motion is
    interpolated from the current position in 50 ms steps."""
    global _pointer
    actions = _start() + [_move(x, y, duration_ms)] if duration_ms > 50 else [_move(x, y)]
    run_actions([_pointer_set(actions)])
    _pointer = (int(x), int(y))


def move_path(points: Iterable[tuple[int, int]], step_ms: int = 50) -> None:
    """Visit every point in order, ``step_ms`` apart (one motion event per
    point; use for gradual dock -> preview transitions)."""
    global _pointer
    pts = [(int(x), int(y)) for x, y in points]
    if not pts:
        return
    actions: list[dict] = []
    for p in pts:
        actions.append(_move(*p))
        actions.append({"type": "pause", "duration": int(step_ms)})
    run_actions([_pointer_set(actions)])
    _pointer = pts[-1]


def line(start: tuple[int, int], end: tuple[int, int], steps: int) -> list[tuple[int, int]]:
    """``steps`` evenly spaced points from start (exclusive) to end (inclusive)."""
    (x0, y0), (x1, y1) = start, end
    return [(round(x0 + (x1 - x0) * i / steps), round(y0 + (y1 - y0) * i / steps)) for i in range(1, steps + 1)]


def click(x: int | None = None, y: int | None = None, button: str = "left", count: int = 1, hold_ms: int = 0) -> None:
    """Click ``button`` (left/middle/right/back/forward) ``count`` times at
    (x, y), or at the current position when x/y are None."""
    global _pointer
    actions: list[dict] = []
    if x is not None and y is not None:
        actions.append(_move(x, y))
        _pointer = (int(x), int(y))
    b = _BUTTONS[button]
    for _ in range(count):
        actions.append({"type": "pointerDown", "button": b})
        if hold_ms:
            actions.append({"type": "pause", "duration": int(hold_ms)})
        actions.append({"type": "pointerUp", "button": b})
    run_actions([_pointer_set(actions)])


def press(button: str = "left", x: int | None = None, y: int | None = None) -> None:
    """Press and keep holding ``button`` (release with :func:`release`).

    Note: press and release reach KWin from the session's one fake-input
    device, which lives as long as its inputsynth process (a crash or a
    timeout restarts it and drops the held button); prefer :func:`drag` for
    press-move-release sequences."""
    global _pointer
    actions: list[dict] = []
    if x is not None and y is not None:
        actions.append(_move(x, y))
        _pointer = (int(x), int(y))
    actions.append({"type": "pointerDown", "button": _BUTTONS[button]})
    run_actions([_pointer_set(actions)])


def release(button: str = "left") -> None:
    """Release ``button`` at the current position."""
    run_actions([_pointer_set(_start() + [{"type": "pointerUp", "button": _BUTTONS[button]}])])


def drag(
    start: tuple[int, int],
    end: tuple[int, int],
    steps: int = 10,
    step_ms: int = 50,
    hold_ms: int = 300,
    button: str = "left",
) -> None:
    """Press at ``start``, hold ``hold_ms``, move to ``end`` in ``steps``
    motion events ``step_ms`` apart, release. One chain, so the button is
    held throughout."""
    global _pointer
    b = _BUTTONS[button]
    actions = [_move(*start), {"type": "pointerDown", "button": b}, {"type": "pause", "duration": int(hold_ms)}]
    for p in line(start, end, steps):
        actions.append(_move(*p))
        actions.append({"type": "pause", "duration": int(step_ms)})
    actions.append({"type": "pointerUp", "button": b})
    run_actions([_pointer_set(actions)])
    _pointer = (int(end[0]), int(end[1]))


def scroll(x: int, y: int, dy: int = 0, dx: int = 0) -> None:
    """Wheel event at (x, y). Positive ``dy`` scrolls down; 15 = one notch."""
    global _pointer
    run_actions(
        [
            {
                "type": "wheel",
                "id": "wheel",
                "actions": [{"type": "scroll", "x": int(x), "y": int(y), "deltaX": int(dx), "deltaY": int(dy), "duration": 0}],
            }
        ]
    )
    _pointer = (int(x), int(y))


def key(*names: str, hold_ms: int = 0) -> None:
    """Press the keys in order and release them in reverse: ``key("Meta",
    "F5")``, ``key("Escape")``, ``key("ctrl", "a")``. Goes to the surface with
    keyboard focus (or to KWin's shortcut handling)."""
    values = [key_value(n) for n in names]
    actions = [{"type": "keyDown", "value": v} for v in values]
    if hold_ms:
        actions.append({"type": "pause", "duration": int(hold_ms)})
    actions += [{"type": "keyUp", "value": v} for v in reversed(values)]
    run_actions([{"type": "key", "id": "keyboard", "actions": actions}])


def type_text(text: str) -> None:
    """Type ``text`` character by character."""
    actions: list[dict] = []
    for ch in text:
        actions += [{"type": "keyDown", "value": ch}, {"type": "keyUp", "value": ch}]
    run_actions([{"type": "key", "id": "keyboard", "actions": actions}])
