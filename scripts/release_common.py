#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Shared release primitives: the verified release context, bounded commands.

Every release module imports from here. Nothing in this module performs an
external write; it only runs the commands its callers pass, reads JSON over
HTTP GET, and validates release inputs.

Standard library only, so the same code runs on the maintainer's Mac and,
later, on any CI runner with Python 3.11+.
"""

from __future__ import annotations

import contextlib
import gzip
import hashlib
import io
import json
import os
import posixpath
import re
import signal
import stat
import subprocess
import tarfile
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal, overload

SCHEMA_VERSION = 1
PROJECT = "krema"
USER_AGENT = "krema-release (https://github.com/isac322/krema)"

BANNER_PATH = "branding/social/release-banner.png"

STATUSES = frozenset(
    {
        "planned",
        "already_published",
        "submitted",
        "published",
        "pending",
        "blocked",
        "failed",
        "browser_required",
        "not_applicable",
    }
)

CONTEXT_FIELDS = (
    "repository",
    "tag",
    "version",
    "commit",
    "source_dir",
    "archive",
    "sha256",
    "source_url",
    "work_dir",
    "notes_path",
    "debian_revision",
)

TAG_RE = re.compile(r"^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
REPOSITORY_RE = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})/[A-Za-z0-9._-]{1,100}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
DEBIAN_REVISION_RE = re.compile(r"^[0-9A-Za-z][0-9A-Za-z.+~]*$")

_SECRET_PATTERNS = (
    re.compile(r"gh[pousr]_[A-Za-z0-9]{20,}"),
    re.compile(r"github_pat_[A-Za-z0-9_]{20,}"),
    re.compile(r"(?i)(authorization:\s*)(\S+\s+)?\S+"),
    re.compile(r"(?i)(bearer|token)\s+[A-Za-z0-9._~+/=-]{12,}"),
    re.compile(r"(?i)((?:password|passwd|secret|token|api[_-]?key)\s*[=:]\s*)\S+"),
    re.compile(
        r"-----BEGIN [A-Z ]*PRIVATE KEY-----.*?-----END [A-Z ]*PRIVATE KEY-----", re.S
    ),
)
_SECRET_KEY_RE = re.compile(
    r"(?i)(token|password|passwd|secret|credential|cookie|private)"
)


class ReleaseError(RuntimeError):
    """A release step cannot proceed safely. The message never holds secrets."""


class CommandError(ReleaseError):
    """A command exited non-zero. ``stderr`` is redacted and truncated."""

    def __init__(self, message: str, *, returncode: int, stderr: str) -> None:
        super().__init__(message)
        self.returncode = returncode
        self.stderr = stderr


class HttpError(ReleaseError):
    """An HTTP GET returned an error status (``status``) or failed (``None``)."""

    def __init__(self, message: str, *, status: int | None) -> None:
        super().__init__(message)
        self.status = status


def redact(text: str) -> str:
    """Mask token-, password- and private-key-shaped substrings."""
    for pattern in _SECRET_PATTERNS:
        if pattern.groups:
            text = pattern.sub(lambda m: (m.group(1) or "") + "[redacted]", text)
        else:
            text = pattern.sub("[redacted]", text)
    return text


def _tail(text: str, limit: int = 600) -> str:
    text = redact(text.strip())
    return text if len(text) <= limit else "…" + text[-limit:]


def _program(argv: Sequence[str]) -> str:
    # Only the program and its first non-option word; later arguments may be
    # tokens, URLs with credentials or file contents.
    name = os.path.basename(str(argv[0]))
    sub = next((str(a) for a in argv[1:2] if not str(a).startswith("-")), "")
    return f"{name} {sub}".strip()


def _kill_group(pgid: int, sig: int) -> None:
    with contextlib.suppress(ProcessLookupError, PermissionError):
        os.killpg(pgid, sig)


def _group_alive(pgid: int) -> bool:
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def terminate_process(proc: subprocess.Popen[Any], *, grace: float = 3.0) -> None:
    """SIGTERM the process group of ``proc``, SIGKILL survivors, then reap it.

    ``proc`` must be its own group leader (``start_new_session=True``), so the
    signals also reach every descendant that stayed in the group. The leader
    may already have exited; the group is still signalled while members remain,
    because a pipe-holding grandchild must not outlive the leader.
    """
    pgid = proc.pid
    if proc.poll() is not None and not _group_alive(pgid):
        return
    _kill_group(pgid, signal.SIGTERM)
    deadline = time.monotonic() + max(0.0, grace)
    while (proc.poll() is None or _group_alive(pgid)) and time.monotonic() < deadline:
        time.sleep(0.02)
    if proc.poll() is None or _group_alive(pgid):
        _kill_group(pgid, signal.SIGKILL)
        deadline = time.monotonic() + 5.0
        while _group_alive(pgid) and time.monotonic() < deadline:
            time.sleep(0.02)
    proc.wait()


_LIVE_PROCS: set[subprocess.Popen[Any]] = set()
_SIGNAL_GRACE = 3.0
_handlers_installed = False


def _terminate_children(signum: int, _frame: object) -> None:
    """Forward an external stop signal to every live child group, then exit.

    An interrupt or termination signal propagates from ``release.py`` to its
    channel tools: each layer's handler terminates its own child group before
    exiting, so no nested session is left running.
    """
    procs = list(_LIVE_PROCS)
    for proc in procs:
        proc.poll()
        _kill_group(proc.pid, signal.SIGTERM)
    deadline = time.monotonic() + _SIGNAL_GRACE
    alive = [p for p in procs if _group_alive(p.pid)]
    while alive and time.monotonic() < deadline:
        time.sleep(0.05)
        alive = [p for p in alive if _group_alive(p.pid)]
    for proc in alive:
        _kill_group(proc.pid, signal.SIGKILL)
    raise SystemExit(128 + signum)


def _install_signal_handlers() -> None:
    global _handlers_installed
    if _handlers_installed:
        return
    _handlers_installed = True
    try:
        signal.signal(signal.SIGTERM, _terminate_children)
        signal.signal(signal.SIGINT, _terminate_children)
    except ValueError:
        pass  # not running on the main thread


@overload
def run_process(
    argv: Sequence[str],
    *,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
    timeout: float | None = None,
    grace: float = 3.0,
    binary: Literal[False] = False,
) -> subprocess.CompletedProcess[str]: ...


@overload
def run_process(
    argv: Sequence[str],
    *,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
    timeout: float | None = None,
    grace: float = 3.0,
    binary: Literal[True],
) -> subprocess.CompletedProcess[bytes]: ...


def run_process(
    argv: Sequence[str],
    *,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
    timeout: float | None = None,
    grace: float = 3.0,
    binary: bool = False,
) -> subprocess.CompletedProcess[Any]:
    """Run ``argv`` in its own session and return a CompletedProcess.

    The child leads a new process group, so a timeout or an external SIGTERM
    (via the handlers above) terminates the whole group: SIGTERM, a bounded
    ``grace``, then SIGKILL — even when the leader already exited but a
    descendant still holds the stdout/stderr pipes. ``subprocess.TimeoutExpired``
    from communicate is re-raised after the group is dead; Popen errors
    propagate unchanged. ``env`` is passed through (None inherits
    ``os.environ``). ``binary=True`` captures stdout/stderr as raw bytes;
    the default decodes them as UTF-8 with replacement.
    """
    if not argv:
        raise ReleaseError("empty command")
    if timeout is not None and timeout <= 0:
        raise ReleaseError("command timeout must be positive")
    _install_signal_handlers()
    proc = subprocess.Popen(
        [str(a) for a in argv],
        cwd=str(cwd) if cwd is not None else None,
        env=dict(env) if env is not None else None,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=not binary,
        encoding=None if binary else "utf-8",
        errors=None if binary else "replace",
        start_new_session=True,
    )
    _LIVE_PROCS.add(proc)
    try:
        try:
            stdout, stderr = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            terminate_process(proc, grace=grace)
            try:
                proc.communicate(timeout=max(5.0, grace * 2))
            except subprocess.TimeoutExpired:
                # A descendant escaped the group and still holds a pipe open;
                # the leader is already dead, so just close and reap.
                for stream in (proc.stdout, proc.stderr):
                    if stream is not None:
                        stream.close()
                proc.wait()
            raise
        except BaseException:
            terminate_process(proc, grace=grace)
            raise
    finally:
        _LIVE_PROCS.discard(proc)
    return subprocess.CompletedProcess(list(argv), proc.returncode, stdout, stderr)


def command(
    argv: Sequence[str],
    *,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
    timeout: int = 120,
) -> str:
    """Run ``argv`` without a shell and return its stdout.

    ``env`` entries override the inherited environment. stdin is closed, so a
    command that tries to prompt fails instead of hanging. A non-zero exit,
    timeout or missing program raises; diagnostics name only the program and
    a redacted stderr tail, never the full argument list. The child runs in
    its own process group via ``run_process``, so a timeout or an external
    SIGTERM also reaches descendants.
    """
    if not argv:
        raise ReleaseError("empty command")
    if timeout <= 0:
        raise ReleaseError("command timeout must be positive")
    merged = None
    if env is not None:
        merged = dict(os.environ)
        merged.update(env)
    program = _program(argv)
    try:
        proc = run_process(argv, cwd=cwd, env=merged, timeout=timeout)
    except FileNotFoundError:
        raise ReleaseError(f"{program}: program not found on PATH") from None
    except subprocess.TimeoutExpired:
        raise ReleaseError(f"{program}: timed out after {timeout}s") from None
    if proc.returncode != 0:
        stderr = _tail(proc.stderr or proc.stdout or "")
        raise CommandError(
            f"{program} failed (exit {proc.returncode}): {stderr}",
            returncode=proc.returncode,
            stderr=stderr,
        )
    return proc.stdout


def _safe_url(url: str) -> str:
    parts = urllib.parse.urlsplit(url)
    host = parts.hostname or ""
    return f"{parts.scheme}://{host}{parts.path}"


def http_json(
    url: str,
    *,
    headers: Mapping[str, str] | None = None,
    timeout: int = 30,
) -> Any:
    """GET ``url`` and decode JSON. Errors name the URL without query/userinfo."""
    if urllib.parse.urlsplit(url).scheme != "https":
        raise ReleaseError(f"refusing non-HTTPS URL {_safe_url(url)}")
    if timeout <= 0:
        raise ReleaseError("HTTP timeout must be positive")
    request = urllib.request.Request(url, method="GET")
    request.add_header("User-Agent", USER_AGENT)
    request.add_header("Accept", "application/json")
    for key, value in (headers or {}).items():
        request.add_header(key, value)
    safe = _safe_url(url)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read()
    except urllib.error.HTTPError as exc:
        raise HttpError(
            f"GET {safe} failed: HTTP {exc.code}", status=exc.code
        ) from None
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        reason = getattr(exc, "reason", exc)
        raise HttpError(
            f"GET {safe} failed: {redact(str(reason))}", status=None
        ) from None
    try:
        return json.loads(body)
    except ValueError:
        raise ReleaseError(f"GET {safe} returned invalid JSON") from None


def result(channel: str, status: str, detail: str, **extra: Any) -> dict:
    """Build a channel result. ``status`` must come from ``STATUSES``."""
    if status not in STATUSES:
        raise ReleaseError(f"{channel}: invalid result status {status!r}")
    for key in extra:
        if key in {"channel", "status", "detail"} or _SECRET_KEY_RE.search(key):
            raise ReleaseError(f"{channel}: result field {key!r} is not allowed")
    out = {"channel": channel, "status": status, "detail": redact(detail)}
    out.update(extra)
    return out


# ---------------------------------------------------------------------------
# Versions and source metadata
# ---------------------------------------------------------------------------


def parse_tag(tag: str) -> str:
    """Return ``X.Y.Z`` for a ``vX.Y.Z`` tag, or raise."""
    match = TAG_RE.match(tag)
    if not match:
        raise ReleaseError(f"tag {tag!r} is not a vX.Y.Z semantic version tag")
    return tag[1:]


def asset_name(version: str) -> str:
    return f"{PROJECT}-{version}.tar.gz"


def source_root_name(version: str) -> str:
    return f"{PROJECT}-{version}"


def asset_url(repository: str, tag: str, version: str) -> str:
    return (
        f"https://github.com/{repository}/releases/download/{tag}/{asset_name(version)}"
    )


def remote_url(repository: str) -> str:
    return f"https://github.com/{repository}.git"


def remote_tag_oids(repository: str, tag: str) -> dict[str, str]:
    """Remote refs for ``tag``: ``refs/tags/<tag>`` and, when annotated, its
    peeled ``refs/tags/<tag>^{}`` commit. An absent tag maps to no entry."""
    out = command(
        [
            "git",
            "ls-remote",
            remote_url(repository),
            f"refs/tags/{tag}",
            f"refs/tags/{tag}^{{}}",
        ],
        timeout=60,
    )
    return {
        line.split("\t")[1]: line.split("\t")[0]
        for line in out.splitlines()
        if "\t" in line
    }


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256sums_text(sha256: str, version: str) -> str:
    """Content of the published ``SHA256SUMS`` (``sha256sum`` format)."""
    return f"{sha256}  {asset_name(version)}\n"


def _read_text(source_dir: Path, rel: str) -> str:
    path = source_dir / rel
    if path.is_symlink() or not path.is_file():
        raise ReleaseError(f"tagged source lacks regular file {rel}")
    return path.read_text(encoding="utf-8")


def changelog_section(changelog: str, version: str) -> str | None:
    """Body of ``## [version] - date`` in a Keep a Changelog file, else None."""
    heading = re.compile(
        rf"^## \[{re.escape(version)}\](?:\s+-\s+\d{{4}}-\d{{2}}-\d{{2}})?\s*$"
    )
    lines = changelog.splitlines()
    for index, line in enumerate(lines):
        if heading.match(line):
            body: list[str] = []
            for nxt in lines[index + 1 :]:
                if nxt.startswith("## "):
                    break
                body.append(nxt)
            return "\n".join(body).strip()
    return None


