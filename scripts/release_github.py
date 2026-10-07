#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""GitHub release channel: the release for the tag plus immutable source assets.

The release carries ``krema-X.Y.Z.tar.gz`` (the context archive) and
``SHA256SUMS``. Existing assets are never replaced: an asset with the same
name and the same bytes counts as published, different bytes fail the
channel. An existing public release's title and notes are left untouched.

A new release is created as a private draft bound to the context commit,
with the notes re-verified against the tagged source immediately before.
The remote annotated tag object is read before the draft is created and
again right before the draft is published; any change, or a peeled commit
other than the context commit, leaves the draft unpublished and fails the
channel. GitHub has no compare-and-swap on a tag object, so a tag moved in
the short window between that final check and publication cannot be
excluded here; it is detected by the post-publication check and reported.
A draft this tooling did not create (different notes or target) blocks the
channel and is never edited or deleted.

Uses the authenticated ``gh`` CLI already configured on the machine.
"""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path
from typing import Any

from release_common import (
    ReleaseContext,
    ReleaseError,
    asset_name,
    command,
    expected_notes,
    remote_tag_oids,
    result,
    sha256_bytes,
    sha256_file,
    sha256sums_text,
)

CHANNEL = "github"
SUMS_NAME = "SHA256SUMS"
API_TIMEOUT = 60
UPLOAD_TIMEOUT = 900


def _gh_json(args: list[str], *, timeout: int = API_TIMEOUT) -> Any:
    out = command(["gh", *args], timeout=timeout)
    try:
        return json.loads(out) if out.strip() else None
    except ValueError:
        raise ReleaseError("gh returned invalid JSON") from None


def find_releases(repository: str, tag: str) -> list[dict]:
    """All releases (drafts included) whose tag is ``tag``."""
    out = command(
        [
            "gh",
            "api",
            "--paginate",
            "-H",
            "Accept: application/vnd.github+json",
            f"repos/{repository}/releases?per_page=100",
            "--jq",
            f'.[] | select(.tag_name == "{tag}")',
        ],
        timeout=API_TIMEOUT,
    )
    releases = []
    for line in out.splitlines():
        if line.strip():
            try:
                releases.append(json.loads(line))
            except ValueError:
                raise ReleaseError("gh returned invalid release JSON") from None
    return releases


def download_asset(repository: str, tag: str, name: str) -> bytes:
    """Bytes of a release asset, downloaded to a private temporary directory."""
    with tempfile.TemporaryDirectory(prefix="krema-asset-") as tmp:
        command(
            [
                "gh",
                "release",
                "download",
                tag,
                "--repo",
                repository,
                "--pattern",
                name,
                "--dir",
                tmp,
            ],
            timeout=UPLOAD_TIMEOUT,
        )
        path = Path(tmp) / name
        if path.is_symlink() or not path.is_file():
            raise ReleaseError(f"downloaded asset {name} is missing")
        return path.read_bytes()


def remote_asset_sha256(repository: str, tag: str, asset: dict) -> str:
    """SHA-256 of an uploaded asset: GitHub's digest, else the downloaded bytes."""
    digest = asset.get("digest")
    if isinstance(digest, str) and digest.startswith("sha256:"):
        return digest.removeprefix("sha256:").lower()
    return sha256_bytes(download_asset(repository, tag, asset["name"]))


def classify_asset(
    expected_sha256: str, remote: dict | None, remote_sha256: str | None
) -> str:
    """``missing``, ``match``, ``mismatch`` or ``incomplete`` for one asset.

    ``remote`` is the GitHub asset object (None when absent) and
    ``remote_sha256`` its content hash. A partially uploaded asset
    (``state`` other than ``uploaded``) is ``incomplete``; it is never
    overwritten automatically.
    """
    if remote is None:
        return "missing"
    if remote.get("state", "uploaded") != "uploaded":
        return "incomplete"
    return "match" if remote_sha256 == expected_sha256 else "mismatch"


def expected_assets(context: ReleaseContext) -> dict[str, str]:
    sums = sha256sums_text(context.sha256, context.version).encode()
    return {asset_name(context.version): context.sha256, SUMS_NAME: sha256_bytes(sums)}


