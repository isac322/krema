#!/usr/bin/env python3
"""OBS and COPR release adapters for a verified Krema ReleaseContext.

Both channels are read-only unless ``execute=True``. Every external write is
preceded by a fresh remote preflight so retries never duplicate a commit or a
build, and never downgrade a channel that already serves a newer version.

OBS (``home:<owner>/krema``):
  Package inputs are taken only from ``<source_dir>/packaging/obs`` of the
  verified tag. ``_service`` is pinned to the selected tag (``revision``).
  ``project.meta.xml``, ``project.config`` and ``apply-project-config.sh``
  configure the project and are never uploaded; remote files that are not
  package inputs are never removed. The tool is native ``osc`` or, when that is
  absent, ``uvx --from osc osc``; both use the existing osc configuration.
  ``osc commit --noservice`` skips only osc's local "trylocal" service run; the
  OBS server still runs ``_service`` on the committed sources.

COPR (``<owner>/krema``):
  The existing SCM package is re-pointed at the selected tag (only the
  committish changes; clone URL, subdirectory, spec and build method must
  already match and are re-verified afterwards), then one build is dispatched
  with ``copr-cli build-package --nowait``. ``--webhook-rebuild``,
  ``--max-builds``, ``--timeout`` and ``--chroot-denylist`` are deliberately
  omitted: copr-cli sends them as null, the frontend drops null fields and only
  updates those settings when present, so existing values are preserved.
  Chroots are never edited. Only builds whose submitted SCM committish equals
  the tag (verified via ``/api_3/build/source-build-config/<id>``) count toward
  a decision, so builds of other refs neither satisfy nor block this release.
  A dispatch is reported as ``submitted``; only a succeeded build of the exact
  version is reported as published.
"""

from __future__ import annotations

import hashlib
import re
import shutil
import tempfile
import urllib.parse
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from release_common import ReleaseContext, ReleaseError, command, http_json, result

PACKAGE = "krema"

OBS_API_TIMEOUT = 300  # first ``uvx`` use resolves osc before the request
OBS_CHECKOUT_TIMEOUT = 600
OBS_COMMIT_TIMEOUT = 900
OBS_PACKAGE_FILES = (
    "_service",
    "krema.spec",
    "krema.dsc",
    "debian.changelog",
    "debian.control",
    "debian.rules",
    "debian.copyright",
)
# Project configuration, never package sources.
OBS_PROJECT_FILES = frozenset(
    {"project.meta.xml", "project.config", "apply-project-config.sh"}
)

COPR_API = "https://copr.fedorainfracloud.org/api_3"
COPR_TIMEOUT = 300
COPR_BUILD_PAGE = 100
# Build states from the COPR API that are still in progress.
COPR_ACTIVE_STATES = frozenset(
    {"importing", "pending", "starting", "running", "waiting"}
)
COPR_SUCCESS_STATES = frozenset({"succeeded", "forked"})
COPR_FAILURE_STATES = frozenset({"failed", "canceled", "skipped"})

OBS_FAILED_CODES = frozenset({"failed", "unresolvable", "broken"})
OBS_IGNORED_CODES = frozenset({"disabled", "excluded", "locked"})

SEMVER_RE = re.compile(r"(\d+)\.(\d+)\.(\d+)")
TAG_RE = re.compile(r"v(\d+\.\d+\.\d+)")
CREATED_BUILD_RE = re.compile(r"Created builds?:\s*([\d\s]+)")


class _Blocked(ReleaseError):
    """A prerequisite (tool, credential, safe remote state) is missing."""


# ---------------------------------------------------------------------------
# Versions
# ---------------------------------------------------------------------------


def version_key(version: str) -> tuple[int, int, int]:
    """'0.10.0' -> (0, 10, 0); reject anything that is not X.Y.Z."""
    match = SEMVER_RE.fullmatch(version.strip())
    if match is None:
        raise ReleaseError(f"not a semantic version: {version!r}")
    return (int(match[1]), int(match[2]), int(match[3]))


def tag_version(ref: str | None) -> str | None:
    """'v0.10.0' -> '0.10.0'; None for branches and other refs."""
    if not ref:
        return None
    match = TAG_RE.fullmatch(ref.strip())
    return match[1] if match else None