def release_notes(changelog: str, version: str, banner_url: str | None) -> str:
    """Deterministic GitHub release notes: banner line plus changelog section."""
    section = changelog_section(changelog, version)
    if not section:
        raise ReleaseError(f"CHANGELOG.md section {version} is empty")
    head = (
        f"![Krema v{version} release banner — lightweight KDE Plasma 6 dock for Wayland]({banner_url})\n\n"
        if banner_url
        else ""
    )
    return f"{head}{section}\n"


def source_versions(source_dir: Path) -> dict[str, str | None]:
    """Version fields declared by a source tree (None when absent)."""
    found: dict[str, str | None] = {}

    cmake = _read_text(source_dir, "CMakeLists.txt")
    m = re.search(
        r"^\s*project\(\s*krema\s+VERSION\s+([0-9][0-9.]*)\b", cmake, re.M | re.I
    )
    found["CMakeLists.txt"] = m.group(1) if m else None

    try:
        root = ET.fromstring(_read_text(source_dir, "src/com.bhyoo.krema.metainfo.xml"))
    except ET.ParseError as exc:
        raise ReleaseError(f"metainfo XML is invalid: {exc}") from None
    first = root.find("releases/release")
    found["src/com.bhyoo.krema.metainfo.xml"] = (
        first.get("version") if first is not None else None
    )

    spec = _read_text(source_dir, "packaging/obs/krema.spec")
    m = re.search(r"^Version:\s*(\S+)\s*$", spec, re.M)
    found["packaging/obs/krema.spec"] = m.group(1) if m else None

    dsc = _read_text(source_dir, "packaging/obs/krema.dsc")
    m = re.search(r"^Version:\s*(\S+)\s*$", dsc, re.M)
    found["packaging/obs/krema.dsc"] = m.group(1) if m else None
    m = re.search(r"^DEBTRANSFORM-TAR:\s*(\S+)\s*$", dsc, re.M)
    found["packaging/obs/krema.dsc DEBTRANSFORM-TAR"] = m.group(1) if m else None

    debchangelog = _read_text(source_dir, "packaging/obs/debian.changelog")
    first_line = debchangelog.splitlines()[0] if debchangelog else ""
    m = re.match(r"^krema \(([^)\s]+)\)\s", first_line)
    found["packaging/obs/debian.changelog"] = m.group(1) if m else None
    return found