def remote_tag_object(context: ReleaseContext) -> str:
    """The remote annotated tag object id, after checking it peels to the context commit."""
    refs = remote_tag_oids(context.repository, context.tag)
    obj = refs.get(f"refs/tags/{context.tag}")
    peeled = refs.get(f"refs/tags/{context.tag}^{{}}")
    if obj is None:
        raise ReleaseError(f"tag {context.tag} is not on {context.repository}")
    if peeled != context.commit:
        raise ReleaseError(
            f"remote tag {context.tag} resolves to {peeled or obj}, context has {context.commit}"
        )
    return obj


def inspect(context: ReleaseContext) -> dict:
    """Read-only comparison of GitHub with the context.

    Returns {"state": "missing"|"draft"|"ambiguous"|"present", "id": <id>,
    "release": <url or None>, "assets": {name: classification}} and, for a
    draft, "ours": whether its notes and target are exactly what this
    context would create.
    """
    releases = find_releases(context.repository, context.tag)
    if not releases:
        return {"state": "missing", "id": None, "release": None, "assets": {}}
    if len(releases) > 1:
        return {"state": "ambiguous", "id": None, "release": None, "assets": {}}
    release = releases[0]
    remote = {asset.get("name"): asset for asset in release.get("assets", [])}
    assets: dict[str, str] = {}
    for name, wanted in expected_assets(context).items():
        asset = remote.get(name)
        remote_sha = None
        if asset is not None and asset.get("state", "uploaded") == "uploaded":
            remote_sha = remote_asset_sha256(context.repository, context.tag, asset)
        assets[name] = classify_asset(wanted, asset, remote_sha)
    out = {
        "state": "draft" if release.get("draft") else "present",
        "id": release.get("id"),
        "release": release.get("html_url"),
        "assets": assets,
    }
    if release.get("draft"):
        out["ours"] = (
            release.get("target_commitish") == context.commit
            and release.get("body") == expected_notes(context).decode()
        )
    return out


def _report(context: ReleaseContext, found: dict, *, planning: bool) -> dict:
    state, assets, url = found["state"], found["assets"], found["release"]
    extra = {"release_url": url, "assets": assets, "version": context.version}
    if state == "ambiguous":
        return result(
            CHANNEL, "failed", f"more than one release uses tag {context.tag}", **extra
        )
    if state == "draft" and not found["ours"]:
        return result(
            CHANNEL,
            "blocked",
            f"a draft release for {context.tag} with other notes or target exists; publish or delete it by hand",
            **extra,
        )
    if state == "missing":
        verb = "would create" if planning else "not created:"
        return result(
            CHANNEL,
            "planned",
            f"{verb} release {context.tag} with {', '.join(expected_assets(context))}",
            **extra,
        )
    bad = sorted(
        name for name, kind in assets.items() if kind in ("mismatch", "incomplete")
    )
    if bad:
        return result(
            CHANNEL,
            "failed",
            f"existing assets differ from the context and are never overwritten: {', '.join(bad)}",
            **extra,
        )
    missing = sorted(name for name, kind in assets.items() if kind == "missing")
    if state == "draft":
        todo = f"upload {', '.join(missing)} and " if missing else ""
        verb = "would resume" if planning else "unpublished:"
        return result(
            CHANNEL,
            "planned",
            f"{verb} draft release {context.tag}: {todo}publish",
            **extra,
        )
    if missing:
        verb = "would upload" if planning else "missing assets:"
        return result(CHANNEL, "planned", f"{verb} {', '.join(missing)}", **extra)
    return result(
        CHANNEL, "already_published", "release and assets match the context", **extra
    )


def status(context: ReleaseContext) -> dict:
    return _report(context, inspect(context), planning=False)


