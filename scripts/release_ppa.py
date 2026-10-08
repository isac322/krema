#!/usr/bin/env python3
"""Launchpad PPA channel adapter for the Krema release tooling.

Target series are discovered dynamically: every Ubuntu series whose primary
archive ships qt6-base at or above the Qt baseline in the tagged
packaging/obs/debian.control. Series that Launchpad still accepts uploads for
are publication targets; Qt-eligible series that Launchpad no longer accepts
(Obsolete) are reported as platform_blocked and never affect other channels
(AGENTS.md, Distribution Support Policy).

Per series the exact version `<version>-<debrev>~ppa1~<series>1` is queried
with getPublishedSources (all statuses, Pending included). Existing uploads
are never repeated and revisions are never incremented automatically.

publish(execute=True) builds the Debian source package from the tagged
source in scratch space under the context work directory (natively with
dpkg-buildpackage, or in the digest-pinned Ubuntu image from the tagged
tests/distro/targets.tsv via Docker), signs .dsc, .buildinfo and .changes with
the local GPG key registered on Launchpad (non-interactive, no key export),
re-verifies every checksum and signature, and uploads with dput or the
standard anonymous FTP upload to ppa.launchpad.net.
"""

from __future__ import annotations

import argparse
import ftplib
import hashlib
import json
import os
import re
import shutil
import sys
import tarfile
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping, Sequence

from release_common import (
    ReleaseContext,
    ReleaseError,
    command,
    http_json,
    load_context,
    result,
)

CHANNEL = "ppa"
SOURCE_NAME = "krema"
PPA_OWNER = "isac322"
PPA_NAME = "krema"
DISTRIBUTION = "ubuntu"
PPA_REVISION = "ppa1"

LP_API = "https://api.launchpad.net/devel"
SERIES_API = f"{LP_API}/{DISTRIBUTION}/series"
PRIMARY_ARCHIVE_API = f"{LP_API}/{DISTRIBUTION}/+archive/primary"
PPA_ARCHIVE_API = f"{LP_API}/~{PPA_OWNER}/+archive/{DISTRIBUTION}/{PPA_NAME}"
OWNER_API = f"{LP_API}/~{PPA_OWNER}"
PPA_WEB = f"https://launchpad.net/~{PPA_OWNER}/+archive/{DISTRIBUTION}/{PPA_NAME}"
DPUT_TARGET = f"ppa:{PPA_OWNER}/{PPA_NAME}"
UPLOAD_HOST = "ppa.launchpad.net"
UPLOAD_DIR = f"~{PPA_OWNER}/{PPA_NAME}/{DISTRIBUTION}"

# Launchpad accepts source uploads only to series in these states.
UPLOADABLE_STATUSES = frozenset(
    {"Active Development", "Pre-release Freeze", "Current Stable Release", "Supported"}
)
QT_SOURCE = "qt6-base"

BUILD_TIMEOUT = 1800  # image pull + apt + dpkg-buildpackage -S
NATIVE_BUILD_TIMEOUT = 900
GPG_TIMEOUT = 120
UPLOAD_TIMEOUT = 900
DOWNLOAD_TIMEOUT = 300
MAX_ORIG_BYTES = 512 * 1024 * 1024
# Launchpad processes accepted uploads within minutes. An upload still
# invisible after this window was rejected (Launchpad mails the reason).
UPLOAD_ACCEPT_WINDOW_SECONDS = 3 * 3600

SERIES_RE = re.compile(r"^[a-z][a-z0-9]*$")
UPSTREAM_RE = re.compile(r"^[0-9][0-9A-Za-z.+~]*$")
DEBREV_RE = re.compile(r"^[0-9][0-9A-Za-z.+~]*$")
FPR_RE = re.compile(r"^[0-9A-F]{40}$")
CHANGELOG_HEADER_RE = re.compile(
    r"^(?P<source>[a-z0-9][a-z0-9+.-]+) \((?P<version>[^()\s]+)\) (?P<dists>[^;]+); (?P<options>.+)$"
)
CHANGELOG_TRAILER_RE = re.compile(r"^ -- (?P<maintainer>.+?)  (?P<date>.+)$")
QT_BASELINE_RE = re.compile(r"\bqt6-base-dev\s*\(\s*>=\s*([^)\s]+)\s*\)")

HASH_FIELDS = {
    "Files": "md5",
    "Checksums-Md5": "md5",
    "Checksums-Sha1": "sha1",
    "Checksums-Sha256": "sha256",
}

# Launchpad BuildSetStatus values from getBuildSummariesForSourceIds.
BUILD_SUMMARY_STATES = {
    "FULLYBUILT": ("published", "all architecture builds succeeded and are published"),
    "FULLYBUILT_PENDING": (
        "pending",
        "builds succeeded; binaries are awaiting publication",
    ),
    "NEEDSBUILD": ("pending", "builds are queued"),
    "BUILDING": ("pending", "builds are running"),
    "FAILEDTOBUILD": ("failed", "at least one architecture build failed"),
}


# ---------------------------------------------------------------------------
# Debian version comparison (dpkg semantics)
# ---------------------------------------------------------------------------


def _order(ch: str) -> int:
    if ch == "" or ch in "0123456789":
        return 0
    if ch == "~":
        return -1
    if ch.isascii() and ch.isalpha():
        return ord(ch)
    return ord(ch) + 256


def _verrevcmp(a: str, b: str) -> int:
    def at(text: str, index: int) -> str:
        return text[index] if index < len(text) else ""

    def digit(ch: str) -> bool:
        return ch != "" and ch in "0123456789"

    i = j = 0
    while i < len(a) or j < len(b):
        first_diff = 0
        while (at(a, i) and not digit(at(a, i))) or (at(b, j) and not digit(at(b, j))):
            ac, bc = _order(at(a, i)), _order(at(b, j))
            if ac != bc:
                return ac - bc
            i += 1
            j += 1
        while at(a, i) == "0":
            i += 1
        while at(b, j) == "0":
            j += 1
        while digit(at(a, i)) and digit(at(b, j)):
            if not first_diff:
                first_diff = ord(a[i]) - ord(b[j])
            i += 1
            j += 1
        if digit(at(a, i)):
            return 1
        if digit(at(b, j)):
            return -1
        if first_diff:
            return first_diff
    return 0


