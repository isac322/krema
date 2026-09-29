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

Each inputsynth process registers its own fake-input device with KWin for its
lifetime. The session must hold one more device open throughout (see
:func:`hold_pointer_capability`), otherwise the seat loses its pointer
capability between calls and clients drop their ``wl_pointer``.
"""

from __future__ import annotations

import json
import signal
import subprocess
import tempfile
import time
from contextlib import contextmanager
from typing import Iterable, Iterator, Sequence

from selenium.webdriver.common.keys import Keys

from . import kwin
from .waits import wait_until

INPUTSYNTH = "selenium-webdriver-at-spi-inputsynth"

_BUTTONS = {"left": 0, "middle": 1, "right": 2, "back": 3, "forward": 4}

#: Last pointer position we moved to. inputsynth is a fresh process per call
#: and interpolates from (0, 0), so every chain first teleports here.
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


def run_actions(action_sets: Sequence[dict], timeout: float = 30.0) -> None:
    """Execute raw W3C action sets (``{"type": "pointer"|"key"|"wheel", ...}``)
    in order. Low level; prefer the helpers below."""
    with tempfile.NamedTemporaryFile("w", suffix=".json") as f:
        json.dump({"actions": list(action_sets)}, f)
        f.flush()
        proc = subprocess.run([INPUTSYNTH, f.name], capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"inputsynth failed ({proc.returncode}): {proc.stderr[-2000:]}")


@contextmanager
def background_actions(action_sets: Sequence[dict], timeout: float = 30.0) -> Iterator[None]:
    """Run raw W3C action sets like :func:`run_actions`, in the background
    while the body runs. Leaving the body (also by an exception) sends the
    chain SIGUSR1 and waits for it to finish: the signal ends the chain's
    pause marked ``"interruptible": True`` (the one in progress, or the next
    one if the chain has not reached it yet; tools/inputsynth-fixes.patch).
    So a chain can hold a button in such a pause exactly as long as the body
    needs, with the pause's duration as the upper bound."""
    deadline = time.monotonic() + timeout
    with (
        tempfile.NamedTemporaryFile("w", suffix=".json") as f,
        tempfile.TemporaryFile("w+") as err,
    ):
        json.dump({"actions": list(action_sets)}, f)
        f.flush()
        proc = subprocess.Popen([INPUTSYNTH, f.name], stdout=subprocess.DEVNULL, stderr=err, text=True)
        try:
            yield
        finally:
            proc.send_signal(signal.SIGUSR1)  # no-op once it has exited
            try:
                proc.wait(timeout=max(0.0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                raise
            if proc.returncode != 0:
                err.seek(0)
                raise RuntimeError(f"inputsynth failed ({proc.returncode}): {err.read()[-2000:]}")


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

    Note: the fake-input device lives as long as one inputsynth process, so a
    held button spans calls only as far as KWin keeps it pressed; prefer
    :func:`drag` for press-move-release sequences."""
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