def _sums_file(context: ReleaseContext) -> Path:
    """Create ``github-upload/SHA256SUMS`` atomically, never clobbering.

    The bytes are written to a private temporary file and hard-linked into
    place, so an interruption never leaves a truncated file and an existing
    file (or a dangling symlink) is never followed or replaced.
    """
    upload_dir = context.work_dir / "github-upload"
    if upload_dir.is_symlink():
        raise ReleaseError(f"{upload_dir} is a symlink")
    upload_dir.mkdir(mode=0o755, exist_ok=True)
    path = upload_dir / SUMS_NAME
    content = sha256sums_text(context.sha256, context.version).encode()
    if not os.path.lexists(path):
        fd, tmp = tempfile.mkstemp(prefix=f".{SUMS_NAME}.", dir=upload_dir)
        try:
            with os.fdopen(fd, "wb") as handle:
                handle.write(content)
                handle.flush()
                os.fsync(handle.fileno())
            os.chmod(tmp, 0o644)
            try:
                os.link(tmp, path)
            except FileExistsError:
                pass
        finally:
            os.unlink(tmp)
    if path.is_symlink() or not path.is_file():
        raise ReleaseError(f"{path} is not a regular file")
    if path.read_bytes() != content:
        raise ReleaseError(f"{path} exists with different content")
    return path


def _require_tag(context: ReleaseContext, expected: str) -> None:
    if remote_tag_object(context) != expected:
        raise ReleaseError(f"remote tag {context.tag} moved during publication")


def publish(context: ReleaseContext, *, execute: bool = False) -> dict:
    found = inspect(context)
    report = _report(context, found, planning=True)
    if not execute or report["status"] != "planned":
        return report

    draft_left = found["state"] == "draft"
    try:
        tag_object = remote_tag_object(context)
        if context.notes_path.read_bytes() != expected_notes(context):
            return result(CHANNEL, "failed", "notes changed after context validation")
        if sha256_file(context.archive) != context.sha256:
            return result(CHANNEL, "failed", "archive changed after context validation")
        files = {
            asset_name(context.version): context.archive,
            SUMS_NAME: _sums_file(context),
        }

        if found["state"] == "present":
            todo = [
                str(files[name])
                for name, kind in found["assets"].items()
                if kind == "missing"
            ]
            command(
                [
                    "gh",
                    "release",
                    "upload",
                    context.tag,
                    *todo,
                    "--repo",
                    context.repository,
                ],
                timeout=UPLOAD_TIMEOUT,
            )
        else:
            if found["state"] == "missing":
                command(
                    [
                        "gh",
                        "release",
                        "create",
                        context.tag,
                        "--repo",
                        context.repository,
                        "--draft",
                        "--verify-tag",
                        "--target",
                        context.commit,
                        "--title",
                        context.tag,
                        "--notes-file",
                        str(context.notes_path),
                    ],
                    timeout=UPLOAD_TIMEOUT,
                )
                draft_left = True
                found = inspect(context)
                if found["state"] != "draft" or not found.get("ours"):
                    raise ReleaseError(
                        f"the created draft for {context.tag} could not be verified as the only matching draft"
                    )
            todo = [
                str(files[name])
                for name, kind in found["assets"].items()
                if kind == "missing"
            ]
            if todo:
                command(
                    [
                        "gh",
                        "release",
                        "upload",
                        context.tag,
                        *todo,
                        "--repo",
                        context.repository,
                    ],
                    timeout=UPLOAD_TIMEOUT,
                )
            found = inspect(context)
            if (
                found["state"] != "draft"
                or not found.get("ours")
                or set(found["assets"].values()) != {"match"}
            ):
                raise ReleaseError("draft assets or notes do not match the context")
            _require_tag(context, tag_object)
            command(
                [
                    "gh",
                    "api",
                    "--method",
                    "PATCH",
                    f"repos/{context.repository}/releases/{found['id']}",
                    "-F",
                    "draft=false",
                ],
                timeout=API_TIMEOUT,
            )
            draft_left = False
        _require_tag(context, tag_object)
    except ReleaseError as exc:
        detail = f"GitHub publication failed: {exc}"
        if draft_left:
            detail += "; the release remains an unpublished draft"
        return result(CHANNEL, "failed", detail, version=context.version)

    after = inspect(context)
    verified = _report(context, after, planning=False)
    if verified["status"] != "already_published":
        return result(
            CHANNEL,
            "failed",
            f"post-upload verification failed: {verified['detail']}",
            release_url=after["release"],
            assets=after["assets"],
            version=context.version,
        )
    return result(
        CHANNEL,
        "published",
        f"release {context.tag} carries verified {', '.join(files)}",
        release_url=after["release"],
        assets=after["assets"],
        version=context.version,
    )