def check_source_versions(source_dir: Path, version: str) -> str:
    """Require every tagged version field to equal ``version``.

    Checks CMake, the CHANGELOG section, the newest metainfo release, the RPM
    spec, the DSC (version and tarball) and the top debian.changelog entry.
    The Arch PKGBUILD may lag on purpose (the AUR adapter sets it from the
    context), so it is not checked. Returns the Debian revision shared by the
    DSC and debian.changelog.
    """
    found = source_versions(source_dir)
    problems: list[str] = []

    def expect(key: str, wanted: str) -> None:
        if found.get(key) != wanted:
            problems.append(f"{key}: {found.get(key)!r} != {wanted!r}")

    expect("CMakeLists.txt", version)
    expect("src/com.bhyoo.krema.metainfo.xml", version)
    expect("packaging/obs/krema.spec", version)
    expect("packaging/obs/krema.dsc DEBTRANSFORM-TAR", asset_name(version))

    revisions: list[str] = []
    for key in ("packaging/obs/krema.dsc", "packaging/obs/debian.changelog"):
        value = found.get(key) or ""
        upstream, sep, revision = value.rpartition("-")
        if not sep or upstream != version or not DEBIAN_REVISION_RE.match(revision):
            problems.append(f"{key}: {value!r} is not {version}-<revision>")
        else:
            revisions.append(revision)
    if len(revisions) == 2 and revisions[0] != revisions[1]:
        problems.append(
            f"Debian revision differs: krema.dsc -{revisions[0]} vs debian.changelog -{revisions[1]}"
        )

    if changelog_section(_read_text(source_dir, "CHANGELOG.md"), version) is None:
        problems.append(f"CHANGELOG.md: no '## [{version}]' section")

    if problems:
        raise ReleaseError("tagged source version mismatch: " + "; ".join(problems))
    return revisions[0]


