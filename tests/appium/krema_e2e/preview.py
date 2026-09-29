# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Preview popup helpers (scenario 03): AT-SPI paths inside the popup, real
pointer paths from a dock item into the popup, thumbnail pixel oracle, and an
AT-SPI ``object:announcement`` listener for ``Accessible.announce``.

The preview is its own layer-shell surface (full width, 400 px deep, anchored
above the dock); ``krema.screen_rect(el, "preview")`` converts its elements to
screen coordinates. The webdriver does not support element-relative lookups,
so everything here is an absolute XPath.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Callable

from PIL import Image
from selenium.webdriver.remote.webelement import WebElement

from . import input as inp
from . import kwin
from .krema import PREVIEW_XPATH, Krema, Rect, _xpath_str
from .waits import wait_until

#: KPipeWire's warning for a received frame whose buffer carries no usable
#: data type (``spa_data.type == SPA_ID_INVALID``): the frame is dropped and
#: the thumbnail keeps its fallback icon. It happens when the PipeWire daemon
#: hands the consumer KWin's DMA-BUF buffer set before their type is resolved
#: (pipewire.log: ``do_port_use_buffers() invalid memory type 8``, 8 being the
#: allowed-types mask 1 << SPA_DATA_DmaBuf); every buffer of that set is
#: affected, so each later frame is dropped too until the stream renegotiates.
#: Seen under CPU contention (sharded runs) on Ubuntu 25.04 and 26.04.
UNTYPED_BUFFER_WARNING = "invalid buffer type"

#: Direct thumbnail buttons of the popup (their close buttons are nested
#: one level deeper, so ``//popup_menu/button`` does not match them).
THUMB_XPATH = PREVIEW_XPATH + "/button"
HEADER_XPATH = PREVIEW_XPATH + "/label"
SEPARATOR_XPATH = PREVIEW_XPATH + "/separator"


def thumb_xpath(title: str) -> str:
    """Thumbnail whose title label is ``title`` (the button name may carry
    ", Active"/", Minimized")."""
    return f"{THUMB_XPATH}[label[@name={_xpath_str(title)}]]"


def close_xpath(title: str) -> str:
    return f"{thumb_xpath(title)}/button[@name={_xpath_str('Close ' + title)}]"


def thumb_titles(krema: Krema) -> list[str]:
    """Titles of the thumbnails in visual (left-to-right) order."""
    labels = krema.find_all(THUMB_XPATH + "/label")
    return [el.get_attribute("name") for el in sorted(labels, key=lambda el: Rect.of(el).x)]


def screen_rect(krema: Krema, element: WebElement) -> Rect:
    return krema.screen_rect(element, "preview")


def open_by_hover(krema: Krema, item: str, timeout: float = 10.0) -> WebElement:
    """Real hover on dock item ``item`` until the preview popup shows."""
    krema.hover_item(item)
    wait_until(krema.preview_visible, timeout=timeout, message=f"preview of {item!r} to open on hover")
    return krema.preview_popup()


def glide_into(krema: Krema, target: tuple[int, int], steps: int = 6, step_ms: int = 20) -> None:
    """Move the pointer from where it is (on the dock item) to ``target`` in
    the popup quickly enough to cross the dock->preview gap within the
    preview hide delay (200 ms by default), like a user's flick."""
    start = inp.pointer_position()
    assert start is not None, "pointer position unknown: hover a dock item first"
    inp.move_path(inp.line(start, target, steps), step_ms=step_ms)


#: Brightest channel of a screenshot pixel that still counts as the empty
#: desktop: kwin.screenshot() flattens KWin's transparent desktop onto black.
DESKTOP_MAX_CHANNEL = 16


