#!/usr/bin/env python3
"""AUR channel adapter for the krema release tooling.

prepare() pins the tagged packaging/arch/PKGBUILD to the release context
(version, immutable source URL, SHA-256, pkgrel=1) in an output directory and
regenerates .SRCINFO with makepkg (native, or an Arch container when makepkg
is missing). It never contacts AUR.

publish() without execute is a read-only plan: it renders the PKGBUILD in
memory, reads the public AUR clone over HTTPS into a temporary scratch
directory and compares versions; makepkg/docker are not run. With
execute=True the AUR repository is cloned over SSH (BatchMode, strict host
key checking, the existing agent/key), prepare() output is committed in the
scratch clone and pushed without force. An AUR pkgver equal to or newer than
the context is never rewritten, so reruns cannot create duplicate commits or
downgrades.

Standalone use is read-only (the main CLI is scripts/release.py):
    python3 scripts/release_aur.py status  --context DIR/context.json
    python3 scripts/release_aur.py prepare --context DIR/context.json --output EMPTY_DIR
    python3 scripts/release_aur.py publish --context DIR/context.json
The standalone publish action only prints the plan; the guarded publication
path is `python3 scripts/release.py publish --channel aur --execute`.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path

from release_common import ReleaseContext, ReleaseError, command, load_context, result

CHANNEL = "aur"
PKGNAME = "krema"
AUR_HTTPS_URL = f"https://aur.archlinux.org/{PKGNAME}.git"
AUR_SSH_URL = f"aur@aur.archlinux.org:{PKGNAME}.git"
AUR_BRANCH = "master"
PKGBUILD_PATH = Path("packaging/arch/PKGBUILD")

# makepkg is absent on macOS; Arch only publishes amd64 images.
ARCH_IMAGE = os.environ.get("KREMA_AUR_IMAGE", "docker.io/archlinux:base-devel")
ARCH_PLATFORM = os.environ.get("KREMA_AUR_PLATFORM", "linux/amd64")

GIT_TIMEOUT = 120
SRCINFO_TIMEOUT = 900  # covers a first-time image pull

SSH_COMMAND = (
    "ssh -o BatchMode=yes -o StrictHostKeyChecking=yes "
    "-o ConnectTimeout=20 -o ServerAliveInterval=15 -o ServerAliveCountMax=2"
)

_VERSION_RE = re.compile(r"^\d+(?:\.\d+)*$")
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_UNSAFE_URL_RE = re.compile(r"[\s\"'`$\\]")


# ---------------------------------------------------------------------------
# Pure helpers
# ---------------------------------------------------------------------------


def version_key(version: str) -> tuple[int, ...]:
    if not _VERSION_RE.match(version):
        raise ReleaseError(f"unsupported AUR pkgver format: {version!r}")
    return tuple(int(part) for part in version.split("."))


def _replace_one(text: str, pattern: str, replacement: str, what: str) -> str:
    new, count = re.subn(pattern, lambda _m: replacement, text, flags=re.MULTILINE)
    if count != 1:
        raise ReleaseError(
            f"tagged PKGBUILD must define {what} exactly once (found {count})"
        )
    return new


def render_pkgbuild(
    template: str, *, version: str, source_url: str, sha256: str
) -> str:
    """Pin a tagged PKGBUILD to a release; dependencies, arch and license stay as tagged.

    The tagged pkgver may lag (the PKGBUILD is historically bumped after the
    GitHub release), so the context version overriding it is the normal path.
    """
    version_key(version)
    if not _SHA256_RE.match(sha256):
        raise ReleaseError("context sha256 is not a lowercase 64-digit hex digest")
    if not source_url.startswith("https://") or _UNSAFE_URL_RE.search(source_url):
        raise ReleaseError("context source_url must be a plain https URL")
    if not re.search(rf"^pkgname={PKGNAME}$", template, flags=re.MULTILINE):
        raise ReleaseError(f"tagged PKGBUILD does not declare pkgname={PKGNAME}")
    text = _replace_one(template, r"^pkgver=.*$", f"pkgver={version}", "pkgver")
    text = _replace_one(text, r"^pkgrel=.*$", "pkgrel=1", "pkgrel")
    text = _replace_one(
        text,
        r"^source=\(.*\)$",
        f'source=("$pkgname-$pkgver.tar.gz::{source_url}")',
        "a single-line source array",
    )
    return _replace_one(
        text, r"^sha256sums=\(.*\)$", f"sha256sums=('{sha256}')", "sha256sums"
    )


def parse_srcinfo(text: str) -> dict[str, list[str]]:
    """Collect .SRCINFO `key = value` pairs (pkgbase section and pkgname sections merged)."""
    fields: dict[str, list[str]] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition(" = ")
        if not sep:
            raise ReleaseError(f"malformed .SRCINFO line: {line!r}")
        fields.setdefault(key, []).append(value)
    return fields


def _single(fields: Mapping[str, list[str]], key: str) -> str | None:
    values = fields.get(key)
    if not values:
        return None
    if len(values) != 1:
        raise ReleaseError(f".SRCINFO has {len(values)} {key} entries")
    return values[0]


def expected_source(context: ReleaseContext) -> str:
    return f"{PKGNAME}-{context.version}.tar.gz::{context.source_url}"


def validate_generated_srcinfo(srcinfo: str, context: ReleaseContext) -> None:
    fields = parse_srcinfo(srcinfo)
    checks = {
        "pkgbase": PKGNAME,
        "pkgver": context.version,
        "pkgrel": "1",
        "source": expected_source(context),
        "sha256sums": context.sha256,
    }
    for key, want in checks.items():
        got = _single(fields, key)
        if got != want:
            raise ReleaseError(
                f"generated .SRCINFO {key} is {got!r}, expected {want!r}"
            )


def classify_remote(
    remote_srcinfo: str | None, context: ReleaseContext
) -> tuple[str, str, dict]:
    """Decide what publishing the context means against the current AUR state.

    Returns (status, detail, extra) where status is planned, already_published
    or blocked. An equal pkgver is never rewritten, even with a different
    source, because AUR users already track that version.
    """
    if remote_srcinfo is None:
        return (
            "planned",
            f"AUR {PKGNAME} has no .SRCINFO; {context.version} would be the first upload",
            {"aur_version": None},
        )
    fields = parse_srcinfo(remote_srcinfo)
    remote_ver = _single(fields, "pkgver")
    remote_rel = _single(fields, "pkgrel")
    if remote_ver is None:
        raise ReleaseError("AUR .SRCINFO has no pkgver")
    extra: dict = {"aur_version": f"{remote_ver}-{remote_rel}"}
    remote_key, target_key = version_key(remote_ver), version_key(context.version)
    if remote_key > target_key:
        return (
            "blocked",
            f"AUR already has newer {remote_ver}; refusing downgrade to {context.version}",
            extra,
        )
    if remote_key == target_key:
        source_matches = (
            _single(fields, "source") == expected_source(context)
            and _single(fields, "sha256sums") == context.sha256
        )
        extra["source_matches"] = source_matches
        detail = f"AUR already publishes {remote_ver}-{remote_rel}"
        if not source_matches:
            detail += (
                " from a different source URL/hash than the context; left unchanged"
            )
        return "already_published", detail, extra
    return (
        "planned",
        f"AUR {remote_ver}-{remote_rel} would be updated to {context.version}-1",
        extra,
    )


# ---------------------------------------------------------------------------
# Side-effecting steps (scratch directories and the AUR remote only)
# ---------------------------------------------------------------------------


def _git_env() -> dict[str, str]:
    env = dict(os.environ)
    env.update(
        {"GIT_SSH_COMMAND": SSH_COMMAND, "GIT_TERMINAL_PROMPT": "0", "LC_ALL": "C"}
    )
    return env


def _git(args: list[str], cwd: Path | None = None, timeout: int = GIT_TIMEOUT) -> str:
    return command(["git", *args], cwd=cwd, env=_git_env(), timeout=timeout)


def _scratch(context: ReleaseContext) -> Path:
    context.work_dir.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix="aur-", dir=context.work_dir))


def clone_remote(url: str, dest: Path) -> str | None:
    """Clone the AUR repository; return the current .SRCINFO text, or None when absent."""
    _git(["clone", "--quiet", "--no-tags", "--branch", AUR_BRANCH, url, str(dest)])
    srcinfo = dest / ".SRCINFO"
    return srcinfo.read_text(encoding="utf-8") if srcinfo.is_file() else None


def tagged_pkgbuild(context: ReleaseContext) -> str:
    """Render the tagged PKGBUILD for the context (pure; validates the template)."""
    tagged = context.source_dir / PKGBUILD_PATH
    if not tagged.is_file():
        raise ReleaseError(f"tagged source has no {PKGBUILD_PATH}")
    return render_pkgbuild(
        tagged.read_text(encoding="utf-8"),
        version=context.version,
        source_url=context.source_url,
        sha256=context.sha256,
    )


def generate_srcinfo(stage_dir: Path) -> tuple[str, str]:
    """Run `makepkg --printsrcinfo`; return (.SRCINFO text, tool description)."""
    if shutil.which("makepkg"):
        return command(
            ["makepkg", "--printsrcinfo"], cwd=stage_dir, timeout=SRCINFO_TIMEOUT
        ), "makepkg"
    if not shutil.which("docker"):
        raise ReleaseError(
            "neither makepkg nor docker is available to generate .SRCINFO"
        )
    # makepkg refuses to run as root and requires a writable startdir/$BUILDDIR,
    # so the host stage stays read-only at /src and makepkg runs on a private
    # copy in the container's /tmp. The PKGBUILD is only parsed, never built.
    script = (
        "set -eu; mkdir /tmp/pkg; cp /src/PKGBUILD /tmp/pkg/PKGBUILD; "
        "cd /tmp/pkg; exec makepkg --printsrcinfo"
    )
    argv = [
        "docker", "run", "--rm", "--pull", "missing", "--network", "none",
        "--platform", ARCH_PLATFORM, "--user", "65534:65534", "--env", "HOME=/tmp",
        "--env", "BUILDDIR=/tmp/pkg", "--env", "SRCDEST=/tmp/pkg", "--env", "PKGDEST=/tmp/pkg",
        "--env", "SRCPKGDEST=/tmp/pkg", "--env", "LOGDEST=/tmp/pkg",
        "--volume", f"{stage_dir.resolve()}:/src:ro", "--workdir", "/tmp",
        ARCH_IMAGE, "sh", "-c", script,
    ]  # fmt: skip
    return command(
        argv, timeout=SRCINFO_TIMEOUT
    ), f"makepkg in {ARCH_IMAGE} ({ARCH_PLATFORM})"


def _commit_identity(clone: Path) -> tuple[str, str]:
    try:
        name = _git(["config", "--get", "user.name"], cwd=clone).strip()
        email = _git(["config", "--get", "user.email"], cwd=clone).strip()
    except ReleaseError as exc:
        raise ReleaseError(
            "git user.name/user.email must be configured for the AUR commit"
        ) from exc
    if not name or not email:
        raise ReleaseError(
            "git user.name/user.email must be configured for the AUR commit"
        )
    return name, email


def _push(clone: Path, context: ReleaseContext) -> str:
    name, email = _commit_identity(clone)
    _git(["add", "--", "PKGBUILD", ".SRCINFO"], cwd=clone)
    _git(
        ["-c", f"user.name={name}", "-c", f"user.email={email}", "commit", "--quiet",
         "-m", f"Update to {context.version}"],
        cwd=clone,
    )  # fmt: skip
    head = _git(["rev-parse", "HEAD"], cwd=clone).strip()
    # Plain push: a concurrent remote update makes it non-fast-forward and fail.
    _git(["push", "--quiet", "origin", f"HEAD:refs/heads/{AUR_BRANCH}"], cwd=clone)
    remote = _git(
        ["ls-remote", "origin", f"refs/heads/{AUR_BRANCH}"], cwd=clone
    ).split()
    if not remote or remote[0] != head:
        raise ReleaseError(
            f"AUR {AUR_BRANCH} does not point at pushed commit {head} after push"
        )
    return head


# ---------------------------------------------------------------------------
# Adapter API
# ---------------------------------------------------------------------------


def status(context: ReleaseContext) -> dict:
    """Read-only: compare the public AUR repository with the context."""
    scratch = _scratch(context)
    try:
        try:
            remote = clone_remote(AUR_HTTPS_URL, scratch / "aur")
        except ReleaseError as exc:
            return result(CHANNEL, "failed", f"cannot read {AUR_HTTPS_URL}: {exc}")
        try:
            state, detail, extra = classify_remote(remote, context)
        except ReleaseError as exc:
            return result(CHANNEL, "failed", f"cannot interpret AUR state: {exc}")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return result(CHANNEL, state, detail, version=context.version, **extra)


def prepare(context: ReleaseContext, output_dir: Path) -> dict:
    """Write the pinned PKGBUILD and makepkg-generated .SRCINFO to output_dir; no remote access."""
    if output_dir.exists() and any(output_dir.iterdir()):
        return result(CHANNEL, "blocked", f"prepare output {output_dir} is not empty")
    try:
        output_dir.mkdir(parents=True, exist_ok=True)
        (output_dir / "PKGBUILD").write_text(tagged_pkgbuild(context), encoding="utf-8")
        srcinfo, tool = generate_srcinfo(output_dir)
        validate_generated_srcinfo(srcinfo, context)
        (output_dir / ".SRCINFO").write_text(srcinfo, encoding="utf-8")
    except (ReleaseError, OSError) as exc:
        return result(
            CHANNEL,
            "blocked",
            f"cannot prepare PKGBUILD/.SRCINFO: {exc}",
            output=str(output_dir),
        )
    return result(
        CHANNEL, "planned", f"prepared AUR {context.version}-1 files (not uploaded)",
        version=context.version, output=str(output_dir), srcinfo_tool=tool,
    )  # fmt: skip


def publish(context: ReleaseContext, *, execute: bool = False) -> dict:
    """Plan (default, read-only) or perform the AUR update for the context's release.

    The plan renders the PKGBUILD in memory and reads the public AUR clone; it
    does not run makepkg/docker. execute=True clones over SSH, prepares the
    files with prepare(), commits in the scratch clone and pushes.
    """
    try:
        tagged_pkgbuild(context)
    except ReleaseError as exc:
        return result(CHANNEL, "blocked", f"cannot stage PKGBUILD: {exc}")

    scratch = _scratch(context)
    url = AUR_SSH_URL if execute else AUR_HTTPS_URL
    clone = scratch / "aur"
    try:
        remote = clone_remote(url, clone)
    except ReleaseError as exc:
        shutil.rmtree(scratch, ignore_errors=True)
        why = (
            "AUR SSH access unavailable (agent/key/known_hosts)"
            if execute
            else f"cannot read {url}"
        )
        return result(CHANNEL, "blocked" if execute else "failed", f"{why}: {exc}")

    try:
        state, detail, extra = classify_remote(remote, context)
    except ReleaseError as exc:
        shutil.rmtree(scratch, ignore_errors=True)
        return result(CHANNEL, "failed", f"cannot interpret AUR state: {exc}")
    extra["version"] = context.version
    if state != "planned" or not execute:
        shutil.rmtree(scratch, ignore_errors=True)
        if state == "planned":
            detail += " (dry run: PKGBUILD + regenerated .SRCINFO would be pushed)"
        return result(CHANNEL, state, detail, **extra)

    extra["scratch"] = str(scratch)
    prepared = prepare(context, scratch / "stage")
    if prepared["status"] != "planned":
        return prepared
    extra["srcinfo_tool"] = prepared["srcinfo_tool"]
    for name in ("PKGBUILD", ".SRCINFO"):
        shutil.copyfile(scratch / "stage" / name, clone / name)
    try:
        changed = _git(
            ["status", "--porcelain", "--", "PKGBUILD", ".SRCINFO"], cwd=clone
        ).rstrip("\n")
    except ReleaseError as exc:
        return result(
            CHANNEL, "failed", f"cannot diff scratch AUR clone: {exc}", **extra
        )
    if not changed:
        return result(
            CHANNEL,
            "failed",
            "staged files are identical to AUR yet pkgver differs",
            **extra,
        )
    extra["changed_files"] = [line[3:] for line in changed.splitlines()]
    try:
        commit = _push(clone, context)
    except ReleaseError as exc:
        return result(CHANNEL, "failed", f"AUR commit/push failed: {exc}", **extra)
    return result(
        CHANNEL,
        "published",
        f"AUR updated to {context.version}-1",
        commit=commit,
        **extra,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("action", choices=("status", "prepare", "publish"))
    parser.add_argument("--context", required=True, type=Path)
    parser.add_argument(
        "--output", type=Path, help="empty directory for prepare output"
    )
    args = parser.parse_args(argv)
    if (args.output is None) == (args.action == "prepare"):
        parser.error("--output is required for prepare and only valid there")
    try:
        context = load_context(args.context)
    except ReleaseError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    if args.action == "status":
        report = status(context)
    elif args.action == "prepare":
        report = prepare(context, args.output)
    else:
        report = publish(context)
    print(json.dumps(report, indent=2, sort_keys=True))
    return (
        0
        if report.get("status") in {"planned", "already_published", "published"}
        else 1
    )


if __name__ == "__main__":
    sys.exit(main())