# ---------------------------------------------------------------------------
# Archive and source tree integrity
# ---------------------------------------------------------------------------

# Manifest values: ("file", "<sha256>", executable) or ("link", "<target>", False).
Manifest = dict[str, tuple[str, str, bool]]


def _check_member_path(name: str, root: str) -> str:
    """Return ``name`` relative to ``root/`` or raise on unsafe paths."""
    if not name or name.startswith("/") or "\\" in name or "\x00" in name:
        raise ReleaseError(f"archive member {name!r} has an unsafe path")
    parts = name.rstrip("/").split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise ReleaseError(f"archive member {name!r} has an unsafe path")
    if parts[0] != root:
        raise ReleaseError(f"archive member {name!r} is outside top directory {root}/")
    return "/".join(parts[1:])


def _check_link_target(rel: str, target: str) -> None:
    if not target or target.startswith("/") or "\\" in target or "\x00" in target:
        raise ReleaseError(f"symlink {rel!r} -> {target!r} is not a safe relative link")
    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(rel), target))
    if resolved == ".." or resolved.startswith("../") or resolved.startswith("/"):
        raise ReleaseError(f"symlink {rel!r} -> {target!r} escapes the source tree")


def _check_no_link_parent(rel: str, links: Mapping[str, str]) -> None:
    parts = rel.split("/")
    for depth in range(1, len(parts)):
        if "/".join(parts[:depth]) in links:
            raise ReleaseError(f"archive member {rel!r} is below a symlink")


