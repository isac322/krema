#!/usr/bin/env python3
"""Build an offline store upload kit for a verified Krema release.

The command reads a verified release context produced by ``scripts/release.py
prepare``, renders the three channel schemas for that release, and writes a
copyable directory plus a ZIP archive. When the tagged source tree carries
``packaging/submissions/``, those tagged schemas, descriptions, and media are
the authoritative templates; otherwise the templates installed next to this
script are used and the kit marks them for human review. Version, tag, commit,
source archive bytes, and SHA-256 always come from the context. It never
authenticates, submits forms, invokes Git, or creates a Flatpak manifest.
"""

from __future__ import annotations

import argparse
import io
import json
import posixpath
import re
import shutil
import sys
import tarfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from typing import Any, Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_common import (  # noqa: E402
    ReleaseContext,
    ReleaseError,
    asset_name,
    asset_url,
    load_context,
    read_archive,
    sha256_bytes,
    source_root_name,
)

ROOT = Path(__file__).resolve().parent.parent
SCHEMA_DIR = Path("packaging/submissions")
CHANNELS = ("kde-store", "alternativeto", "launchpad")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
LFS_PREFIX = b"version https://git-lfs.github.com/spec/v1"
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
REPOSITORY_RE = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})/[A-Za-z0-9._-]{1,100}$")
TEMPLATE_TAGGED = "tagged-source"
TEMPLATE_INSTALLED = "installed-fallback"
URL_RE = re.compile(r"https?://", re.IGNORECASE)
EMAIL_RE = re.compile(r"\b[^\s@]+@[^\s@]+\.[^\s@]+\b")
PLACEHOLDER_RE = re.compile(
    r"\b(?:TODO|TBD|FIXME|PLACEHOLDER|UNKNOWN)\b", re.IGNORECASE
)
TOKEN_RE = re.compile(r"\{\{([a-z_][a-z0-9_]*)\}\}")
CMAKE_VERSION_RE = re.compile(
    r"project\s*\(\s*krema\b[^)]*?\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
    re.IGNORECASE | re.DOTALL,
)
MAX_DOWNLOAD_BYTES = 128 * 1024 * 1024


class KitError(ReleaseError):
    """A user-correctable input or output error."""


def fail(message: str) -> None:
    raise KitError(message)


def repo_path(
    value: Any, description: str, root: Path, *, must_exist: bool = True
) -> Path:
    """Resolve a safe tool-root-relative path and reject symlink escapes."""
    if not isinstance(value, str) or not value or "\x00" in value:
        fail(f"{description} must be a non-empty repository-relative path")
    candidate = Path(value)
    if candidate.is_absolute() or ".." in candidate.parts:
        fail(f"{description} is not repository-relative: {value!r}")
    try:
        resolved = (root / candidate).resolve(strict=must_exist)
    except OSError as exc:
        fail(f"{description} cannot be resolved: {value!r} ({exc})")
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        fail(f"{description} escapes the repository: {value!r}")
    if must_exist and not resolved.is_file():
        fail(f"{description} is not a regular file: {value!r}")
    return resolved


def safe_filename(value: Any, description: str) -> str:
    if not isinstance(value, str) or not value or value in {".", ".."}:
        fail(f"{description} must be a non-empty filename")
    if Path(value).name != value or "/" in value or "\\" in value or "\x00" in value:
        fail(f"{description} must not contain a directory: {value!r}")
    return value


