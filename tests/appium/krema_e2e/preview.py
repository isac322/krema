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
import time
from pathlib import Path
from typing import Any, Callable

from PIL import Image
from selenium.webdriver.remote.webelement import WebElement

from . import env, kwin
from . import input as inp
from .krema import PREVIEW_XPATH, Krema, Rect, _xpath_str
from .waits import wait_stable, wait_until

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


def fast_pointer_entry(krema: Krema, item: str, timeout: float = 10.0) -> tuple[Rect, float]:
    """Enter the current popup as soon as AT-SPI reports it visible.

    Cache the in-process Accessible and the already mapped preview surface
    before hovering. Polling the Accessible avoids serializing the whole
    webdriver tree during the short visibility-to-input window. Unlike
    :func:`wait_on_screen`, this waits for neither pixels nor settled layout.
    Return the popup rect used for entry and the elapsed time since visibility.
    """
    import pyatspi  # noqa: PLC0415 - only this timing-sensitive helper needs it

    def accessible() -> Any | None:
        app = next((a for a in pyatspi.Registry.getDesktop(0) if a is not None and a.get_process_id() == krema.pid), None)
        if app is None:
            return None
        nodes = pyatspi.findAllDescendants(app, lambda n: n.getRoleName() == "popup menu")
        return nodes[0] if len(nodes) == 1 else None

    popup = wait_until(accessible, timeout=timeout, message="preview popup over AT-SPI")
    popup_element = wait_until(krema.preview_popup, timeout=timeout, message="preview popup webdriver element")
    if popup.get_accessible_id() != popup_element.get_attribute("accessibility-id"):
        raise AssertionError("AT-SPI and WebDriver resolved different preview popups")
    surface = wait_until(
        lambda: krema.surface_rect("preview"),
        timeout=timeout,
        message="pre-mapped KWin preview surface",
    )

    first_visible: float | None = None
    component: Any | None = None
    geometry: Rect | None = None

    def current() -> Rect | None:
        nonlocal component, geometry, first_visible
        popup.clear_cache()
        states = popup.getState()
        if states.contains(pyatspi.STATE_VISIBLE) and first_visible is None:
            first_visible = time.monotonic()
        if states.contains(pyatspi.STATE_SHOWING):
            if component is None:
                component = popup.get_component_iface()
            if component is not None and (geometry is None or states.contains(pyatspi.STATE_VISIBLE)):
                candidate = component.get_extents(pyatspi.XY_SCREEN)
                local = Rect(candidate.x, candidate.y, candidate.width, candidate.height)
                if (
                    local.width > 8
                    and local.height > 0
                    and local.x >= 0
                    and local.y >= 0
                    and local.x + local.width <= surface.width
                    and local.y + local.height <= surface.height
                ):
                    geometry = local
        if not states.contains(pyatspi.STATE_SHOWING) or not states.contains(pyatspi.STATE_VISIBLE):
            return None
        if geometry is None:
            return None
        return Rect(surface.x + geometry.x, surface.y + geometry.y, geometry.width, geometry.height)

    krema.hover_item(item)
    rect = wait_until(
        current,
        timeout=timeout,
        interval=0.005,
        message="visible AT-SPI popup geometry inside its KWin surface",
    )
    x, y = rect.center
    # The second event is motion inside the popup, which its HoverHandler
    # needs after wl_pointer.enter. Both points lie inside this current rect.
    inp.move_path([(x, y), (x + 4, y)], step_ms=5)
    assert first_visible is not None
    return rect, time.monotonic() - first_visible


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

#: How long :func:`wait_on_screen` requires the popup's geometry unchanged.
#: Its size and position follow the thumbnail rows through Qt layout and
#: PreviewController::setContentSize/recalcContentPosition: no timer and no
#: Behavior animation, so while the popup is still being laid out, successive
#: changes are one or a few frames apart. That is the "layout animation that
#: starts immediately" case in which tests/appium/README.md allows 0.3 s.
GEOMETRY_SETTLE = 0.3