def _check_no_link_chain(rel: str, target: str, links: Mapping[str, str]) -> None:
    """Reject links whose target passes through another symlink.

    Lexical normalization and kernel resolution disagree once a target walks
    through a symlink (``a -> c/../x`` with ``c -> .``), so chains are refused.
    """
    parts = [p for p in posixpath.dirname(rel).split("/") if p]
    for component in target.split("/"):
        if component in ("", "."):
            continue
        if component == "..":
            if not parts:
                raise ReleaseError(
                    f"symlink {rel!r} -> {target!r} escapes the source tree"
                )
            parts.pop()
            continue
        parts.append(component)
        prefix = "/".join(parts)
        if prefix in links and prefix != rel:
            raise ReleaseError(
                f"symlink {rel!r} -> {target!r} passes through symlink {prefix!r}"
            )
        if prefix == rel:
            raise ReleaseError(f"symlink {rel!r} points at itself")


def read_archive(
    data: bytes, version: str
) -> tuple[str | None, list[tuple[tarfile.TarInfo, str]]]:
    """Validate a release tarball and return (pax commit comment, members).

    Accepts only directories, regular files and relative symlinks that stay
    inside the single ``krema-<version>/`` top directory. Each returned
    member is paired with its path relative to that directory.
    """
    root = source_root_name(version)
    try:
        tar = tarfile.open(fileobj=io.BytesIO(data), mode="r:gz")
    except (tarfile.TarError, OSError, EOFError) as exc:
        raise ReleaseError(f"archive is not a gzip tarball: {exc}") from None
    members: list[tuple[tarfile.TarInfo, str]] = []
    seen: set[str] = set()
    links: dict[str, str] = {}
    with tar:
        try:
            infos = tar.getmembers()
        except (tarfile.TarError, OSError, EOFError) as exc:
            raise ReleaseError(f"archive is corrupt: {exc}") from None
        comment = tar.pax_headers.get("comment")
        for info in infos:
            if info.type == tarfile.XGLTYPE:
                continue
            rel = _check_member_path(info.name, root)
            if rel in seen:
                raise ReleaseError(f"archive member {info.name!r} appears twice")
            seen.add(rel)
            if info.isdir():
                pass
            elif info.isreg():
                if not rel:
                    raise ReleaseError("archive top directory is a file")
            elif info.issym():
                if not rel:
                    raise ReleaseError("archive top directory is a symlink")
                _check_link_target(rel, info.linkname)
                links[rel] = info.linkname
            else:
                raise ReleaseError(
                    f"archive member {info.name!r} is not a file, directory or symlink"
                )
            members.append((info, rel))
        if not members:
            raise ReleaseError("archive is empty")
        for rel in seen:
            _check_no_link_parent(rel, links)
        for rel, target in links.items():
            _check_no_link_chain(rel, target, links)
        return comment, members