def _owner(ctx: ReleaseContext) -> str:
    owner, sep, name = ctx.repository.partition("/")
    if not sep or not owner or not name or "/" in name:
        raise ReleaseError(f"repository must be owner/name: {ctx.repository!r}")
    return owner


def _clone_url(ctx: ReleaseContext) -> str:
    return f"https://github.com/{ctx.repository}.git"


def _check_context(ctx: ReleaseContext) -> None:
    if tag_version(ctx.tag) != ctx.version:
        raise ReleaseError(f"tag {ctx.tag!r} does not match version {ctx.version!r}")
    version_key(ctx.version)


# ---------------------------------------------------------------------------
# OBS package inputs (pure)
# ---------------------------------------------------------------------------


def pin_service_revision(text: str, tag: str, clone_url: str) -> str:
    """Return ``_service`` with the tar_scm ``revision`` pinned to ``tag``.

    The text is edited in place (formatting preserved) and must contain exactly
    one tar_scm service fetching ``clone_url`` with exactly one revision param.
    """
    try:
        root = ET.fromstring(text)
    except ET.ParseError as e:
        raise ReleaseError(f"_service is not valid XML: {e}") from None
    scm = [s for s in root.findall("service") if s.get("name") == "tar_scm"]
    if len(scm) != 1:
        raise ReleaseError(f"_service must have one tar_scm service, found {len(scm)}")
    params = {}
    for param in scm[0].findall("param"):
        name = param.get("name", "")
        if name in params:
            raise ReleaseError(f"_service tar_scm repeats param {name!r}")
        params[name] = (param.text or "").strip()
    if params.get("url") != clone_url:
        raise ReleaseError(
            f"_service tar_scm url {params.get('url')!r} is not {clone_url!r}"
        )
    if "revision" not in params:
        raise ReleaseError("_service tar_scm has no revision param")

    pattern = re.compile(r'(<param\s+name="revision"\s*>)[^<]*(</param>)')
    pinned, count = pattern.subn(lambda m: f"{m[1]}{tag}{m[2]}", text)
    if count != 1:
        raise ReleaseError(f"_service has {count} revision params, expected 1")
    if service_revision(pinned) != tag:
        raise ReleaseError("failed to pin _service revision")
    return pinned


def service_revision(text: str) -> str | None:
    """tar_scm revision of a ``_service`` document, or None."""
    try:
        root = ET.fromstring(text)
    except ET.ParseError:
        return None
    for service in root.findall("service"):
        if service.get("name") != "tar_scm":
            continue
        for param in service.findall("param"):
            if param.get("name") == "revision":
                return (param.text or "").strip()
    return None


def spec_version(text: str) -> str:
    versions = re.findall(r"^Version:\s*(\S+)\s*$", text, re.MULTILINE)
    if len(versions) != 1:
        raise ReleaseError(
            f"krema.spec must declare one Version, found {len(versions)}"
        )
    return versions[0]


def _single(pattern: str, text: str, what: str) -> str:
    found = re.findall(pattern, text, re.MULTILINE)
    if len(found) != 1:
        raise ReleaseError(f"{what}: expected one match, found {len(found)}")
    return found[0]


def build_obs_inputs(
    source_dir: Path,
    *,
    tag: str,
    version: str,
    debian_revision: str,
    clone_url: str,
) -> dict[str, bytes]:
    """Package inputs for OBS from the tagged source tree.

    Only ``OBS_PACKAGE_FILES`` are returned; project configuration is never
    included. The tagged packaging must already declare ``version`` (spec,
    dsc, Debian changelog); disagreement is an error, never a silent rewrite.
    """
    obs_dir = source_dir / "packaging" / "obs"
    inputs: dict[str, bytes] = {}
    for name in OBS_PACKAGE_FILES:
        path = obs_dir / name
        if path.is_symlink() or not path.is_file():
            raise ReleaseError(f"tagged source lacks regular file packaging/obs/{name}")
        inputs[name] = path.read_bytes()
    try:
        texts = {name: data.decode("utf-8") for name, data in inputs.items()}
    except UnicodeDecodeError:
        raise ReleaseError("tagged OBS packaging is not UTF-8") from None

    if spec_version(texts["krema.spec"]) != version:
        raise ReleaseError(f"tagged krema.spec Version is not {version}")
    full = f"{version}-{debian_revision}"
    dsc = texts["krema.dsc"]
    if _single(r"^Version:\s*(\S+)\s*$", dsc, "krema.dsc Version") != full:
        raise ReleaseError(f"tagged krema.dsc Version is not {full}")
    tarball = _single(
        r"^DEBTRANSFORM-TAR:\s*(\S+)\s*$", dsc, "krema.dsc DEBTRANSFORM-TAR"
    )
    if tarball != f"{PACKAGE}-{version}.tar.gz":
        raise ReleaseError(f"tagged krema.dsc DEBTRANSFORM-TAR is {tarball!r}")
    top = re.match(r"krema \(([^)]+)\)", texts["debian.changelog"])
    if top is None or top[1] != full:
        raise ReleaseError(f"tagged debian.changelog top entry is not {full}")

    inputs["_service"] = pin_service_revision(texts["_service"], tag, clone_url).encode(
        "utf-8"
    )
    return inputs


