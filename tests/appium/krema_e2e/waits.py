# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Condition-based waiting. Never assert on a fixed sleep."""

from __future__ import annotations

import time
from typing import Callable, TypeVar

T = TypeVar("T")

#: Poll interval for predicates that only ask KWin (one scripting round trip,
#: a few ms): window mapped/unmapped. AT-SPI predicates cost tens of ms per
#: poll and keep the default.
KWIN_POLL_INTERVAL = 0.02


class WaitTimeout(AssertionError):
    """A wait_until() condition did not become truthy in time.

    Subclasses AssertionError so pytest reports it as a test failure, not an
    error, and shows the last observed value.
    """


def wait_until(
    predicate: Callable[[], T],
    timeout: float = 10.0,
    interval: float = 0.1,
    message: str | Callable[[], str] | None = None,
) -> T:
    """Poll ``predicate`` until it returns a truthy value and return that value.

    Exceptions raised by the predicate count as "not yet" (the element may not
    exist yet, D-Bus may be starting); the last one is chained into the
    WaitTimeout raised after ``timeout`` seconds.
    """
    deadline = time.monotonic() + timeout
    last_value: object = None
    last_error: BaseException | None = None
    while True:
        try:
            last_value = predicate()
            last_error = None
            if last_value:
                return last_value  # type: ignore[return-value]
        except Exception as e:  # noqa: BLE001 - "not yet" by contract
            last_error = e
        if time.monotonic() >= deadline:
            break
        time.sleep(interval)
    text = message() if callable(message) else message
    detail = f"last value: {last_value!r}"
    if last_error is not None:
        detail += f"; last error: {last_error!r}"
    raise WaitTimeout(f"{text or 'condition not met'} within {timeout}s ({detail})") from last_error


def wait_stable(
    getter: Callable[[], T],
    duration: float = 0.5,
    timeout: float = 10.0,
    interval: float = 0.05,
) -> T:
    """Wait until ``getter()`` returns the same value for ``duration`` seconds.

    For settling animations (zoom, show/hide) before measuring geometry.
    Samples are at most ``interval`` apart (plus the getter's own time); the
    last pause is shortened so the sample that completes ``duration`` is
    taken as soon as the window has elapsed.
    """
    deadline = time.monotonic() + timeout
    value = getter()
    since = time.monotonic()
    while time.monotonic() < deadline:
        time.sleep(min(interval, max(0.0, duration - (time.monotonic() - since))))
        current = getter()
        if current != value:
            value = current
            since = time.monotonic()
        elif time.monotonic() - since >= duration:
            return value
    raise WaitTimeout(f"value did not settle for {duration}s within {timeout}s (last: {value!r})")