def archive_manifest(data: bytes, version: str) -> tuple[str | None, Manifest]:
    """Validate a release tarball; return (pax commit comment, manifest)."""
    comment, members = read_archive(data, version)
    manifest: Manifest = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        for info, rel in members:
            if info.isreg():
                handle = tar.extractfile(info)
                if handle is None:
                    raise ReleaseError(f"archive member {info.name!r} is unreadable")
                manifest[rel] = (
                    "file",
                    sha256_bytes(handle.read()),
                    bool(info.mode & 0o111),
                )
            elif info.issym():
                manifest[rel] = ("link", info.linkname, False)
    if not manifest:
        raise ReleaseError("archive holds no files")
    return comment, manifest


def tree_manifest(source_dir: Path) -> Manifest:
    """Manifest of an extracted source tree; rejects unsafe entries."""
    if source_dir.is_symlink() or not source_dir.is_dir():
        raise ReleaseError(f"{source_dir} is not a real directory")
    manifest: Manifest = {}
    for current, dirs, files in os.walk(source_dir, followlinks=False):
        base = Path(current)
        for name in list(dirs) + files:
            path = base / name
            rel = path.relative_to(source_dir).as_posix()
            mode = os.lstat(path).st_mode
            if stat.S_ISLNK(mode):
                target = os.readlink(path)
                _check_link_target(rel, target)
                manifest[rel] = ("link", target, False)
                if name in dirs:
                    dirs.remove(name)
            elif stat.S_ISREG(mode):
                manifest[rel] = ("file", sha256_file(path), bool(mode & 0o111))
            elif not stat.S_ISDIR(mode):
                raise ReleaseError(
                    f"{rel} in source tree is not a file, directory or symlink"
                )
    links = {rel: entry[1] for rel, entry in manifest.items() if entry[0] == "link"}
    for rel, target in links.items():
        _check_no_link_chain(rel, target, links)
    return manifest