def md5_map(inputs: dict[str, bytes]) -> dict[str, str]:
    return {name: hashlib.md5(data).hexdigest() for name, data in inputs.items()}


# ---------------------------------------------------------------------------
# OBS remote state (parsing is pure)
# ---------------------------------------------------------------------------


@dataclass
class ObsDirectory:
    rev: str | None
    entries: dict[str, str]  # name -> md5
    service_code: str | None
    service_error: str | None


def parse_obs_directory(text: str) -> ObsDirectory:
    try:
        root = ET.fromstring(text)
    except ET.ParseError as e:
        raise ReleaseError(f"unexpected OBS directory listing: {e}") from None
    if root.tag != "directory":
        raise ReleaseError("unexpected OBS directory listing root")
    info = root.find("serviceinfo")
    return ObsDirectory(
        rev=root.get("rev"),
        entries={e.get("name", ""): e.get("md5", "") for e in root.findall("entry")},
        service_code=info.get("code") if info is not None else None,
        service_error=info.get("error") if info is not None else None,
    )


@dataclass(frozen=True)
class ObsResult:
    repository: str
    arch: str
    state: str
    dirty: bool
    code: str


def parse_obs_results(text: str, package: str = PACKAGE) -> list[ObsResult]:
    try:
        root = ET.fromstring(text)
    except ET.ParseError as e:
        raise ReleaseError(f"unexpected OBS result list: {e}") from None
    out = []
    for res in root.findall("result"):
        for status in res.findall("status"):
            if status.get("package") != package:
                continue
            out.append(
                ObsResult(
                    repository=res.get("repository", ""),
                    arch=res.get("arch", ""),
                    state=res.get("state", ""),
                    dirty=res.get("dirty") == "true",
                    code=status.get("code", ""),
                )
            )
    return out


def classify_obs_results(results: list[ObsResult]) -> tuple[str, str]:
    """Return (published|pending|failed, detail) for the current sources.

    Published means every enabled repository/arch succeeded and its repository
    is published and not dirty. Any failure wins over pending work.
    """
    active = [r for r in results if r.code not in OBS_IGNORED_CODES]
    failed = [r for r in active if r.code in OBS_FAILED_CODES]
    if failed:
        names = ", ".join(f"{r.repository}/{r.arch}={r.code}" for r in failed)
        return "failed", f"OBS builds failed: {names}"
    if not active:
        return "pending", "OBS reports no enabled build results yet"
    waiting = [
        r for r in active if r.code != "succeeded" or r.state != "published" or r.dirty
    ]
    if waiting:
        names = ", ".join(
            f"{r.repository}/{r.arch}={r.code}/{r.state}{'*' if r.dirty else ''}"
            for r in waiting
        )
        return (
            "pending",
            f"OBS builds in progress ({len(waiting)}/{len(active)}): {names}",
        )
    return "published", f"OBS built and published {len(active)} repository/arch targets"


@dataclass
class ObsPlan:
    status: str
    detail: str
    differing: list[str] = field(default_factory=list)


