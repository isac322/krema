# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""kremarc (KConfig INI) writer/reader.

Keys and groups come from src/config/krema.kcfg (group ``General``) and
src/config/screensettings.cpp (groups ``Screen-<output name>``).
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Mapping

#: Visibility modes (krema.kcfg VisibilityMode)
ALWAYS_VISIBLE, AUTO_HIDE, DODGE_WINDOWS = 0, 1, 2
#: Edges (krema.kcfg Edge)
EDGE_TOP, EDGE_BOTTOM, EDGE_LEFT, EDGE_RIGHT = 0, 1, 2, 3


def launcher(desktop_id: str) -> str:
    """PinnedLaunchers entry for a desktop file id (``org.kde.kwrite`` or
    ``org.kde.kwrite.desktop``)."""
    if not desktop_id.endswith(".desktop"):
        desktop_id += ".desktop"
    return f"applications:{desktop_id}"


def _escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")


def _format(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (list, tuple)):
        # KConfig list separator is ',', literal commas are escaped as '\,'.
        return ",".join(_escape(str(v)).replace(",", "\\,") for v in value)
    return _escape(str(value))


def normalize(settings: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    """Accept either ``{"IconSize": 64}`` (implies group General) or
    ``{"General": {...}, "Screen-Virtual-0": {...}}``."""
    if all(isinstance(v, Mapping) for v in settings.values()) and settings:
        return {g: dict(v) for g, v in settings.items()}
    return {"General": dict(settings)}


def write_kremarc(path: Path, settings: Mapping[str, Any]) -> None:
    """Write ``settings`` as a kremarc file at ``path`` (overwrites)."""
    groups = normalize(settings)
    lines: list[str] = []
    for group, entries in groups.items():
        lines.append(f"[{group}]")
        for key, value in entries.items():
            lines.append(f"{key}={_format(value)}")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def _split_list(raw: str) -> list[str]:
    out, cur, i = [], "", 0
    while i < len(raw):
        c = raw[i]
        if c == "\\" and i + 1 < len(raw):
            cur += raw[i + 1]
            i += 2
            continue
        if c == ",":
            out.append(cur)
            cur = ""
        else:
            cur += c
        i += 1
    out.append(cur)
    return out


def read_kremarc(path: Path) -> dict[str, dict[str, str]]:
    """Parse a kremarc into ``{group: {key: raw string}}``.

    Values stay strings (KConfig is untyped on disk); use :func:`as_bool`,
    :func:`as_list`, ``int()``/``float()`` to interpret. Returns ``{}`` when
    the file does not exist.
    """
    if not path.exists():
        return {}
    groups: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = groups.setdefault(line[1:-1], {})
            continue
        if "=" in line and current is not None:
            key, value = line.split("=", 1)
            # KConfig may write immutable/locale suffixes: Key[$e]=...
            key = key.split("[", 1)[0].strip()
            current[key] = value
    return groups


def as_bool(value: str) -> bool:
    return value.strip().lower() in ("true", "1", "yes", "on")


def as_list(value: str) -> list[str]:
    return [] if value == "" else _split_list(value)