def compare_manifests(expected: Manifest, actual: Manifest) -> list[str]:
    """Human-readable differences (empty when identical)."""
    problems: list[str] = []
    for rel in sorted(expected.keys() - actual.keys())[:5]:
        problems.append(f"missing {rel}")
    for rel in sorted(actual.keys() - expected.keys())[:5]:
        problems.append(f"unexpected {rel}")
    changed = [
        rel
        for rel in sorted(expected.keys() & actual.keys())
        if expected[rel] != actual[rel]
    ]
    problems.extend(f"changed {rel}" for rel in changed[:5])
    return problems


def extract_archive(data: bytes, version: str, destination: Path) -> Path:
    """Safely extract a validated tarball; return ``destination/krema-<version>``.

    ``destination/krema-<version>`` must not exist yet. Ownership and
    timestamps from the tarball are ignored.
    """
    _, members = read_archive(data, version)
    target = destination / source_root_name(version)
    if os.path.lexists(target):
        raise ReleaseError(f"{target} already exists")
    target.mkdir(mode=0o755)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        for info, rel in members:
            if not rel:
                continue
            path = target / rel
            if info.isdir():
                path.mkdir(mode=0o755, parents=True, exist_ok=True)
                continue
            path.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
            if info.issym():
                os.symlink(info.linkname, path)
                continue
            handle = tar.extractfile(info)
            if handle is None:
                raise ReleaseError(f"archive member {info.name!r} is unreadable")
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
            fd = os.open(path, flags, 0o755 if info.mode & 0o111 else 0o644)
            with os.fdopen(fd, "wb") as out:
                out.write(handle.read())
    return target


def gzip_deterministic(raw_tar: bytes) -> bytes:
    """Gzip with a zero timestamp and no file name, so equal input = equal bytes."""
    out = io.BytesIO()
    with gzip.GzipFile(
        filename="", mode="wb", fileobj=out, compresslevel=9, mtime=0
    ) as gz:
        gz.write(raw_tar)
    return out.getvalue()


# ---------------------------------------------------------------------------
# Release context
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class ReleaseContext:
    repository: str
    tag: str
    version: str
    commit: str
    source_dir: Path
    archive: Path
    sha256: str
    source_url: str
    work_dir: Path
    notes_path: Path
    debian_revision: str


def context_to_json(context: ReleaseContext) -> dict:
    data: dict[str, Any] = {"schema_version": SCHEMA_VERSION}
    for name in CONTEXT_FIELDS:
        value = getattr(context, name)
        data[name] = str(value) if isinstance(value, Path) else value
    return data


def _inside(path: Path, work_dir: Path, what: str) -> Path:
    if not path.is_absolute():
        raise ReleaseError(f"context {what} must be an absolute path")
    if os.path.lexists(path) and path.is_symlink():
        raise ReleaseError(f"context {what} {path} is a symlink")
    resolved = path.resolve()
    if resolved != path:
        raise ReleaseError(f"context {what} {path} is not a canonical path")
    if resolved == work_dir or not resolved.is_relative_to(work_dir):
        raise ReleaseError(f"context {what} {path} is outside work_dir {work_dir}")
    if resolved.parent != work_dir:
        raise ReleaseError(f"context {what} {path} must sit directly in work_dir")
    return resolved