def decide_obs(
    *,
    version: str,
    tag: str,
    staged: dict[str, str],
    directory: ObsDirectory,
    remote_version: str | None,
    remote_revision: str | None,
    results: list[ObsResult],
) -> ObsPlan:
    """Pure OBS decision from staged inputs and the remote package state."""
    target = version_key(version)
    if remote_version is not None:
        try:
            remote_key = version_key(remote_version)
        except ReleaseError:
            remote_key = None
        if remote_key is not None and remote_key > target:
            return ObsPlan(
                "blocked",
                f"OBS already serves newer {remote_version}; refusing to downgrade to {version}",
            )
    pinned = tag_version(remote_revision)
    if pinned is not None and version_key(pinned) > target:
        return ObsPlan(
            "blocked",
            f"OBS _service is pinned to newer {remote_revision}; refusing {tag}",
        )

    differing = sorted(
        n for n, md5 in staged.items() if directory.entries.get(n) != md5
    )
    if differing:
        same = remote_version == version
        why = (
            f"remote already has {version} sources but differs in"
            if same
            else f"remote has {remote_version or 'no'} sources; {tag} changes"
        )
        return ObsPlan("planned", f"{why}: {', '.join(differing)}", differing)

    if directory.service_code == "failed":
        return ObsPlan(
            "failed",
            f"OBS source service failed: {directory.service_error or 'no detail'}",
        )
    if directory.service_code not in (None, "succeeded"):
        return ObsPlan("pending", f"OBS source service is {directory.service_code}")
    status, detail = classify_obs_results(results)
    return ObsPlan(status, detail)


# ---------------------------------------------------------------------------
# OBS tool access
# ---------------------------------------------------------------------------


def osc_argv() -> list[str]:
    """Native osc, else ``uvx --from osc osc``; both use the existing oscrc."""
    if shutil.which("osc"):
        return ["osc"]
    if shutil.which("uvx"):
        return ["uvx", "--from", "osc", "osc"]
    raise _Blocked("neither osc nor uvx is available")


def _obs_project(ctx: ReleaseContext) -> str:
    return f"home:{_owner(ctx)}"


def _obs_url(ctx: ReleaseContext) -> str:
    return f"https://build.opensuse.org/package/show/{_obs_project(ctx)}/{PACKAGE}"


def _osc_api(osc: list[str], path: str) -> str:
    return command([*osc, "api", path], timeout=OBS_API_TIMEOUT)


def _osc_file(osc: list[str], project: str, name: str) -> str | None:
    try:
        return _osc_api(osc, f"/source/{project}/{PACKAGE}/{name}")
    except ReleaseError:
        return None


@dataclass
class ObsRemote:
    directory: ObsDirectory
    version: str | None
    revision: str | None
    results: list[ObsResult]


def _obs_remote(osc: list[str], project: str) -> ObsRemote:
    directory = parse_obs_directory(_osc_api(osc, f"/source/{project}/{PACKAGE}"))
    spec = (
        _osc_file(osc, project, "krema.spec")
        if "krema.spec" in directory.entries
        else None
    )
    service = (
        _osc_file(osc, project, "_service") if "_service" in directory.entries else None
    )
    version = None
    if spec is not None:
        try:
            version = spec_version(spec)
        except ReleaseError:
            version = None
    results = parse_obs_results(
        _osc_api(osc, f"/build/{project}/_result?package={PACKAGE}")
    )
    return ObsRemote(
        directory=directory,
        version=version,
        revision=service_revision(service) if service is not None else None,
        results=results,
    )


def _obs_inputs(ctx: ReleaseContext) -> dict[str, bytes]:
    return build_obs_inputs(
        ctx.source_dir,
        tag=ctx.tag,
        version=ctx.version,
        debian_revision=ctx.debian_revision,
        clone_url=_clone_url(ctx),
    )


def _obs_evaluate(ctx: ReleaseContext, osc: list[str]):
    inputs = _obs_inputs(ctx)
    remote = _obs_remote(osc, _obs_project(ctx))
    plan = decide_obs(
        version=ctx.version,
        tag=ctx.tag,
        staged=md5_map(inputs),
        directory=remote.directory,
        remote_version=remote.version,
        remote_revision=remote.revision,
        results=remote.results,
    )
    return inputs, remote, plan


def _obs_extra(ctx: ReleaseContext, remote: ObsRemote, plan: ObsPlan) -> dict[str, Any]:
    extras = sorted(
        n
        for n in remote.directory.entries
        if n not in OBS_PACKAGE_FILES and not n.startswith("_service:")
    )
    return {
        "project": _obs_project(ctx),
        "package": PACKAGE,
        "url": _obs_url(ctx),
        "remote_rev": remote.directory.rev,
        "remote_version": remote.version,
        "remote_revision": remote.revision,
        "differing_files": plan.differing,
        "untouched_remote_files": extras,
    }