def _popup_geometry(krema: Krema, popup: WebElement) -> tuple[Rect, Rect] | None:
    """(preview surface, popup) in screen coordinates, or None unless the
    popup's AT-SPI rect is non-empty and lies inside the KWin window of the
    preview surface. ``krema.surface_rect`` already matches that window by
    the AT-SPI frame's size. A popup being laid out can report a new size
    at its old position (648x211 at the 17x38 popup's origin, reaching past
    the surface), which fails this check."""
    surface = krema.surface_rect("preview")
    if surface is None:
        return None
    local = Rect.of(popup)
    inside = (
        local.width > 0
        and local.height > 0
        and local.x >= 0
        and local.y >= 0
        and local.x + local.width <= surface.width
        and local.y + local.height <= surface.height
    )
    if not inside:
        return None
    return surface, Rect(surface.x + local.x, surface.y + local.y, local.width, local.height)


def wait_on_screen(krema: Krema, popup: WebElement, timeout: float = 5.0) -> Rect:
    """Wait for settled popup layout and painted pixels; return its screen rect.

    Read thumbnail and close-button glide targets only after this returns.
    AT-SPI reports the popup shown before its rows finish layout (it starts
    at 17x38). This wait also guarded against the delayed input-region commit
    found in issue #55. Keep it for layout and thumbnail-paint stability;
    :func:`fast_pointer_entry` exercises the earlier AT-SPI-visible path.

    Readiness, in order:

    1. Final geometry: the popup's AT-SPI rect lies inside the preview
       surface's KWin window, and both are unchanged for
       :data:`GEOMETRY_SETTLE`.
    2. Painted there: the screenshot pixels inside that rect (clipped to the
       screen) that no other window covers were the empty (black) desktop
       until the popup painted its opaque Kirigami background over them.
       setMask runs in the same call as the size change, before the frame
       that paints the new size, so that frame's commit carries the input
       region too.
    3. Still there: the geometry has not changed while waiting for the
       paint.

    Needs screenshots (OpenGL KWin)."""
    assert kwin.can_capture(), f"KWin compositing is {kwin.compositing_type()}: waiting for the popup on screen needs screenshots"
    deadline = time.monotonic() + timeout

    def settled() -> tuple[Rect, Rect] | None:
        # None while the popup is outside its surface: wait_until keeps
        # polling. A geometry that does not settle in time raises WaitTimeout,
        # which wait_until chains into its own.
        return wait_stable(
            lambda: _popup_geometry(krema, popup),
            duration=GEOMETRY_SETTLE,
            timeout=max(GEOMETRY_SETTLE, deadline - time.monotonic()),
        )

    surface, rect = wait_until(
        settled,
        timeout=timeout,
        message=lambda: f"preview popup geometry to settle inside the preview surface (now: {_popup_geometry(krema, popup)})",
    )
    x0, y0 = max(rect.x, 0), max(rect.y, 0)
    x1, y1 = min(rect.x + rect.width, env.SCREEN_WIDTH), min(rect.y + rect.height, env.SCREEN_HEIGHT)
    visible = Rect(x0, y0, max(0, x1 - x0), max(0, y1 - y0))
    covers = [
        Rect(w.x, w.y, w.width, w.height)
        for w in kwin.windows()
        if (w.client_x, w.client_y, w.client_width, w.client_height) != tuple(surface)
    ]
    # Inside the rounded corners and the 1 px border.
    inset = 8
    points = [
        (x, y)
        for x in range(visible.x + inset, visible.x + visible.width - inset, 2)
        for y in range(visible.y + inset, visible.y + visible.height - inset, 2)
        if not any(c.contains(x, y) for c in covers)
    ]
    assert len(points) >= 100, f"popup {rect} is covered by other windows {covers}: no desktop pixels left to check"
    lit: list[float] = []

    def shown() -> bool:
        image = krema.screenshot("preview-on-screen", visible)
        lit[:] = [sum(1 for p in points if max(image.getpixel(p)) > DESKTOP_MAX_CHANNEL) / len(points)]
        return lit[0] > 0.5

    wait_until(
        shown,
        timeout=max(1.0, deadline - time.monotonic()),
        message=lambda: f"preview popup {rect} on screen (painted fraction {lit})",
    )
    now = _popup_geometry(krema, popup)
    assert now == (surface, rect), f"preview popup moved while it was painted: {(surface, rect)} -> {now}"
    return rect


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