def load_context(path: Path) -> ReleaseContext:
    """Load and fully re-verify a ``context.json`` written by ``release.py prepare``.

    Validates the schema and identifiers, that every path is canonical,
    non-symlink and directly inside ``work_dir``, the archive hash, that the
    archive's embedded commit equals ``commit``, that the extracted source
    tree is byte-identical to the archive, that the source's version fields
    agree with the tag and Debian revision, and that ``notes_path`` equals
    the notes ``expected_notes`` regenerates from that verified tree.
    """
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ReleaseError(f"context {path} is not a regular file")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ReleaseError(f"context {path} is unreadable: {exc}") from None
    if not isinstance(data, dict):
        raise ReleaseError("context must be a JSON object")
    if data.get("schema_version") != SCHEMA_VERSION:
        raise ReleaseError(f"context schema_version must be {SCHEMA_VERSION}")
    keys = set(data) - {"schema_version"}
    if keys != set(CONTEXT_FIELDS):
        missing = sorted(set(CONTEXT_FIELDS) - keys)
        extra = sorted(keys - set(CONTEXT_FIELDS))
        raise ReleaseError(
            f"context fields mismatch: missing={missing} unexpected={extra}"
        )
    for name in CONTEXT_FIELDS:
        if not isinstance(data[name], str) or not data[name]:
            raise ReleaseError(f"context {name} must be a non-empty string")

    repository, tag, version = data["repository"], data["tag"], data["version"]
    if not REPOSITORY_RE.match(repository) or ".." in repository:
        raise ReleaseError(f"context repository {repository!r} is not owner/name")
    if parse_tag(tag) != version:
        raise ReleaseError(f"context version {version!r} does not match tag {tag!r}")
    if not COMMIT_RE.match(data["commit"]):
        raise ReleaseError("context commit must be a full lowercase SHA-1")
    if not SHA256_RE.match(data["sha256"]):
        raise ReleaseError("context sha256 must be 64 lowercase hex digits")
    if not DEBIAN_REVISION_RE.match(data["debian_revision"]):
        raise ReleaseError("context debian_revision is invalid")
    if data["source_url"] != asset_url(repository, tag, version):
        raise ReleaseError(
            f"context source_url must be {asset_url(repository, tag, version)}"
        )

    work_dir = Path(data["work_dir"])
    if not work_dir.is_absolute() or work_dir.is_symlink() or not work_dir.is_dir():
        raise ReleaseError("context work_dir must be an absolute real directory")
    if work_dir.resolve() != work_dir:
        raise ReleaseError("context work_dir is not a canonical path")
    source_dir = _inside(Path(data["source_dir"]), work_dir, "source_dir")
    archive = _inside(Path(data["archive"]), work_dir, "archive")
    notes_path = _inside(Path(data["notes_path"]), work_dir, "notes_path")
    if source_dir.name != source_root_name(version) or not source_dir.is_dir():
        raise ReleaseError(
            f"context source_dir must be directory {source_root_name(version)}"
        )
    if archive.name != asset_name(version) or not archive.is_file():
        raise ReleaseError(
            f"context archive must be regular file {asset_name(version)}"
        )
    if not notes_path.is_file():
        raise ReleaseError("context notes_path must be a regular file")

    archive_bytes = archive.read_bytes()
    if sha256_bytes(archive_bytes) != data["sha256"]:
        raise ReleaseError(f"archive {archive.name} does not match context sha256")
    comment, expected = archive_manifest(archive_bytes, version)
    if comment != data["commit"]:
        raise ReleaseError("archive was not generated from the context commit")
    differences = compare_manifests(expected, tree_manifest(source_dir))
    if differences:
        raise ReleaseError("source_dir differs from archive: " + ", ".join(differences))
    revision = check_source_versions(source_dir, version)
    if revision != data["debian_revision"]:
        raise ReleaseError(
            f"context debian_revision {data['debian_revision']!r} != tagged revision {revision!r}"
        )

    context = ReleaseContext(
        repository=repository,
        tag=tag,
        version=version,
        commit=data["commit"],
        source_dir=source_dir,
        archive=archive,
        sha256=data["sha256"],
        source_url=data["source_url"],
        work_dir=work_dir,
        notes_path=notes_path,
        debian_revision=revision,
    )
    if notes_path.read_bytes() != expected_notes(context):
        raise ReleaseError(
            "context notes_path does not match the notes generated from the tagged source"
        )
    return context


def expected_notes(context: ReleaseContext) -> bytes:
    """The notes ``release.py prepare`` writes for this exact tagged tree.

    Deterministic: the changelog section of the verified ``source_dir`` plus
    the banner line iff the tagged tree carries ``BANNER_PATH`` (any member,
    matching ``git cat-file -e``). Compared byte-for-byte, never rewritten.
    """
    banner_url = None
    if os.path.lexists(context.source_dir / BANNER_PATH):
        banner_url = f"https://media.githubusercontent.com/media/{context.repository}/{context.tag}/{BANNER_PATH}"
    changelog = _read_text(context.source_dir, "CHANGELOG.md")
    return release_notes(changelog, context.version, banner_url).encode()