def _scratch(ctx: ReleaseContext, prefix: str) -> Path:
    parent = ctx.work_dir / "rpm"
    parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=prefix, dir=parent))


def _obs_commit(
    ctx: ReleaseContext, osc: list[str], inputs: dict[str, bytes], remote: ObsRemote
) -> None:
    """Checkout into scratch, overwrite package inputs only, add new ones, commit.

    Remote files that are not package inputs are left in place (no addremove).
    """
    project = _obs_project(ctx)
    scratch = _scratch(ctx, "obs-")
    try:
        checkout = scratch / "checkout"
        command(
            [*osc, "checkout", "--output-dir", str(checkout), project, PACKAGE],
            cwd=scratch,
            timeout=OBS_CHECKOUT_TIMEOUT,
        )
        if not (checkout / ".osc").is_dir():
            raise ReleaseError("osc checkout did not produce a working copy")
        for name, data in inputs.items():
            (checkout / name).write_bytes(data)
        new = [n for n in inputs if n not in remote.directory.entries]
        if new:
            command([*osc, "add", *new], cwd=checkout, timeout=OBS_API_TIMEOUT)
        command(
            [*osc, "commit", "--noservice", "-m", f"Update to {ctx.tag}"],
            cwd=checkout,
            timeout=OBS_COMMIT_TIMEOUT,
        )
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def _obs(ctx: ReleaseContext, *, execute: bool, publishing: bool) -> dict:
    try:
        _check_context(ctx)
        osc = osc_argv()
        inputs, remote, plan = _obs_evaluate(ctx, osc)
        extra = _obs_extra(ctx, remote, plan)
        if plan.status != "planned":
            status = plan.status
            if publishing and status == "published":
                status = "already_published"
            return result("obs", status, plan.detail, **extra)
        if not execute:
            return result(
                "obs",
                "planned",
                f"{plan.detail}; would commit {ctx.tag} package inputs to {extra['project']}/{PACKAGE}",
                **extra,
            )

        _obs_commit(ctx, osc, inputs, remote)
        after = _obs_remote(osc, _obs_project(ctx))
        staged = md5_map(inputs)
        mismatch = sorted(
            n for n, md5 in staged.items() if after.directory.entries.get(n) != md5
        )
        extra = _obs_extra(ctx, after, ObsPlan("submitted", "", mismatch))
        if mismatch:
            return result(
                "obs",
                "failed",
                f"OBS commit did not land expected sources: {', '.join(mismatch)}",
                **extra,
            )
        return result(
            "obs",
            "submitted",
            f"committed {ctx.tag} sources as rev {after.directory.rev}; builds not yet verified",
            **extra,
        )
    except _Blocked as e:
        return result("obs", "blocked", str(e))
    except ReleaseError as e:
        return result("obs", "failed", str(e))


def publish_obs(context: ReleaseContext, *, execute: bool = False) -> dict:
    return _obs(context, execute=execute, publishing=True)


def status_obs(context: ReleaseContext) -> dict:
    return _obs(context, execute=False, publishing=False)


# ---------------------------------------------------------------------------
# COPR (decision is pure)
# ---------------------------------------------------------------------------


def build_version(build: dict) -> str | None:
    """Upstream version of a COPR build ('0.10.0-1' -> '0.10.0')."""
    source = build.get("source_package") or {}
    full = source.get("version")
    if not full:
        return None
    return str(full).rsplit("-", 1)[0]


def build_is_ours(config: dict | None, tag: str, clone_url: str) -> bool:
    """True when a build's submitted SCM source points at ``tag`` of our repo.

    ``config`` is the public ``/api_3/build/source-build-config/<id>`` payload:
    the identity recorded at submission. A build queued from another branch or
    tag must not count as a build of this release.
    """
    if not isinstance(config, dict) or config.get("source_type") != "scm":
        return False
    source = config.get("source_dict") or {}
    return (
        source.get("type") == "git"
        and source.get("clone_url") == clone_url
        and source.get("subdirectory") == "packaging/obs"
        and source.get("spec") == "krema.spec"
        and source.get("committish") == tag
    )