def _split_version(version: str) -> tuple[int, str, str]:
    if not version or any(ch.isspace() for ch in version):
        raise ReleaseError(f"invalid Debian version {version!r}")
    epoch = 0
    rest = version
    if ":" in version:
        epoch_text, rest = version.split(":", 1)
        if not epoch_text.isdigit():
            raise ReleaseError(f"invalid Debian version epoch in {version!r}")
        epoch = int(epoch_text)
    upstream, _, revision = rest.rpartition("-") if "-" in rest else (rest, "", "")
    if not upstream or not upstream[0].isdigit():
        raise ReleaseError(f"invalid Debian upstream version in {version!r}")
    return epoch, upstream, revision


def compare_versions(a: str, b: str) -> int:
    """Compare two Debian versions like dpkg --compare-versions; returns -1/0/1."""
    ea, ua, ra = _split_version(a)
    eb, ub, rb = _split_version(b)
    if ea != eb:
        return -1 if ea < eb else 1
    for left, right in ((ua, ub), (ra, rb)):
        diff = _verrevcmp(left, right)
        if diff:
            return -1 if diff < 0 else 1
    return 0


def upstream_version(version: str) -> str:
    return _split_version(version)[1]


def package_version(version: str, debian_revision: str, series: str) -> str:
    """Exact PPA version `<version>-<debrev>~ppa1~<series>1`."""
    if not UPSTREAM_RE.match(version) or "-" in version or ":" in version:
        raise ReleaseError(f"invalid upstream version {version!r}")
    if not DEBREV_RE.match(debian_revision):
        raise ReleaseError(f"invalid Debian revision {debian_revision!r}")
    if not SERIES_RE.match(series):
        raise ReleaseError(f"invalid Ubuntu series name {series!r}")
    return f"{version}-{debian_revision}~{PPA_REVISION}~{series}1"


# ---------------------------------------------------------------------------
# RFC 822 / deb822 control files and clearsigned documents
# ---------------------------------------------------------------------------


def strip_clearsign(text: str) -> str:
    """Return the signed body of an OpenPGP clearsigned document, or text unchanged."""
    lines = text.splitlines()
    if not lines or lines[0].strip() != "-----BEGIN PGP SIGNED MESSAGE-----":
        return text
    index = 1
    while index < len(lines) and lines[index].strip():
        index += 1
    body: list[str] = []
    for line in lines[index + 1 :]:
        if line.strip() == "-----BEGIN PGP SIGNATURE-----":
            return "\n".join(body) + "\n"
        body.append(line[2:] if line.startswith("- ") else line)
    raise ReleaseError("clearsigned document has no signature block")


def parse_control(text: str) -> dict[str, str]:
    """Parse the first deb822 paragraph; continuation lines join with newlines."""
    fields: dict[str, str] = {}
    current: str | None = None
    for line in strip_clearsign(text).splitlines():
        if not line.strip():
            if fields:
                break
            continue
        if line[0] in " \t":
            if current is None:
                raise ReleaseError("control continuation line without a field")
            fields[current] += "\n" + line.strip()
            continue
        name, sep, value = line.partition(":")
        if not sep or not name or " " in name:
            raise ReleaseError(f"malformed control line {line!r}")
        current = name
        if current in fields:
            raise ReleaseError(f"duplicate control field {current}")
        fields[current] = value.strip()
    return fields


def listed_files(text: str) -> dict[str, dict[str, tuple[str, int]]]:
    """Map file name -> {algorithm: (hex digest, size)} from every hash field."""
    out: dict[str, dict[str, tuple[str, int]]] = {}
    for field_name, algorithm in HASH_FIELDS.items():
        value = parse_control(text).get(field_name)
        if value is None:
            continue
        for line in value.splitlines():
            tokens = line.split()
            if not tokens:
                continue
            if len(tokens) < 3 or not tokens[1].isdigit():
                raise ReleaseError(f"malformed {field_name} entry {line!r}")
            name = tokens[-1]
            if "/" in name or name in {".", ".."}:
                raise ReleaseError(f"unsafe file name {name!r} in {field_name}")
            entry = out.setdefault(name, {})
            if algorithm in entry and entry[algorithm] != (
                tokens[0].lower(),
                int(tokens[1]),
            ):
                raise ReleaseError(f"conflicting {algorithm} entries for {name}")
            entry[algorithm] = (tokens[0].lower(), int(tokens[1]))
    return out