def reject_placeholders(value: Any, location: str) -> None:
    if value is None:
        fail(f"{location} contains null")
    if isinstance(value, str) and PLACEHOLDER_RE.search(value):
        fail(f"{location} contains a placeholder value")
    if isinstance(value, dict):
        for key, item in value.items():
            reject_placeholders(item, f"{location}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            reject_placeholders(item, f"{location}[{index}]")


def render(value: Any, tokens: dict[str, str], location: str) -> Any:
    """Replace ``{{token}}`` release references with the verified target release."""
    if isinstance(value, str):

        def substitute(match: re.Match[str]) -> str:
            name = match.group(1)
            if name not in tokens:
                fail(f"{location} uses unknown release token {{{{{name}}}}}")
            return tokens[name]

        rendered = TOKEN_RE.sub(substitute, value)
        if "{{" in rendered or "}}" in rendered:
            fail(f"{location} contains a malformed release token")
        return rendered
    if isinstance(value, dict):
        return {
            key: render(item, tokens, f"{location}.{key}")
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [
            render(item, tokens, f"{location}[{index}]")
            for index, item in enumerate(value)
        ]
    return value


def read_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(f"cannot read JSON {path}: {exc}")
    if not isinstance(data, dict):
        fail(f"submission JSON must contain an object: {path}")
    reject_placeholders(data, str(path))
    return data


def require_string(mapping: dict[str, Any], key: str, location: str) -> str:
    value = mapping.get(key)
    if not isinstance(value, str) or not value.strip():
        fail(f"{location}.{key} must be a non-empty string")
    return value


def validate_png(
    path: Path, expected_width: Any, expected_height: Any, location: str
) -> None:
    if not isinstance(expected_width, int) or not isinstance(expected_height, int):
        fail(f"{location} width and height must be integers")
    try:
        header = path.read_bytes()
    except OSError as exc:
        fail(f"cannot read {location}: {exc}")
    if header.startswith(LFS_PREFIX) or LFS_PREFIX in header[:256]:
        fail(f"{location} is a Git LFS pointer, not image data")
    if len(header) < 24 or header[:8] != PNG_SIGNATURE or header[12:16] != b"IHDR":
        fail(f"{location} is not a PNG with an IHDR header")
    width = int.from_bytes(header[16:20], "big")
    height = int.from_bytes(header[20:24], "big")
    if (width, height) != (expected_width, expected_height):
        fail(
            f"{location} dimensions are {width}x{height}; "
            f"schema requires {expected_width}x{expected_height}"
        )


def read_description(channel: str, path: Path, tokens: dict[str, str]) -> str:
    try:
        raw = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        fail(f"cannot read {channel} description: {exc}")
    text = render(raw, tokens, f"{channel} description")
    if not text.strip():
        fail(f"{channel} description is empty")
    if PLACEHOLDER_RE.search(text):
        fail(f"{channel} description contains a placeholder value")
    if channel == "alternativeto" and (URL_RE.search(text) or EMAIL_RE.search(text)):
        fail("AlternativeTo description must not contain URLs or email addresses")
    return text


def validate_download(download: Any, location: str) -> dict[str, str]:
    if not isinstance(download, dict):
        fail(f"{location} must be an object")
    url = require_string(download, "url", location)
    if not url.lower().startswith("https://"):
        fail(f"{location}.url must use HTTPS")
    filename = safe_filename(download.get("filename"), f"{location}.filename")
    digest = require_string(download, "sha256", location).lower()
    if not SHA256_RE.fullmatch(digest):
        fail(f"{location}.sha256 must be a 64-character hexadecimal digest")
    label = require_string(download, "label", location)
    kind = require_string(download, "kind", location)
    return {
        "url": url,
        "filename": filename,
        "sha256": digest,
        "label": label,
        "kind": kind,
    }


def validate_observed_release(value: Any, location: str) -> dict[str, str]:
    """Validate the release the schema's recorded observations were made against."""
    if not isinstance(value, dict) or set(value) != {"version", "tag", "commit"}:
        fail(f"{location} must contain exactly version, tag, and commit")
    version = value["version"]
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        fail(f"{location}.version must be a semantic version")
    if value["tag"] != f"v{version}":
        fail(f"{location}.tag must be v{version}")
    if not isinstance(value["commit"], str) or not COMMIT_RE.fullmatch(value["commit"]):
        fail(f"{location}.commit must be a 40-character lowercase commit SHA")
    return dict(value)


def validate_submission(
    data: dict[str, Any], source: Path, root: Path, tokens: dict[str, str]
) -> tuple[str, str, str]:
    channel = data.get("channel")
    if channel not in CHANNELS:
        fail(f"{source}: channel must be one of {', '.join(CHANNELS)}")
    if data.get("schema_version") != 1:
        fail(f"{source}: schema_version must be 1")
    if "release" in data:
        fail(
            f"{source}: release is derived from the verified context; record observations in observed_release"
        )
    data["observed_release"] = validate_observed_release(
        data.get("observed_release"), f"{source}.observed_release"
    )
    fields = data.get("fields")
    if not isinstance(fields, dict) or not fields:
        fail(f"{source}: fields must be a non-empty object")
    description_rel = data.get("description_file")
    description = repo_path(description_rel, f"{source}.description_file", root)
    if description.suffix.lower() != ".txt":
        fail(f"{source}: description_file must point to a .txt file")
    description_text = read_description(channel, description, tokens)
    media = data.get("media")
    if not isinstance(media, list) or not media:
        fail(f"{source}: media must be a non-empty array")
    media_names: set[str] = set()
    for index, item in enumerate(media):
        location = f"{source}.media[{index}]"
        if not isinstance(item, dict):
            fail(f"{location} must be an object")
        media_source = repo_path(item.get("source"), f"{location}.source", root)
        filename = safe_filename(item.get("filename"), f"{location}.filename")
        if filename in media_names:
            fail(f"{source}: duplicate media filename {filename!r}")
        media_names.add(filename)
        if media_source.suffix.lower() != ".png":
            fail(f"{location}.source must be a PNG")
        validate_png(media_source, item.get("width"), item.get("height"), location)
        for key in ("role", "caption", "alt"):
            require_string(item, key, location)
    downloads = data.get("downloads")
    if not isinstance(downloads, list):
        fail(f"{source}: downloads must be an array")
    download_names: set[str] = set()
    normalized_downloads: list[dict[str, str]] = []
    for index, item in enumerate(downloads):
        normalized = validate_download(item, f"{source}.downloads[{index}]")
        if normalized["filename"] in download_names:
            fail(f"{source}: duplicate download filename {normalized['filename']!r}")
        download_names.add(normalized["filename"])
        normalized_downloads.append(normalized)
    submission = data.get("submission")
    if not isinstance(submission, dict):
        fail(f"{source}: submission must be an object")
    require_string(submission, "entry_url", f"{source}.submission")
    steps = submission.get("steps")
    if (
        not isinstance(steps, list)
        or not steps
        or not all(isinstance(step, str) and step.strip() for step in steps)
    ):
        fail(f"{source}: submission.steps must contain complete English actions")
    if submission.get("target_status") not in {
        "existing",
        "new-listing",
        "proposed-project",
    }:
        fail(f"{source}: submission.target_status is invalid")
    for key in ("evidence_urls", "limitations"):
        values = data.get(key)
        if not isinstance(values, list) or not all(
            isinstance(item, str) and item.strip() for item in values
        ):
            fail(f"{source}: {key} must be a list of non-empty strings")
    if channel == "alternativeto":
        require_string(data, "name", str(source))
        require_string(data, "short_summary", str(source))
        relation = data.get("relation")
        if not isinstance(relation, dict):
            fail(f"{source}: relation must be an object")
        require_string(relation, "rationale", f"{source}.relation")
        fee = data.get("optional_fee")
        if not isinstance(fee, dict) or not isinstance(fee.get("chosen"), bool):
            fail(f"{source}: optional_fee.chosen must be a boolean")
        if fee["chosen"]:
            fail(f"{source}: AlternativeTo paid priority submission is not authorized")
    data["_normalized_downloads"] = normalized_downloads
    return channel, description_rel, description_text


def template_root(context: ReleaseContext) -> tuple[Path, str]:
    """Prefer the tagged source tree's submission templates; fall back to the installed copies."""
    source_dir = Path(context.source_dir).resolve()
    tagged = source_dir / SCHEMA_DIR
    present = [(tagged / f"{channel}.json").is_file() for channel in CHANNELS]
    if all(present):
        return source_dir, TEMPLATE_TAGGED
    if any(present):
        fail(f"tagged source has an incomplete {SCHEMA_DIR.as_posix()} template set")
    return ROOT, TEMPLATE_INSTALLED


def release_tokens(context: ReleaseContext) -> dict[str, str]:
    """Validate the context identity and return the schema rendering tokens."""
    if not isinstance(context.repository, str) or not REPOSITORY_RE.fullmatch(
        context.repository
    ):
        fail(f"context repository is not owner/name: {context.repository!r}")
    if not isinstance(context.version, str) or not VERSION_RE.fullmatch(
        context.version
    ):
        fail(f"context version is not a semantic version: {context.version!r}")
    if context.tag != f"v{context.version}":
        fail(f"context tag {context.tag!r} does not match version {context.version!r}")
    if not isinstance(context.commit, str) or not COMMIT_RE.fullmatch(context.commit):
        fail("context commit must be a 40-character lowercase commit SHA")
    if not isinstance(context.sha256, str) or not SHA256_RE.fullmatch(context.sha256):
        fail("context sha256 must be a 64-character hexadecimal digest")
    if context.source_url != asset_url(
        context.repository, context.tag, context.version
    ):
        fail(
            f"context source_url is not the GitHub release asset for {context.tag}: {context.source_url!r}"
        )
    return {
        "repository": context.repository,
        "tag": context.tag,
        "version": context.version,
        "commit": context.commit,
        "source_url": context.source_url,
        "source_sha256": context.sha256.lower(),
        "source_filename": asset_name(context.version),
    }


def read_context_archive(context: ReleaseContext) -> bytes:
    archive = Path(context.archive)
    if archive.is_symlink() or not archive.is_file():
        fail(f"context archive is not a regular file: {archive}")
    if archive.stat().st_size > MAX_DOWNLOAD_BYTES:
        fail(f"context archive is larger than {MAX_DOWNLOAD_BYTES} bytes: {archive}")
    try:
        payload = archive.read_bytes()
    except OSError as exc:
        fail(f"cannot read context archive {archive}: {exc}")
    actual = sha256_bytes(payload)
    if actual != context.sha256.lower():
        fail(
            f"SHA256 mismatch for {archive}: context records {context.sha256}, archive is {actual}"
        )
    return payload


def download_verified(url: str, expected_sha256: str) -> bytes:
    request = urllib.request.Request(
        url, headers={"User-Agent": "Krema-submission-kit/1.0"}
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            content_length = response.headers.get("Content-Length")
            if content_length and int(content_length) > MAX_DOWNLOAD_BYTES:
                fail(f"download is larger than {MAX_DOWNLOAD_BYTES} bytes: {url}")
            chunks: list[bytes] = []
            total = 0
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > MAX_DOWNLOAD_BYTES:
                    fail(f"download is larger than {MAX_DOWNLOAD_BYTES} bytes: {url}")
                chunks.append(chunk)
    except (OSError, urllib.error.URLError, ValueError) as exc:
        fail(f"cannot download {url}: {exc}")
    payload = b"".join(chunks)
    actual = sha256_bytes(payload)
    if actual != expected_sha256.lower():
        fail(f"SHA256 mismatch for {url}: expected {expected_sha256}, got {actual}")
    return payload


def validate_source_archive(payload: bytes, version: str, commit: str) -> None:
    """Check archive safety with the shared validator, then bind its content to the release."""
    comment, members = read_archive(payload, version)
    if comment != commit:
        fail(f"source archive records commit {comment!r}; context commit is {commit}")
    entries = {rel: info for info, rel in members}

    def resolved(rel: str) -> tarfile.TarInfo | None:
        info = entries.get(rel)
        if info is not None and info.issym():
            info = entries.get(
                posixpath.normpath(
                    posixpath.join(posixpath.dirname(rel), info.linkname)
                )
            )
        return info

    for required in ("CMakeLists.txt", "LICENSE"):
        info = resolved(required)
        if info is None or not info.isreg():
            fail(f"source archive is missing a readable {required} file")
    if not any(
        rel.startswith("src/") and info.isreg() for rel, info in entries.items()
    ):
        fail("source archive is missing the src/ files")
    licenses = sorted(
        (rel, info)
        for rel, info in entries.items()
        if rel.startswith("LICENSES/") and info.isreg()
    )
    if not licenses:
        fail("source archive is missing the LICENSES/ files")
    license_target = resolved("LICENSE")
    try:
        with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:

            def member_bytes(info: tarfile.TarInfo) -> bytes:
                handle = archive.extractfile(info.name)
                return handle.read() if handle else b""

            for label, info in (("LICENSE", license_target), *licenses):
                text = member_bytes(info)
                if not text.strip() or text.startswith(LFS_PREFIX):
                    fail(
                        f"source archive {label} ({info.name}) is empty or an unhydrated Git LFS pointer"
                    )
            cmake = member_bytes(resolved("CMakeLists.txt"))[: 1024 * 1024].decode(
                "utf-8"
            )
    except (tarfile.TarError, OSError, KeyError, UnicodeError) as exc:
        fail(f"source archive license or CMakeLists.txt is not readable: {exc}")
    match = CMAKE_VERSION_RE.search(cmake)
    if not match:
        fail("source archive CMakeLists.txt has no project(krema VERSION x.y.z)")
    if match.group(1) != version:
        fail(
            f"source archive CMakeLists.txt declares version {match.group(1)}; context is {version}"
        )


def localize_step(step: str, data: dict[str, Any]) -> str:
    localized = step
    description_rel = data["description_file"]
    localized = localized.replace(description_rel, "description.txt")
    localized = localized.replace(Path(description_rel).name, "description.txt")
    for item in data["media"]:
        filename = item["filename"]
        localized = re.sub(
            rf"(?<![A-Za-z0-9_./-]){re.escape(filename)}(?![A-Za-z0-9_./-])",
            f"media/{filename}",
            localized,
        )
    for item in data["_normalized_downloads"]:
        filename = item["filename"]
        localized = re.sub(
            rf"(?<![A-Za-z0-9_./-]){re.escape(filename)}(?![A-Za-z0-9_./-])",
            f"downloads/{filename}",
            localized,
        )
    return localized


def upload_notes(data: dict[str, Any], description_rel: str) -> str:
    submission = data["submission"]
    release = data["release"]
    observed = data["observed_release"]
    lines = [
        f"Channel: {data['channel']}",
        f"Target release: {release['tag']} ({release['commit']})",
        f"Recorded channel observations made at: {observed['tag']} ({observed['commit']})",
        (
            f"Templates: packaging/submissions from the {release['tag']} source tree"
            if data["_template_source"] == TEMPLATE_TAGGED
            else f"Templates: installed fallback, because the {release['tag']} source tree has no packaging/submissions. "
            f"Only the version, source file, SHA-256, and README links are bound to {release['tag']}; "
            "a person must confirm the description and feature claims against this release before submitting."
        ),
        f"Target status: {submission['target_status']}",
        f"Entry URL: {submission['entry_url']}",
        f"Repository description source (provenance only): {description_rel}",
        "The files named below are present in this channel packet.",
        "Use description.txt, media/, and downloads/ only as directed by the channel steps; Launchpad is verification-only.",
        "",
        "Handoff steps:",
    ]
    lines.extend(
        f"{index}. {localize_step(step, data)}"
        for index, step in enumerate(submission["steps"], 1)
    )
    lines.extend(["", "Final field values:"])
    for key, value in data["fields"].items():
        rendered = json.dumps(value, ensure_ascii=False, separators=(",", ": "))
        lines.append(f"- {key}: {rendered}")
    for key in ("name", "short_summary"):
        if key in data:
            lines.append(f"- {key}: {data[key]}")
    if data["channel"] == "alternativeto":
        lines.append(f"- relation.rationale: {data['relation']['rationale']}")
        lines.append(
            f"- optional_fee.chosen: {json.dumps(data['optional_fee']['chosen'])}"
        )
    if data.get("media"):
        lines.extend(["", "Media mapping:"])
        for item in data["media"]:
            lines.extend(
                [
                    f"- media/{item['filename']} ({item['role']}, {item['width']}x{item['height']})",
                    f"  Caption: {item['caption']}",
                    f"  Alt text: {item['alt']}",
                ]
            )
    if data.get("_normalized_downloads"):
        lines.extend(["", "Downloads:"])
        for item in data["_normalized_downloads"]:
            lines.append(
                f"- downloads/{item['filename']} [{item['label']}] ({item['kind']}) sha256={item['sha256']}"
            )
    lines.extend(["", "Evidence URLs:"])
    lines.extend(f"- {url}" for url in data["evidence_urls"])
    lines.extend(["", "Limitations:"])
    lines.extend(f"- {item}" for item in data["limitations"])
    return "\n".join(lines) + "\n"


def copy_bytes(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as handle:
        handle.write(payload)


def json_bytes(value: Any) -> bytes:
    return (
        json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    ).encode("utf-8")


def iter_files(root: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_file():
            yield path


def write_checksums(output: Path) -> None:
    lines: list[str] = []
    for path in iter_files(output):
        if path.name == "SHA256SUMS":
            continue
        digest = sha256_bytes(path.read_bytes())
        lines.append(f"{digest}  {path.relative_to(output).as_posix()}")
    (output / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")


def make_zip(output: Path, zip_path: Path) -> None:
    with zipfile.ZipFile(zip_path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in iter_files(output):
            archive.write(path, arcname=Path(output.name) / path.relative_to(output))


def release_identity(context: ReleaseContext) -> dict[str, str]:
    return {
        "repository": context.repository,
        "tag": context.tag,
        "version": context.version,
        "commit": context.commit,
    }


def load_submissions(
    context: ReleaseContext,
) -> tuple[dict[str, tuple[dict[str, Any], str, str]], str, Path]:
    """Read, render, and validate every channel schema for the context's release.

    Returns the submissions, the template source label, and the template root.
    """
    tokens = release_tokens(context)
    release = release_identity(context)
    root, template_source = template_root(context)
    submissions: dict[str, tuple[dict[str, Any], str, str]] = {}
    for expected in CHANNELS:
        schema_path = repo_path(
            (SCHEMA_DIR / f"{expected}.json").as_posix(), f"{expected} schema", root
        )
        data = render(read_json(schema_path), tokens, str(schema_path))
        channel, description_rel, description_text = validate_submission(
            data, schema_path, root, tokens
        )
        if channel != expected:
            fail(f"{schema_path}: channel must be {expected}")
        data["release"] = release
        data["_template_source"] = template_source
        submissions[channel] = (data, description_rel, description_text)
    baselines = {
        json.dumps(data["observed_release"], sort_keys=True)
        for data, _, _ in submissions.values()
    }
    if len(baselines) != 1:
        fail("channel schemas disagree on observed_release")
    return submissions, template_source, root


def build_kit(context: ReleaseContext, output: Path) -> dict[str, Any]:
    """Validate the context and schemas, then write the kit directory, ZIP, and checksums."""
    output = output.expanduser().resolve()
    for protected in (ROOT, Path(context.source_dir).resolve()):
        try:
            output.relative_to(protected)
        except ValueError:
            continue
        fail(
            f"output must be outside the repository and the release source tree: {output}"
        )
    if output.exists():
        if not output.is_dir():
            fail(f"output exists and is not a directory: {output}")
        if any(output.iterdir()):
            fail(f"refusing to use non-empty output directory: {output}")
    zip_path = output.with_name(output.name + ".zip")
    if zip_path.exists():
        fail(f"refusing to overwrite existing ZIP: {zip_path}")

    tokens = release_tokens(context)
    release = release_identity(context)
    source_payload = read_context_archive(context)
    validate_source_archive(source_payload, context.version, context.commit)
    submissions, template_source, root = load_submissions(context)

    source_entries: list[tuple[str, dict[str, str]]] = []
    for channel, (data, _, _) in submissions.items():
        for item in data["_normalized_downloads"]:
            if (
                item["url"] == context.source_url
                and item["sha256"] != tokens["source_sha256"]
            ):
                fail(
                    f"{channel}: every {context.tag} source URL reference must use the context SHA-256"
                )
            if item["kind"].lower() == "source-archive":
                source_entries.append((channel, item))
    if len(source_entries) != 1 or source_entries[0][0] != "kde-store":
        fail("exactly one KDE Store source download is required")
    _, source = source_entries[0]
    if (
        source["url"] != context.source_url
        or source["sha256"] != tokens["source_sha256"]
    ):
        fail(
            f"KDE Store source download does not match the verified {context.tag} source"
        )
    if source["filename"] != tokens["source_filename"]:
        fail(f"KDE Store source download filename must be {tokens['source_filename']}")
    if (
        "source" not in source["label"].lower()
        or context.version not in source["label"]
    ):
        fail(
            f"KDE Store source download label must identify {context.version} source code"
        )
    downloads: dict[str, bytes] = {context.source_url: source_payload}
    for data, _, _ in submissions.values():
        for item in data["_normalized_downloads"]:
            if item["url"] not in downloads:
                downloads[item["url"]] = download_verified(item["url"], item["sha256"])

    output.mkdir(parents=True, exist_ok=True)
    for channel, (data, description_rel, description_text) in submissions.items():
        channel_dir = output / channel
        media_dir = channel_dir / "media"
        downloads_dir = channel_dir / "downloads"
        media_dir.mkdir(parents=True)
        downloads_dir.mkdir()
        (channel_dir / "description.txt").write_text(description_text, encoding="utf-8")
        (channel_dir / "fields.json").write_bytes(json_bytes(data["fields"]))
        (channel_dir / "upload-order.txt").write_text(
            upload_notes(data, description_rel), encoding="utf-8"
        )
        (channel_dir / "submission.json").write_bytes(
            json_bytes(
                {key: value for key, value in data.items() if not key.startswith("_")}
            )
        )
        for item in data["media"]:
            source_path = repo_path(item["source"], f"{channel}.media source", root)
            shutil.copyfile(source_path, media_dir / item["filename"])
        for item in data["_normalized_downloads"]:
            copy_bytes(downloads_dir / item["filename"], downloads[item["url"]])

    kit_info = {
        "release": release,
        "schema_baseline": submissions["kde-store"][0]["observed_release"],
        "templates": {
            "source": template_source,
            "human_review_required": template_source == TEMPLATE_INSTALLED,
        },
        "source": {
            "url": context.source_url,
            "sha256": tokens["source_sha256"],
            "filename": source["filename"],
            "label": source["label"],
        },
        "channels": list(CHANNELS),
        "publication_state": "prepared; no external submission performed",
    }
    (output / "kit.json").write_bytes(json_bytes(kit_info))
    write_checksums(output)
    make_zip(output, zip_path)
    return {
        "output": output,
        "zip": zip_path,
        "checksums": output / "SHA256SUMS",
        "kit": kit_info,
        "submissions": {channel: data for channel, (data, _, _) in submissions.items()},
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--context",
        required=True,
        type=Path,
        help="verified context.json from release.py prepare",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="new kit directory outside the repository",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(sys.argv[1:] if argv is None else argv)
        built = build_kit(load_context(args.context), args.output)
        print(f"Prepared {built['output']}")
        print(f"ZIP {built['zip']}")
        print(f"Checksums {built['checksums']}")
        return 0
    except ReleaseError as exc:
        print(f"prepare_submission_kit.py: error: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("prepare_submission_kit.py: interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