def _needs_identity(build: dict, version: str) -> bool:
    """Builds whose outcome can affect this release's decision."""
    return build.get("state") in COPR_ACTIVE_STATES or build_version(build) in (
        None,
        version,
    )


@dataclass
class CoprPlan:
    status: str
    detail: str
    build_ids: list[int] = field(default_factory=list)
    needs_edit: bool = False


def decide_copr(
    *,
    version: str,
    tag: str,
    package: dict,
    builds: list[dict],
    configs: dict[int, dict],
    clone_url: str,
) -> CoprPlan:
    """Pure COPR decision from the package definition and its builds.

    ``configs`` maps build id -> ``source-build-config`` for every build that
    ``_needs_identity`` flags. Only builds whose submitted committish equals
    ``tag`` count for or against this release; other builds are ignored.
    """
    target = version_key(version)
    if package.get("source_type") != "scm":
        return CoprPlan(
            "blocked", f"COPR package is {package.get('source_type')!r}, not scm"
        )
    source = package.get("source_dict") or {}
    expected = {
        "type": "git",
        "clone_url": clone_url,
        "subdirectory": "packaging/obs",
        "spec": "krema.spec",
    }
    wrong = sorted(k for k, v in expected.items() if source.get(k) != v)
    if wrong:
        return CoprPlan(
            "blocked",
            f"COPR SCM source differs in {', '.join(wrong)}; refusing unrelated package edits",
        )

    ordered = sorted(builds, key=lambda b: int(b.get("id", 0)), reverse=True)
    known_newer = []
    for b in ordered:
        if b.get("state") not in COPR_SUCCESS_STATES | COPR_ACTIVE_STATES:
            continue
        bid = int(b.get("id", 0))
        v = build_version(b)
        # An in-flight build pinned to a newer tag is also a downgrade guard.
        if v is None and _needs_identity(b, version):
            v = tag_version(
                ((configs.get(bid) or {}).get("source_dict") or {}).get("committish")
            )
        if v is None:
            continue
        try:
            if version_key(v) > target:
                known_newer.append(f"#{bid}={v}")
        except ReleaseError:
            continue
    if known_newer:
        return CoprPlan(
            "blocked",
            f"COPR already has newer builds ({', '.join(known_newer)}); refusing to downgrade to {version}",
        )
    pinned = tag_version(source.get("committish"))
    if pinned is not None and version_key(pinned) > target:
        return CoprPlan(
            "blocked",
            f"COPR package is pinned to newer {source.get('committish')}; refusing {tag}",
        )

    ours = [
        b
        for b in ordered
        if _needs_identity(b, version)
        and build_is_ours(configs.get(int(b.get("id", 0))), tag, clone_url)
    ]
    published = [
        b
        for b in ours
        if b.get("state") in COPR_SUCCESS_STATES and build_version(b) == version
    ]
    if published:
        return CoprPlan(
            "published",
            f"COPR build #{published[0]['id']} of {tag} ({version}) succeeded",
            [int(published[0]["id"])],
        )
    active = [b for b in ours if b.get("state") in COPR_ACTIVE_STATES]
    if active:
        return CoprPlan(
            "pending",
            f"COPR build(s) of {tag} in progress: "
            + ", ".join(f"#{b['id']}={b.get('state')}" for b in active),
            [int(b["id"]) for b in active],
        )
    # Ours but succeeded without producing the target version (e.g. unknown
    # SRPM version): honest failure, since a terminal different-known-version
    # build never reaches here — it is filtered out and already guarded above.
    wrong_version = [b for b in ours if b.get("state") in COPR_SUCCESS_STATES]
    failed = [b for b in ours if b.get("state") in COPR_FAILURE_STATES]
    problems = wrong_version + failed
    if problems:
        b = problems[0]
        produced = build_version(b)
        if b.get("state") in COPR_SUCCESS_STATES:
            detail = (
                f"COPR build #{b['id']} pinned to {tag} produced "
                f"{produced or 'an unknown version'}, expected {version}"
            )
        else:
            detail = f"latest COPR build #{b['id']} of {tag} is {b.get('state')}"
        return CoprPlan(
            "failed", detail, [int(b["id"])], needs_edit=source.get("committish") != tag
        )
    return CoprPlan(
        "planned",
        f"no COPR build of {tag} exists"
        + (
            f"; committish {source.get('committish')!r} -> {tag}"
            if source.get("committish") != tag
            else ""
        ),
        needs_edit=source.get("committish") != tag,
    )