def wait_on_screen(krema: Krema, popup: WebElement, timeout: float = 5.0) -> None:
    """Wait until KWin shows the open preview ``popup`` on screen.

    Call it before moving the pointer onto a popup that has just opened.
    ``krema.preview_visible()`` reads the AT-SPI tree, which reports the popup
    as shown once krema's QML shows it. The popup's input region
    (PreviewController::updateInputRegion, QWindow::setMask) is double-buffered
    Wayland state: it applies with the preview surface's next commit, the one
    that first puts the popup on screen. On the first open after a krema start,
    that commit lands 100-190 ms after the AT-SPI change. A pointer that
    arrives earlier gets no wl_pointer.enter on the preview, and KWin sends
    none once the pointer is resting, so the preview never sees the hover and
    closes after its hide delay. A user cannot aim at a popup before it is
    drawn.

    Oracle: the screenshot pixels inside the popup's rect that no other window
    covers were the empty (black) desktop until the popup painted its opaque
    Kirigami background over them. Needs screenshots (OpenGL KWin)."""
    assert kwin.can_capture(), f"KWin compositing is {kwin.compositing_type()}: waiting for the popup on screen needs screenshots"
    rect = screen_rect(krema, popup)
    preview = krema.surface_rect("preview")
    covers = [
        Rect(w.x, w.y, w.width, w.height)
        for w in kwin.windows()
        if (w.client_x, w.client_y, w.client_width, w.client_height) != tuple(preview or ())
    ]
    # Inside the rounded corners and the 1 px border.
    inset = 8
    points = [
        (x, y)
        for x in range(rect.x + inset, rect.x + rect.width - inset, 2)
        for y in range(rect.y + inset, rect.y + rect.height - inset, 2)
        if not any(c.contains(x, y) for c in covers)
    ]
    assert len(points) >= 100, f"popup {rect} is covered by other windows {covers}: no desktop pixels left to check"
    lit: list[float] = []

    def shown() -> bool:
        image = Image.open(krema.screenshot("preview-on-screen"))
        lit[:] = [sum(1 for p in points if max(image.getpixel(p)) > DESKTOP_MAX_CHANNEL) / len(points)]
        return lit[0] > 0.5

    wait_until(shown, timeout=timeout, message=lambda: f"preview popup {rect} on screen (painted fraction {lit})")


def dominant_fraction(image: Image.Image, rect: Rect, matches: Callable[[tuple[int, int, int]], bool]) -> float:
    """Fraction of pixels in ``rect`` for which ``matches(rgb)`` holds."""
    crop = image.convert("RGB").crop((rect.x, rect.y, rect.x + rect.width, rect.y + rect.height))
    pixels = list(crop.getdata())
    return sum(1 for p in pixels if matches(p)) / max(1, len(pixels))


def thumbnail_image_rect(thumb: Rect) -> Rect:
    """Central part of a thumbnail's window image (screen coordinates): away
    from the scaled title bar, the close button, the fixture's label/Quit
    button and the title label below the image (the image is the top
    ``0.7 * width`` of the button)."""
    image_h = int(thumb.width * 0.7)
    return Rect(thumb.x + thumb.width // 4, thumb.y + image_h * 3 // 10, thumb.width // 2, image_h * 4 // 10)


def is_red(p: tuple[int, int, int]) -> bool:
    return p[0] > 180 and p[1] < 80 and p[2] < 80


def is_blue(p: tuple[int, int, int]) -> bool:
    return p[2] > 180 and p[0] < 80 and p[1] < 80


def is_green(p: tuple[int, int, int]) -> bool:
    return p[1] > 100 and p[0] < 80 and p[2] < 80


def dropped_untyped_buffers(krema: Krema) -> bool:
    """Whether KPipeWire in this krema dropped a frame for an untyped buffer
    (:data:`UNTYPED_BUFFER_WARNING`)."""
    return UNTYPED_BUFFER_WARNING in Path(krema.log_path).read_text(errors="replace")


def renegotiate_screencast(title: str) -> None:
    """Widen window ``title`` by 2 px through KWin. Its screencast's buffer
    size follows the window, so the stream renegotiates and KWin allocates a
    fresh buffer set, which recovers a stream stuck on untyped buffers."""
    kwin.evaluate(
        f"const w = workspace.windowList().find(w => w.caption === {json.dumps(title)});"
        "const g = w.frameGeometry;"
        "w.frameGeometry = {x: g.x, y: g.y, width: g.width + 2, height: g.height};"
        "report(true);"
    )



class Announcements:
    """Collects AT-SPI ``object:announcement`` events (what Qt emits for QML
    ``Accessible.announce``) on the session's a11y bus.

    libatspi dispatches on the default GLib main context, so no second main
    loop runs: :meth:`messages` pumps pending events (kwin.evaluate pumps the
    same context, which only delivers them earlier)."""

    def __init__(self) -> None:
        import pyatspi  # noqa: PLC0415 - only this helper needs it

        self._registry = pyatspi.Registry
        self._events: list[tuple[str, str]] = []
        self._registry.registerEventListener(self._on_event, "object:announcement")

    def _on_event(self, event) -> None:  # noqa: ANN001 - pyatspi event
        app = event.host_application.name if event.host_application is not None else ""
        self._events.append((app, str(event.any_data)))

    def _pump(self) -> None:
        from gi.repository import GLib  # noqa: PLC0415

        ctx = GLib.MainContext.default()
        while ctx.iteration(False):
            pass

    def messages(self, app: str | None = None) -> list[str]:
        """Announcement texts received so far (from application ``app``)."""
        self._pump()
        return [text for a, text in self._events if app is None or a == app]

    def clear(self) -> None:
        self._pump()
        self._events.clear()

    def close(self) -> None:
        self._registry.deregisterEventListener(self._on_event, "object:announcement")
