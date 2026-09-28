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

from typing import Callable

from PIL import Image
from selenium.webdriver.remote.webelement import WebElement

from . import input as inp
from .krema import PREVIEW_XPATH, Krema, Rect, _xpath_str
from .waits import wait_until

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