def _copr_names(ctx: ReleaseContext) -> tuple[str, str]:
    return _owner(ctx), ctx.repository.split("/", 1)[1]


def _copr_package(owner: str, project: str) -> dict:
    query = urllib.parse.urlencode(
        {"ownername": owner, "projectname": project, "packagename": PACKAGE}
    )
    data = http_json(f"{COPR_API}/package?{query}")
    if not isinstance(data, dict) or data.get("name") != PACKAGE:
        raise ReleaseError("unexpected COPR package response")
    return data


def _copr_builds(owner: str, project: str) -> list[dict]:
    query = urllib.parse.urlencode(
        {
            "ownername": owner,
            "projectname": project,
            "packagename": PACKAGE,
            "limit": COPR_BUILD_PAGE,
            "order": "id",
            "order_type": "DESC",
        }
    )
    data = http_json(f"{COPR_API}/build/list?{query}")
    items = data.get("items") if isinstance(data, dict) else None
    if not isinstance(items, list):
        raise ReleaseError("unexpected COPR build list response")
    return items


def _copr_build_config(cache: dict[int, dict], build_id: int) -> dict:
    """One ``source-build-config`` fetch per build id per run; no retries."""
    if build_id in cache:
        return cache[build_id]
    data = http_json(f"{COPR_API}/build/source-build-config/{build_id}")
    if not isinstance(data, dict):
        raise ReleaseError(f"could not read source config of COPR build #{build_id}")
    cache[build_id] = data
    return data


def _copr_evaluate(ctx: ReleaseContext) -> tuple[dict, CoprPlan]:
    owner, project = _copr_names(ctx)
    package = _copr_package(owner, project)
    builds = _copr_builds(owner, project)
    cache: dict[int, dict] = {}
    configs = {
        int(b["id"]): _copr_build_config(cache, int(b["id"]))
        for b in builds
        if _needs_identity(b, ctx.version) and b.get("id") is not None
    }
    plan = decide_copr(
        version=ctx.version,
        tag=ctx.tag,
        package=package,
        builds=builds,
        configs=configs,
        clone_url=_clone_url(ctx),
    )
    return package, plan


def _copr_extra(ctx: ReleaseContext, package: dict, plan: CoprPlan) -> dict[str, Any]:
    owner, project = _copr_names(ctx)
    return {
        "project": f"{owner}/{project}",
        "package": PACKAGE,
        "url": f"https://copr.fedorainfracloud.org/coprs/{owner}/{project}/builds/",
        "committish": (package.get("source_dict") or {}).get("committish"),
        "build_ids": plan.build_ids,
    }


def _copr_dispatch(ctx: ReleaseContext, package: dict, plan: CoprPlan) -> list[int]:
    owner, project = _copr_names(ctx)
    copr = f"{owner}/{project}"
    if not shutil.which("copr-cli"):
        raise _Blocked("copr-cli is not available")
    try:
        command(["copr-cli", "whoami"], timeout=COPR_TIMEOUT)
    except ReleaseError as e:
        raise _Blocked(f"copr-cli is not authenticated: {e}") from None

    if plan.needs_edit:
        source = package["source_dict"]
        before_rebuild = bool(package.get("auto_rebuild"))
        command(
            [
                "copr-cli",
                "edit-package-scm",
                copr,
                "--name",
                PACKAGE,
                "--clone-url",
                source["clone_url"],
                "--commit",
                ctx.tag,
                "--subdir",
                source["subdirectory"],
                "--spec",
                source["spec"],
                "--type",
                source["type"],
                "--method",
                source.get("source_build_method") or "rpkg",
            ],
            timeout=COPR_TIMEOUT,
        )
        after = _copr_package(owner, project)
        expected = dict(source, committish=ctx.tag)
        if (
            after.get("source_dict") != expected
            or bool(after.get("auto_rebuild")) != before_rebuild
        ):
            raise ReleaseError(
                "COPR package source after edit does not match the pinned tag"
            )

    # Re-check immediately before dispatch so a concurrent run never duplicates.
    _, fresh = _copr_evaluate(ctx)
    if fresh.status in ("published", "pending", "blocked"):
        raise _Skip(fresh)
    out = command(
        ["copr-cli", "build-package", copr, "--name", PACKAGE, "--nowait"],
        timeout=COPR_TIMEOUT,
    )
    match = CREATED_BUILD_RE.search(out)
    if match is None:
        raise ReleaseError("copr-cli build-package did not report a build id")
    return [int(x) for x in match[1].split()]