def file_digests(data: bytes) -> dict[str, str]:
    return {
        "md5": hashlib.md5(data).hexdigest(),
        "sha1": hashlib.sha1(data).hexdigest(),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def update_checksums(text: str, name: str, data: bytes) -> str:
    """Rewrite hash and size of `name` in every hash field of an unsigned control file."""
    if text.lstrip().startswith("-----BEGIN PGP"):
        raise ReleaseError("refusing to rewrite checksums inside a signed document")
    digests = file_digests(data)
    size = str(len(data))
    fields_with_name: set[str] = set()
    fields_present: set[str] = set()
    current: str | None = None
    out_lines: list[str] = []
    for line in text.splitlines():
        if line and line[0] not in " \t":
            current = line.partition(":")[0]
            if current in HASH_FIELDS:
                fields_present.add(current)
            out_lines.append(line)
            continue
        if current in HASH_FIELDS and line.strip():
            tokens = line.split()
            if tokens[-1] == name:
                tokens[0] = digests[HASH_FIELDS[current]]
                tokens[1] = size
                fields_with_name.add(current)
                out_lines.append(" " + " ".join(tokens))
                continue
        out_lines.append(line)
    if not fields_with_name:
        raise ReleaseError(f"{name} is not listed in any checksum field")
    missing = fields_present - fields_with_name
    if missing:
        raise ReleaseError(f"{name} is missing from {', '.join(sorted(missing))}")
    return "\n".join(out_lines) + "\n"


def verify_listed_files(text: str, directory: Path) -> list[str]:
    """Verify every listed file's size and hashes; return the verified names."""
    entries = listed_files(text)
    if not entries:
        raise ReleaseError("control file lists no files")
    for name, sums in entries.items():
        path = directory / name
        if not path.is_file():
            raise ReleaseError(f"listed file {name} is missing")
        data = path.read_bytes()
        actual = file_digests(data)
        for algorithm, (expected, size) in sums.items():
            if size != len(data):
                raise ReleaseError(f"{name}: size {len(data)} != listed {size}")
            if actual[algorithm] != expected:
                raise ReleaseError(f"{name}: {algorithm} mismatch")
    return sorted(entries)


# ---------------------------------------------------------------------------
# Tagged packaging inputs
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class ChangelogEntry:
    source: str
    version: str
    body: tuple[str, ...]
    maintainer: str
    date: str

    @property
    def timestamp(self) -> int:
        try:
            return int(parsedate_to_datetime(self.date).timestamp())
        except (AttributeError, TypeError, ValueError) as exc:
            raise ReleaseError(f"invalid changelog date {self.date!r}") from exc


def parse_top_changelog_entry(text: str) -> ChangelogEntry:
    lines = text.splitlines()
    while lines and not lines[0].strip():
        lines.pop(0)
    if not lines:
        raise ReleaseError("debian changelog is empty")
    header = CHANGELOG_HEADER_RE.match(lines[0])
    if not header:
        raise ReleaseError(f"malformed debian changelog header {lines[0]!r}")
    body: list[str] = []
    for line in lines[1:]:
        trailer = CHANGELOG_TRAILER_RE.match(line)
        if trailer:
            while body and not body[0].strip():
                body.pop(0)
            while body and not body[-1].strip():
                body.pop()
            if not body:
                raise ReleaseError("top debian changelog entry has no change lines")
            entry = ChangelogEntry(
                source=header["source"],
                version=header["version"],
                body=tuple(body),
                maintainer=trailer["maintainer"],
                date=trailer["date"],
            )
            entry.timestamp  # validate the date
            return entry
        if line and not line.startswith(" "):
            break
        body.append(line)
    raise ReleaseError("top debian changelog entry has no maintainer trailer")


def render_series_changelog(entry: ChangelogEntry, version: str, series: str) -> str:
    return (
        f"{entry.source} ({version}) {series}; urgency=medium\n\n"
        + "\n".join(entry.body)
        + f"\n\n -- {entry.maintainer}  {entry.date}\n"
    )


def parse_qt_baseline(control_text: str) -> str:
    match = QT_BASELINE_RE.search(control_text)
    if not match:
        raise ReleaseError(
            "debian.control has no versioned qt6-base-dev build dependency"
        )
    return match.group(1)


def _version_key(text: str) -> tuple[int, ...]:
    parts = []
    for part in text.split("."):
        if not part.isdigit():
            return ()
        parts.append(int(part))
    return tuple(parts)


def builder_image(targets_text: str) -> str:
    """Digest-pinned image of the newest Ubuntu distro E2E target."""
    best: tuple[tuple[int, ...], str] | None = None
    for line in targets_text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        columns = line.split("\t")
        if (
            len(columns) < 4
            or columns[1] != "debian"
            or not columns[2].startswith("ubuntu:")
        ):
            continue
        digest = columns[3]
        if not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
            continue
        key = _version_key(columns[2].split(":", 1)[1])
        if key and (best is None or key > best[0]):
            best = (key, f"{columns[2]}@{digest}")
    if best is None:
        raise ReleaseError(
            "tests/distro/targets.tsv has no digest-pinned Ubuntu target"
        )
    return best[1]


@dataclass(frozen=True)
class TaggedPackaging:
    entry: ChangelogEntry
    qt_baseline: str
    image: str
    debian_files: Mapping[str, Path]


def load_tagged_packaging(context: ReleaseContext) -> TaggedPackaging:
    obs = context.source_dir / "packaging" / "obs"
    files = {
        "control": obs / "debian.control",
        "rules": obs / "debian.rules",
        "copyright": obs / "debian.copyright",
        "changelog": obs / "debian.changelog",
    }
    for name, path in files.items():
        if not path.is_file() or path.is_symlink():
            raise ReleaseError(f"tagged source lacks packaging/obs/debian.{name}")
    entry = parse_top_changelog_entry(files["changelog"].read_text(encoding="utf-8"))
    expected = f"{context.version}-{context.debian_revision}"
    if entry.source != SOURCE_NAME or entry.version != expected:
        raise ReleaseError(
            f"tagged debian.changelog top entry is {entry.source} {entry.version}, expected {SOURCE_NAME} {expected}"
        )
    control = files["control"].read_text(encoding="utf-8")
    if parse_control(control).get("Source") != SOURCE_NAME:
        raise ReleaseError("tagged debian.control Source is not krema")
    targets = context.source_dir / "tests" / "distro" / "targets.tsv"
    if not targets.is_file():
        raise ReleaseError("tagged source lacks tests/distro/targets.tsv")
    return TaggedPackaging(
        entry=entry,
        qt_baseline=parse_qt_baseline(control),
        image=builder_image(targets.read_text(encoding="utf-8")),
        debian_files=files,
    )


# ---------------------------------------------------------------------------
# Series selection and per-series state (pure)
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class SeriesCandidate:
    name: str
    version: str
    lp_status: str
    uploadable: bool
    qt_version: str | None
    role: str  # target | platform_blocked | not_applicable

    @property
    def link(self) -> str:
        return f"{LP_API}/{DISTRIBUTION}/{self.name}"


def select_series(
    entries: Iterable[Mapping[str, Any]],
    qt_lookup: Callable[[str], str | None],
    qt_baseline: str,
) -> list[SeriesCandidate]:
    """Classify Ubuntu series against the Qt baseline and Launchpad upload policy.

    Every series Launchpad accepts uploads for is evaluated. Series it no longer
    accepts are walked newest first and reported while they still meet the Qt
    baseline; the walk stops at the first one that does not.
    """
    usable = []
    for entry in entries:
        name = str(entry.get("name", ""))
        version = str(entry.get("version", ""))
        status = str(entry.get("status", ""))
        if not SERIES_RE.match(name) or not _version_key(version) or status == "Future":
            continue
        usable.append(
            (
                name,
                version,
                status,
                bool(entry.get("active")) and status in UPLOADABLE_STATUSES,
            )
        )
    usable.sort(key=lambda item: _version_key(item[1]), reverse=True)
    out: list[SeriesCandidate] = []
    obsolete_walk_done = False
    for name, version, status, uploadable in usable:
        if not uploadable and obsolete_walk_done:
            continue
        qt = qt_lookup(name)
        eligible = qt is not None and compare_versions(qt, qt_baseline) >= 0
        if uploadable:
            role = "target" if eligible else "not_applicable"
        elif eligible:
            role = "platform_blocked"
        else:
            obsolete_walk_done = True
            continue
        out.append(SeriesCandidate(name, version, status, uploadable, qt, role))
    return out


def classify_target(
    target_version: str,
    exact: Sequence[Mapping[str, Any]],
    build_summaries: Mapping[str, Mapping[str, Any]],
    newest_in_series: str | None,
    upload_marker: Mapping[str, Any] | None,
    now: datetime,
) -> tuple[str, str]:
    """Return (state, detail) for one target series.

    States: published, pending, failed, blocked, missing.
    """
    for pub in exact:
        if pub.get("source_package_version") != target_version:
            raise ReleaseError(
                "Launchpad returned a publication for a different version"
            )
    statuses = {str(pub.get("status")) for pub in exact}
    if "Pending" in statuses:
        return "pending", "source accepted by Launchpad; publication is pending"
    live = [pub for pub in exact if pub.get("status") in ("Published", "Superseded")]
    if live:
        worst: tuple[str, str] | None = None
        rank = {"failed": 0, "pending": 1, "published": 2}
        for pub in live:
            source_id = str(pub.get("self_link", "")).rstrip("/").rsplit("/", 1)[-1]
            summary = build_summaries.get(source_id)
            status = str(summary.get("status")) if summary else "UNKNOWN"
            state = BUILD_SUMMARY_STATES.get(
                status, ("failed", f"unrecognised build summary {status}")
            )
            if worst is None or rank[state[0]] < rank[worst[0]]:
                worst = state
        assert worst is not None
        superseded = statuses == {"Superseded"}
        detail = worst[1] + (
            "; source was later superseded by a newer upload" if superseded else ""
        )
        return worst[0], detail
    if statuses:
        return (
            "blocked",
            f"exact version exists only as {'/'.join(sorted(statuses))}; Launchpad will not accept the same "
            "files again, so a new Debian revision in packaging/obs/debian.changelog is required",
        )
    if (
        newest_in_series is not None
        and compare_versions(newest_in_series, target_version) >= 0
    ):
        return (
            "blocked",
            f"series already carries {newest_in_series}, not older than {target_version}; Launchpad would reject the upload",
        )
    if upload_marker is not None:
        if upload_marker.get("version") != target_version:
            raise ReleaseError("upload record does not match the target version")
        uploaded = datetime.fromisoformat(str(upload_marker["uploaded_at"]))
        age = (now - uploaded).total_seconds()
        if age < UPLOAD_ACCEPT_WINDOW_SECONDS:
            return (
                "pending",
                f"uploaded at {uploaded.isoformat()}; awaiting Launchpad processing",
            )
        return (
            "failed",
            f"uploaded at {uploaded.isoformat()} but Launchpad shows no publication; the upload was rejected "
            "(see the Launchpad rejection mail)",
        )
    return "missing", "not uploaded"


def aggregate_state(states: Iterable[str]) -> str:
    """Channel status from target-series states; platform/not-applicable series are informational."""
    found = set(states)
    if not found:
        return "blocked"
    for state, channel_status in (
        ("failed", "failed"),
        ("blocked", "blocked"),
        ("missing", "planned"),
        ("submitted", "submitted"),
        ("pending", "pending"),
    ):
        if state in found:
            return channel_status
    if found == {"published"}:
        return "already_published"
    raise ReleaseError(f"unknown series states {sorted(found)}")


# ---------------------------------------------------------------------------
# Launchpad queries (read-only)
# ---------------------------------------------------------------------------


def _url(base: str, **params: str) -> str:
    return f"{base}?{urllib.parse.urlencode(params)}"


def _collection(url: str, *, max_pages: int = 20) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    next_url: str | None = url
    for _ in range(max_pages):
        if next_url is None:
            return entries
        data = http_json(next_url)
        if not isinstance(data, dict) or not isinstance(data.get("entries"), list):
            raise ReleaseError("unexpected Launchpad collection response")
        entries.extend(data["entries"])
        next_url = data.get("next_collection_link")
    if next_url is not None:
        raise ReleaseError("Launchpad collection exceeded the page limit")
    return entries


def _qt_lookup(series: str) -> str | None:
    pubs = _collection(
        _url(
            PRIMARY_ARCHIVE_API,
            **{
                "ws.op": "getPublishedSources",
                "source_name": QT_SOURCE,
                "exact_match": "true",
                "distro_series": f"{LP_API}/{DISTRIBUTION}/{series}",
                "status": "Published",
            },
        )
    )
    best: str | None = None
    for pub in pubs:
        version = str(pub.get("source_package_version", ""))
        if version and (best is None or compare_versions(version, best) > 0):
            best = version
    return best


def _ppa_sources(**params: str) -> list[dict[str, Any]]:
    return _collection(
        _url(
            PPA_ARCHIVE_API,
            **{
                "ws.op": "getPublishedSources",
                "source_name": SOURCE_NAME,
                "exact_match": "true",
                **params,
            },
        )
    )


def _series_publications(series: SeriesCandidate) -> list[dict[str, Any]]:
    pubs = _ppa_sources(distro_series=series.link, status="Published")
    pubs += _ppa_sources(distro_series=series.link, status="Pending")
    return pubs


def _newest(pubs: Iterable[Mapping[str, Any]]) -> str | None:
    best: str | None = None
    for pub in pubs:
        version = str(pub.get("source_package_version", ""))
        if version and (best is None or compare_versions(version, best) > 0):
            best = version
    return best


def _build_summaries(pubs: Iterable[Mapping[str, Any]]) -> dict[str, Any]:
    ids = sorted(
        {
            str(pub.get("self_link", "")).rstrip("/").rsplit("/", 1)[-1]
            for pub in pubs
            if pub.get("status") in ("Published", "Superseded")
        }
    )
    if not ids:
        return {}
    data = http_json(
        PPA_ARCHIVE_API
        + "?"
        + urllib.parse.urlencode(
            [("ws.op", "getBuildSummariesForSourceIds")]
            + [("source_ids", i) for i in ids]
        )
    )
    if not isinstance(data, dict):
        raise ReleaseError("unexpected Launchpad build summary response")
    return data


# ---------------------------------------------------------------------------
# Scratch layout and upload records
# ---------------------------------------------------------------------------


def _ppa_root(context: ReleaseContext) -> Path:
    return context.work_dir.resolve() / "ppa"


def _marker_path(context: ReleaseContext, version: str) -> Path:
    return _ppa_root(context) / "uploads" / f"{version}.json"


def _read_marker(context: ReleaseContext, version: str) -> dict[str, Any] | None:
    path = _marker_path(context, version)
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ReleaseError(f"corrupt PPA upload record {path}") from exc
    if not isinstance(data, dict):
        raise ReleaseError(f"corrupt PPA upload record {path}")
    return data


def _write_marker(context: ReleaseContext, record: Mapping[str, Any]) -> Path:
    path = _marker_path(context, str(record["version"]))
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    os.replace(tmp, path)
    return path


# ---------------------------------------------------------------------------
# Inspection
# ---------------------------------------------------------------------------


def _inspect(context: ReleaseContext, tagged: TaggedPackaging) -> list[dict[str, Any]]:
    now = datetime.now(timezone.utc)
    series = select_series(_collection(SERIES_API), _qt_lookup, tagged.qt_baseline)
    reports: list[dict[str, Any]] = []
    for cand in series:
        version = package_version(context.version, context.debian_revision, cand.name)
        report: dict[str, Any] = {
            "series": cand.name,
            "ubuntu_version": cand.version,
            "launchpad_status": cand.lp_status,
            "qt_version": cand.qt_version,
            "package_version": version,
        }
        if cand.role == "not_applicable":
            report["state"] = "not_applicable"
            report["detail"] = (
                f"{QT_SOURCE} {cand.qt_version or 'is not published'} is below the Qt baseline {tagged.qt_baseline}"
            )
            reports.append(report)
            continue
        active = _series_publications(cand)
        report["existing"] = [
            f"{pub.get('source_package_version')} ({pub.get('status')})"
            for pub in active
        ]
        if cand.role == "platform_blocked":
            report["state"] = "platform_blocked"
            report["detail"] = (
                f"Launchpad rejects uploads to {cand.lp_status} series; existing PPA publications stay as they are"
            )
            reports.append(report)
            continue
        exact = _ppa_sources(distro_series=cand.link, version=version)
        state, detail = classify_target(
            version,
            exact,
            _build_summaries(exact),
            _newest(
                pub for pub in active if pub.get("source_package_version") != version
            ),
            _read_marker(context, version),
            now,
        )
        report["state"] = state
        report["detail"] = detail
        reports.append(report)
    return reports


def _summary(
    context: ReleaseContext, reports: Sequence[Mapping[str, Any]], status: str
) -> dict:
    targets = [
        r for r in reports if r["state"] not in ("platform_blocked", "not_applicable")
    ]
    parts = [f"{r['series']}={r['state']}" for r in reports]
    if not targets:
        detail = "no Launchpad-accepted Ubuntu series meets the Qt baseline"
    else:
        detail = ", ".join(parts)
    return result(
        CHANNEL,
        status,
        detail,
        version=context.version,
        ppa=PPA_WEB,
        series=[dict(r) for r in reports],
    )


def status(context: ReleaseContext) -> dict:
    """Read-only per-series publication report."""
    try:
        tagged = load_tagged_packaging(context)
        reports = _inspect(context, tagged)
    except ReleaseError as exc:
        return result(CHANNEL, "failed", str(exc), version=context.version)
    targets = [
        r["state"]
        for r in reports
        if r["state"] not in ("platform_blocked", "not_applicable")
    ]
    return _summary(context, reports, aggregate_state(targets))


# ---------------------------------------------------------------------------
# Credentials and tools (local, non-interactive)
# ---------------------------------------------------------------------------


def _gpg_env() -> dict[str, str]:
    env = dict(os.environ)
    env.pop("GPG_TTY", None)
    return env


def _gpg(args: Sequence[str], *, timeout: int = GPG_TIMEOUT) -> str:
    return command(
        ["gpg", "--batch", "--no-tty", "--pinentry-mode", "error", *args],
        env=_gpg_env(),
        timeout=timeout,
    )


def launchpad_fingerprints() -> set[str]:
    owner = http_json(OWNER_API)
    link = owner.get("gpg_keys_collection_link") if isinstance(owner, dict) else None
    if not link:
        raise ReleaseError("Launchpad owner record has no GPG key collection")
    return {
        str(key.get("fingerprint", "")).upper()
        for key in _collection(link)
        if key.get("active", True)
        and FPR_RE.match(str(key.get("fingerprint", "")).upper())
    }


def local_secret_fingerprints() -> set[str]:
    out = _gpg(["--with-colons", "--list-secret-keys"], timeout=30)
    fingerprints: set[str] = set()
    expect_primary_fpr = False
    for line in out.splitlines():
        fields = line.split(":")
        if fields[0] == "sec":
            expect_primary_fpr = True
        elif fields[0] == "fpr" and expect_primary_fpr:
            fingerprints.add(fields[9].upper())
            expect_primary_fpr = False
        elif fields[0] in ("ssb", "uid"):
            expect_primary_fpr = False
    return fingerprints


def signing_fingerprint() -> str:
    matches = sorted(launchpad_fingerprints() & local_secret_fingerprints())
    if not matches:
        raise ReleaseError(
            "no local GPG secret key matches a key registered on Launchpad for ~"
            + PPA_OWNER
        )
    if len(matches) > 1:
        raise ReleaseError(
            "several local secret keys match Launchpad; refusing to choose one"
        )
    return matches[0]


def clearsign(path: Path, fingerprint: str) -> None:
    tmp = path.with_name(path.name + ".asc.tmp")
    if tmp.exists():
        tmp.unlink()
    _gpg(
        [
            "--local-user",
            fingerprint,
            "--armor",
            "--digest-algo",
            "SHA512",
            "--clearsign",
            "--output",
            str(tmp),
            str(path),
        ]
    )
    os.replace(tmp, path)


def verify_signature(path: Path, fingerprint: str) -> None:
    out = _gpg(["--status-fd", "1", "--verify", str(path)])
    for line in out.splitlines():
        parts = line.split()
        if (
            len(parts) >= 3
            and parts[:2] == ["[GNUPG:]", "VALIDSIG"]
            and parts[-1].upper() == fingerprint
        ):
            return
    raise ReleaseError(f"{path.name} is not validly signed by {fingerprint}")


def _test_signing(context: ReleaseContext, fingerprint: str) -> None:
    probe_dir = _ppa_root(context) / "signing-probe"
    probe_dir.mkdir(parents=True, exist_ok=True)
    probe = probe_dir / "probe.txt"
    probe.write_text(
        f"krema {context.tag} {context.commit} PPA signing probe\n", encoding="utf-8"
    )
    clearsign(probe, fingerprint)
    verify_signature(probe, fingerprint)


def _builder() -> str:
    if sys.platform.startswith("linux") and all(
        shutil.which(tool) for tool in ("dpkg-buildpackage", "dpkg-source", "dh")
    ):
        return "native"
    if shutil.which("docker"):
        command(["docker", "info", "--format", "{{.ServerVersion}}"], timeout=60)
        return "docker"
    raise ReleaseError(
        "neither native dpkg-buildpackage/debhelper nor a running Docker daemon is available"
    )


# ---------------------------------------------------------------------------
# Source package build, signing, upload
# ---------------------------------------------------------------------------

BUILD_SCRIPT = """\
set -euo pipefail
cd "$BUILD"
rm -rf "src"
mkdir "src"
tar -xzf "krema_${KREMA_VERSION}.orig.tar.gz" -C src --strip-components=1
cp -R debian src/debian
chmod 0755 src/debian/rules
cd src
dpkg-buildpackage -S -sa -us -uc -d
"""


def _single_top_dir(archive: Path) -> None:
    with tarfile.open(archive, "r:gz") as tar:
        tops = set()
        for member in tar.getmembers():
            name = member.name
            if name.startswith("/") or ".." in Path(name).parts:
                raise ReleaseError("unsafe path in source archive")
            while name.startswith("./"):
                name = name[2:]
            if not name or name == ".":
                continue
            tops.add(name.split("/", 1)[0])
    if len(tops) != 1:
        raise ReleaseError(
            "source archive must contain exactly one top-level directory"
        )


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _stage(
    context: ReleaseContext, tagged: TaggedPackaging, series: str, version: str
) -> Path:
    root = _ppa_root(context)
    build = root / "build" / series
    if build.exists():
        if build.resolve().parent != (root / "build").resolve():
            raise ReleaseError(
                "refusing to clean a build directory outside the context work directory"
            )
        shutil.rmtree(build)
    debian = build / "debian"
    (debian / "source").mkdir(parents=True)
    orig = build / f"{SOURCE_NAME}_{context.version}.orig.tar.gz"
    shutil.copyfile(context.archive, orig)
    if _sha256(orig) != context.sha256:
        raise ReleaseError("staged orig tarball does not match the context source hash")
    _single_top_dir(orig)
    shutil.copyfile(tagged.debian_files["control"], debian / "control")
    shutil.copyfile(tagged.debian_files["rules"], debian / "rules")
    shutil.copyfile(tagged.debian_files["copyright"], debian / "copyright")
    (debian / "rules").chmod(0o755)
    (debian / "source" / "format").write_text("3.0 (quilt)\n", encoding="utf-8")
    (debian / "changelog").write_text(
        render_series_changelog(tagged.entry, version, series), encoding="utf-8"
    )
    (build / "build-source.sh").write_text(BUILD_SCRIPT, encoding="utf-8")
    return build


def _build(
    context: ReleaseContext, tagged: TaggedPackaging, build: Path, builder: str
) -> None:
    epoch = str(tagged.entry.timestamp)
    if builder == "native":
        env = dict(
            os.environ,
            BUILD=str(build),
            KREMA_VERSION=context.version,
            SOURCE_DATE_EPOCH=epoch,
        )
        command(
            ["bash", str(build / "build-source.sh")],
            env=env,
            timeout=NATIVE_BUILD_TIMEOUT,
        )
        return
    inner = (
        "set -euo pipefail\n"
        "apt-get update -qq\n"
        "apt-get install -y -qq --no-install-recommends dpkg-dev debhelper >/dev/null\n"
        "status=0\n"
        "bash /build/build-source.sh || status=$?\n"
        'chown -R "$HOST_UID:$HOST_GID" /build\n'
        'exit "$status"\n'
    )
    command(
        [
            "docker",
            "run",
            "--rm",
            "-v",
            f"{build}:/build",
            "-e",
            "DEBIAN_FRONTEND=noninteractive",
            "-e",
            "BUILD=/build",
            "-e",
            f"KREMA_VERSION={context.version}",
            "-e",
            f"SOURCE_DATE_EPOCH={epoch}",
            "-e",
            f"HOST_UID={os.getuid()}",
            "-e",
            f"HOST_GID={os.getgid()}",
            tagged.image,
            "bash",
            "-c",
            inner,
        ],
        timeout=BUILD_TIMEOUT,
    )


@dataclass(frozen=True)
class SourceUpload:
    changes: Path
    files: tuple[Path, ...]  # .changes last


def _check_built(
    context: ReleaseContext, build: Path, series: str, version: str
) -> SourceUpload:
    names = {
        "dsc": build / f"{SOURCE_NAME}_{version}.dsc",
        "buildinfo": build / f"{SOURCE_NAME}_{version}_source.buildinfo",
        "changes": build / f"{SOURCE_NAME}_{version}_source.changes",
    }
    for kind, path in names.items():
        if not path.is_file():
            raise ReleaseError(f"source build produced no {kind} file")
    changes_text = names["changes"].read_text(encoding="utf-8")
    fields = parse_control(changes_text)
    expected = {
        "Source": SOURCE_NAME,
        "Version": version,
        "Distribution": series,
        "Architecture": "source",
    }
    for key, value in expected.items():
        if fields.get(key) != value:
            raise ReleaseError(
                f".changes {key} is {fields.get(key)!r}, expected {value!r}"
            )
    dsc_text = names["dsc"].read_text(encoding="utf-8")
    dsc = parse_control(dsc_text)
    if dsc.get("Source") != SOURCE_NAME or dsc.get("Version") != version:
        raise ReleaseError(".dsc Source/Version do not match the target")
    orig_name = f"{SOURCE_NAME}_{context.version}.orig.tar.gz"
    listed = verify_listed_files(changes_text, build)
    if (
        orig_name not in listed
        or names["dsc"].name not in listed
        or names["buildinfo"].name not in listed
    ):
        raise ReleaseError(".changes must list the orig tarball, .dsc and .buildinfo")
    if orig_name not in verify_listed_files(dsc_text, build):
        raise ReleaseError(".dsc does not reference the orig tarball")
    if _sha256(build / orig_name) != context.sha256:
        raise ReleaseError(
            "orig tarball in the source package does not match the context source hash"
        )
    files = tuple(build / name for name in listed if name != names["changes"].name)
    return SourceUpload(changes=names["changes"], files=files + (names["changes"],))


def sign_source(upload: SourceUpload, fingerprint: str) -> None:
    """Sign .dsc, refresh .buildinfo and sign it, refresh .changes and sign it, then verify."""
    build = upload.changes.parent
    changes_text = upload.changes.read_text(encoding="utf-8")
    entries = listed_files(changes_text)
    dsc = next(build / name for name in entries if name.endswith(".dsc"))
    buildinfo = next(build / name for name in entries if name.endswith(".buildinfo"))
    for path in (dsc, buildinfo, upload.changes):
        if path.read_text(encoding="utf-8").lstrip().startswith("-----BEGIN PGP"):
            raise ReleaseError(f"{path.name} is already signed")

    clearsign(dsc, fingerprint)
    dsc_bytes = dsc.read_bytes()

    buildinfo_text = buildinfo.read_text(encoding="utf-8")
    if dsc.name in listed_files(buildinfo_text):
        buildinfo.write_text(
            update_checksums(buildinfo_text, dsc.name, dsc_bytes), encoding="utf-8"
        )
    clearsign(buildinfo, fingerprint)

    changes_text = update_checksums(changes_text, dsc.name, dsc_bytes)
    changes_text = update_checksums(
        changes_text, buildinfo.name, buildinfo.read_bytes()
    )
    upload.changes.write_text(changes_text, encoding="utf-8")
    clearsign(upload.changes, fingerprint)

    for path in (dsc, buildinfo, upload.changes):
        verify_signature(path, fingerprint)
    verify_listed_files(upload.changes.read_text(encoding="utf-8"), build)
    verify_listed_files(dsc.read_text(encoding="utf-8"), build)
    verify_listed_files(buildinfo.read_text(encoding="utf-8"), build)


def _upload(upload: SourceUpload) -> str:
    if shutil.which("dput"):
        command(["dput", DPUT_TARGET, str(upload.changes)], timeout=UPLOAD_TIMEOUT)
        return "dput"
    try:
        with ftplib.FTP(UPLOAD_HOST, timeout=UPLOAD_TIMEOUT) as ftp:
            ftp.login()
            ftp.cwd(UPLOAD_DIR)
            for path in upload.files:
                with path.open("rb") as handle:
                    ftp.storbinary(f"STOR {path.name}", handle)
    except (OSError, ftplib.Error) as exc:
        raise ReleaseError(
            f"anonymous FTP upload to {UPLOAD_HOST} failed: {exc.__class__.__name__}"
        ) from exc
    return "ftp"


def _check_orig_against_ppa(context: ReleaseContext) -> None:
    """Launchpad rejects an orig tarball whose bytes differ from one already in the PPA."""
    orig_name = f"{SOURCE_NAME}_{context.version}.orig.tar.gz"
    for pub in _ppa_sources():
        try:
            same_upstream = (
                upstream_version(str(pub.get("source_package_version", "")))
                == context.version
            )
        except ReleaseError:
            continue
        if not same_upstream:
            continue
        urls = http_json(str(pub["self_link"]) + "?ws.op=sourceFileUrls")
        if not isinstance(urls, list):
            raise ReleaseError("unexpected Launchpad sourceFileUrls response")
        url = next(
            (u for u in urls if isinstance(u, str) and u.endswith("/" + orig_name)),
            None,
        )
        if url is None:
            continue
        request = urllib.request.Request(
            url, headers={"User-Agent": "krema-release-ppa"}
        )
        digest = hashlib.sha256()
        size = 0
        try:
            with urllib.request.urlopen(request, timeout=DOWNLOAD_TIMEOUT) as response:
                for chunk in iter(lambda: response.read(1 << 20), b""):
                    size += len(chunk)
                    if size > MAX_ORIG_BYTES:
                        raise ReleaseError(
                            "published orig tarball exceeds the size limit"
                        )
                    digest.update(chunk)
        except OSError as exc:
            raise ReleaseError(f"could not download the published {orig_name}") from exc
        if digest.hexdigest() != context.sha256:
            raise ReleaseError(
                f"the PPA already holds a different {orig_name}; Launchpad rejects a differing orig tarball"
            )
        return


class _StepError(Exception):
    def __init__(self, state: str, detail: str) -> None:
        super().__init__(detail)
        self.state = state
        self.detail = detail


def _target_states(reports: Sequence[Mapping[str, Any]]) -> list[str]:
    return [
        r["state"]
        for r in reports
        if r["state"] not in ("platform_blocked", "not_applicable")
    ]


def _readiness(context: ReleaseContext) -> tuple[str, str]:
    """Read-only preflight: published orig consistency, Launchpad-registered local key, builder."""
    _check_orig_against_ppa(context)
    return signing_fingerprint(), _builder()


def _prepare_one(
    context: ReleaseContext,
    tagged: TaggedPackaging,
    series: str,
    version: str,
    builder: str,
    fingerprint: str,
) -> SourceUpload:
    """Build, verify and sign one series' source package in scratch; never uploads."""
    try:
        build = _stage(context, tagged, series, version)
        _build(context, tagged, build, builder)
        upload = _check_built(context, build, series, version)
    except ReleaseError as exc:
        raise _StepError("failed", f"source build failed: {exc}") from exc
    try:
        sign_source(upload, fingerprint)
    except ReleaseError as exc:
        raise _StepError("blocked", f"signing failed: {exc}") from exc
    return upload


def prepare(context: ReleaseContext, *, series: Sequence[str] | None = None) -> dict:
    """Build and sign real source packages for target series in scratch, without uploading.

    Works for any target series regardless of publication state, so an already
    published release can exercise the full preparation path safely.
    """
    try:
        tagged = load_tagged_packaging(context)
        reports = _inspect(context, tagged)
    except ReleaseError as exc:
        return result(CHANNEL, "failed", str(exc), version=context.version)
    targets = [
        r for r in reports if r["state"] not in ("platform_blocked", "not_applicable")
    ]
    if series:
        unknown = sorted(set(series) - {r["series"] for r in targets})
        if unknown:
            return result(
                CHANNEL,
                "failed",
                f"not target series: {', '.join(unknown)}",
                version=context.version,
            )
        targets = [r for r in targets if r["series"] in set(series)]
    if not targets:
        return _summary(context, reports, "blocked")
    try:
        fingerprint, builder = _readiness(context)
        _test_signing(context, fingerprint)
    except ReleaseError as exc:
        return result(
            CHANNEL,
            "blocked",
            f"not ready: {exc}",
            version=context.version,
            series=reports,
        )
    prepared: list[dict[str, Any]] = []
    failures: list[str] = []
    for report in targets:
        entry: dict[str, Any] = {
            "series": report["series"],
            "package_version": report["package_version"],
        }
        try:
            upload = _prepare_one(
                context,
                tagged,
                report["series"],
                report["package_version"],
                builder,
                fingerprint,
            )
        except _StepError as exc:
            entry["state"], entry["detail"] = exc.state, exc.detail
            failures.append(exc.state)
        else:
            entry["state"] = "prepared"
            entry["detail"] = (
                f"built ({builder}), signed by {fingerprint}, checksums verified; not uploaded"
            )
            entry["artifacts"] = [str(path) for path in upload.files]
        prepared.append(entry)
    channel_status = (
        "failed" if "failed" in failures else "blocked" if failures else "planned"
    )
    detail = ", ".join(f"{e['series']}={e['state']}" for e in prepared)
    return result(
        CHANNEL,
        channel_status,
        detail,
        version=context.version,
        prepared=prepared,
        series=reports,
    )


def publish(context: ReleaseContext, *, execute: bool = False) -> dict:
    """Read-only plan (default), or build, sign and upload every missing target series."""
    try:
        tagged = load_tagged_packaging(context)
        reports = _inspect(context, tagged)
    except ReleaseError as exc:
        return result(CHANNEL, "failed", str(exc), version=context.version)
    missing = [r for r in reports if r["state"] == "missing"]
    if not missing:
        return _summary(context, reports, aggregate_state(_target_states(reports)))
    try:
        fingerprint, builder = _readiness(context)
        if execute:
            _test_signing(context, fingerprint)
    except ReleaseError as exc:
        for report in missing:
            report["state"], report["detail"] = "blocked", f"not ready: {exc}"
        return _summary(context, reports, "blocked")

    if not execute:
        for report in missing:
            report["detail"] = (
                f"would build ({builder}), sign with {fingerprint} and upload"
            )
        return _summary(context, reports, aggregate_state(_target_states(reports)))

    for report in missing:
        series, version = report["series"], report["package_version"]
        try:
            upload = _prepare_one(
                context, tagged, series, version, builder, fingerprint
            )
            method = _upload(upload)
        except _StepError as exc:
            report["state"], report["detail"] = exc.state, exc.detail
            continue
        except ReleaseError as exc:
            report["state"], report["detail"] = "failed", f"upload failed: {exc}"
            continue
        _write_marker(
            context,
            {
                "version": version,
                "series": series,
                "uploaded_at": datetime.now(timezone.utc).isoformat(),
                "method": method,
                "changes_sha256": _sha256(upload.changes),
            },
        )
        report["state"] = "submitted"
        report["detail"] = (
            f"signed source uploaded via {method}; Launchpad build results follow"
        )
        report["artifacts"] = [str(path) for path in upload.files]
    return _summary(context, reports, aggregate_state(_target_states(reports)))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Launchpad PPA channel adapter")
    sub = parser.add_subparsers(dest="action", required=True)
    for name in ("status", "publish", "prepare"):
        description = (
            "read-only plan; real uploads go through release.py publish --channel ppa --execute"
            if name == "publish"
            else None
        )
        cmd = sub.add_parser(name, description=description)
        cmd.add_argument("--context", type=Path, required=True)
        if name == "prepare":
            cmd.add_argument(
                "--series", action="append", help="limit to these target series"
            )
    args = parser.parse_args(argv)
    try:
        context = load_context(args.context)
    except ReleaseError as exc:
        print(json.dumps(result(CHANNEL, "failed", str(exc)), indent=2))
        return 1
    if args.action == "status":
        report = status(context)
    elif args.action == "prepare":
        report = prepare(context, series=args.series)
    else:
        report = publish(context)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 1 if report.get("status") in ("failed", "blocked") else 0


if __name__ == "__main__":
    raise SystemExit(main())