class _Skip(Exception):
    def __init__(self, plan: CoprPlan) -> None:
        super().__init__(plan.detail)
        self.plan = plan


def _copr(ctx: ReleaseContext, *, execute: bool, publishing: bool) -> dict:
    try:
        _check_context(ctx)
        package, plan = _copr_evaluate(ctx)
        extra = _copr_extra(ctx, package, plan)
        retryable = plan.status == "failed" and publishing
        if plan.status != "planned" and not retryable:
            status = plan.status
            if publishing and status == "published":
                status = "already_published"
            return result("copr", status, plan.detail, **extra)
        if not execute:
            if not publishing:
                return result("copr", plan.status, plan.detail, **extra)
            return result(
                "copr",
                "planned",
                f"{plan.detail}; would build {ctx.tag} in {extra['project']}",
                **extra,
            )
        try:
            ids = _copr_dispatch(ctx, package, plan)
        except _Skip as skip:
            status = (
                "already_published"
                if skip.plan.status == "published"
                else skip.plan.status
            )
            return result(
                "copr",
                status,
                skip.plan.detail,
                **_copr_extra(ctx, package, skip.plan),
            )
        extra["build_ids"] = ids
        extra["committish"] = ctx.tag
        return result(
            "copr",
            "submitted",
            f"dispatched COPR build(s) {', '.join(f'#{i}' for i in ids)} for {ctx.tag}; completion not yet verified",
            **extra,
        )
    except _Blocked as e:
        return result("copr", "blocked", str(e))
    except ReleaseError as e:
        return result("copr", "failed", str(e))


def publish_copr(context: ReleaseContext, *, execute: bool = False) -> dict:
    return _copr(context, execute=execute, publishing=True)


def status_copr(context: ReleaseContext) -> dict:
    return _copr(context, execute=False, publishing=False)


# ---------------------------------------------------------------------------
# Local preparation only (no external access)
# ---------------------------------------------------------------------------


def prepare_obs(context: ReleaseContext, output: Path) -> dict:
    """Write the exact OBS package inputs ``publish_obs`` would commit.

    No network, osc or credentials are used. ``output`` must not exist yet.
    The same validated tagged spec is what the COPR SCM build consumes.
    """
    try:
        _check_context(context)
        staged = _obs_inputs(context)
        output.mkdir(parents=True, exist_ok=False)
        for name, data in staged.items():
            (output / name).write_bytes(data)
        return result(
            "obs",
            "planned",
            f"prepared {len(staged)} {context.tag} package inputs (not uploaded)",
            output=str(output),
            files=rpm_md5_listing(staged),
        )
    except FileExistsError:
        return result("obs", "failed", f"output already exists: {output}")
    except ReleaseError as e:
        return result("obs", "failed", str(e))


def rpm_md5_listing(staged: dict[str, bytes]) -> dict[str, str]:
    return dict(sorted(md5_map(staged).items()))


def main(argv: list[str] | None = None) -> int:
    import argparse
    import json

    from release_common import load_context

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    prep = sub.add_parser("prepare-obs", help="stage OBS inputs locally; no upload")
    prep.add_argument("--context", type=Path, required=True)
    prep.add_argument("--output", type=Path, required=True)
    for name in ("status-obs", "status-copr"):
        sub.add_parser(name, help="read-only remote status").add_argument(
            "--context", type=Path, required=True
        )
    args = parser.parse_args(argv)
    ctx = load_context(args.context)
    if args.cmd == "prepare-obs":
        out = prepare_obs(ctx, args.output)
    elif args.cmd == "status-obs":
        out = status_obs(ctx)
    else:
        out = status_copr(ctx)
    print(json.dumps(out, indent=2, sort_keys=True))
    return 1 if out.get("status") in ("failed", "blocked") else 0


if __name__ == "__main__":
    raise SystemExit(main())
