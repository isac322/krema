#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Portable Krema release CLI.

  prepare  verify a signed vX.Y.Z tag and write a release context (source
           tree, krema-X.Y.Z.tar.gz, notes.md, context.json) outside the repo
  gate     inspect the full Distro E2E matrix for the tagged commit
  status   read-only per-channel publication report
  publish  plan (default) or, with --execute, publish to channels

Every subcommand prints exactly one JSON object on stdout. Exit codes: 0 ok,
1 a channel or the gate failed, 2 the request itself was rejected.

Nothing is written outside --output / the context's work_dir unless
--execute is given. publish --execute requires a tag signed by a pinned key,
the same tag object on GitHub, the tagged commit reachable from a freshly
fetched default branch, and a fully green Distro E2E run for that commit.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_common import (  # noqa: E402
    REPOSITORY_RE,
    CommandError,
    ReleaseContext,
    ReleaseError,
    archive_manifest,
    asset_name,
    asset_url,
    check_source_versions,
    command,
    compare_manifests,
    context_to_json,
    expected_notes,
    extract_archive,
    gzip_deterministic,
    load_context,
    parse_tag,
    redact,
    remote_tag_oids,
    remote_url,
    result,
    run_process,
    sha256_bytes,
    source_root_name,
)

DEFAULT_REPOSITORY = "isac322/krema"
PINNED_SIGNERS = ("AC48F8F7708DDBA12D959C1FCF1251E9D42EF5B9",)
CHANNELS = ("github", "aur", "obs", "copr", "ppa")
GATE_WORKFLOW = "distro-e2e.yml"
GATE_JOB_RE = re.compile(r"^Distro E2E \((?P<target>[^)]+)\)$")
TARGETS_FILE = "tests/distro/targets.tsv"
TARGET_ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")
EXTRA_TARGETS = ("arch",)
DISPATCH_MARKER = "gate-dispatch.json"
DISPATCH_VISIBLE_WITHIN = 3600  # seconds until a dispatched run must be listed
FINGERPRINT_RE = re.compile(r"^[0-9A-F]{40}$")
DONE = {"published", "already_published"}


# ---------------------------------------------------------------------------
# git and trust checks
# ---------------------------------------------------------------------------


def git(root: Path, *args: str, timeout: int = 120) -> str:
    return command(["git", "-C", str(root), *args], timeout=timeout).strip()


def resolve_repo_root(value: str | None) -> Path:
    root = Path(value or os.getcwd()).resolve()
    try:
        git(root, "rev-parse", "--git-dir")
    except ReleaseError:
        raise ReleaseError(
            f"{root} is not a git repository; pass --repo-root"
        ) from None
    return root


def normalize_signers(values: list[str] | None) -> tuple[str, ...]:
    signers = tuple(v.replace(" ", "").upper() for v in (values or PINNED_SIGNERS))
    for fpr in signers:
        if not FINGERPRINT_RE.match(fpr):
            raise ReleaseError(
                f"signer {fpr!r} is not a 40-hex-digit OpenPGP fingerprint"
            )
    return signers


def tag_identity(root: Path, tag: str) -> tuple[str, str]:
    """(tag object id, commit id) for an annotated tag named ``tag``."""
    try:
        obj = git(root, "rev-parse", "--verify", "--quiet", f"refs/tags/{tag}")
    except CommandError:
        raise ReleaseError(f"tag {tag} does not exist in {root}") from None
    if git(root, "cat-file", "-t", obj) != "tag":
        raise ReleaseError(
            f"tag {tag} is lightweight; releases need a signed annotated tag"
        )
    header = git(root, "cat-file", "tag", obj).split("\n\n", 1)[0]
    if f"\ntag {tag}\n" not in f"\n{header}\n":
        raise ReleaseError(f"tag object {obj} is not named {tag}")
    commit = git(root, "rev-parse", "--verify", f"{obj}^{{commit}}")
    return obj, commit


def parse_gpg_status(status: str, signers: tuple[str, ...]) -> str:
    """Return the matching pinned fingerprint from ``[GNUPG:]`` status lines."""
    lines = [
        line.split() for line in status.splitlines() if line.startswith("[GNUPG:] ")
    ]
    keywords = {fields[1] for fields in lines if len(fields) > 1}
    if keywords & {"BADSIG", "ERRSIG", "EXPKEYSIG", "REVKEYSIG", "EXPSIG", "NO_PUBKEY"}:
        raise ReleaseError(
            "tag signature is bad, expired, revoked or from an unknown key"
        )
    if "GOODSIG" not in keywords:
        raise ReleaseError("tag has no good signature")
    for fields in lines:
        if len(fields) > 2 and fields[1] == "VALIDSIG":
            candidates = {fields[2].upper()}
            if len(fields) > 11:
                candidates.add(fields[11].upper())
            for fpr in signers:
                if fpr in candidates:
                    return fpr
            raise ReleaseError(
                f"tag is signed by {fields[2]}, which is not a pinned signer"
            )
    raise ReleaseError("tag signature has no VALIDSIG status")


def verify_signature(root: Path, tag: str, signers: tuple[str, ...]) -> str:
    """Verify ``tag`` with git/gpg and return the pinned fingerprint that signed it."""
    env = dict(os.environ)
    env["LC_ALL"] = "C"
    try:
        proc = run_process(
            ["git", "-C", str(root), "verify-tag", "--raw", f"refs/tags/{tag}"],
            env=env,
            timeout=60,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        raise ReleaseError("git verify-tag could not run") from None
    fpr = parse_gpg_status(proc.stderr, signers)
    if proc.returncode != 0:
        raise ReleaseError(f"git verify-tag rejected {tag} (exit {proc.returncode})")
    return fpr


def default_branch(repository: str) -> str:
    branch = command(
        ["gh", "api", f"repos/{repository}", "--jq", ".default_branch"], timeout=60
    ).strip()
    if not branch or not re.match(r"^[A-Za-z0-9._/-]+$", branch):
        raise ReleaseError("could not determine the default branch")
    return branch


def is_ancestor(root: Path, commit: str, ref: str) -> bool:
    try:
        git(root, "merge-base", "--is-ancestor", commit, ref)
    except CommandError as exc:
        if exc.returncode == 1:
            return False
        raise
    return True


def _check(name: str, func) -> dict:
    try:
        detail = func()
    except ReleaseError as exc:
        return {"check": name, "ok": False, "detail": redact(str(exc))}
    if detail is None:
        return {"check": name, "ok": None, "detail": "not verified (read-only plan)"}
    return {"check": name, "ok": True, "detail": detail}


def trust_checks(
    context: ReleaseContext, root: Path, signers: tuple[str, ...], *, fresh: bool
) -> list[dict]:
    """Tag, signature, remote tag and default-branch checks.

    ``fresh`` fetches the default branch into FETCH_HEAD of ``root`` (the
    only repository write); without it the existing origin ref is used and
    the reachability result is advisory.
    """
    identity: dict[str, str] = {}

    def local_tag() -> str:
        obj, commit = tag_identity(root, context.tag)
        if commit != context.commit:
            raise ReleaseError(
                f"tag {context.tag} now points to {commit}, context has {context.commit}"
            )
        identity["obj"] = obj
        return f"{context.tag} -> {commit}"

    def signature() -> str:
        return f"signed by pinned key {verify_signature(root, context.tag, signers)}"

    def remote_tag() -> str:
        if "obj" not in identity:
            raise ReleaseError("local tag check failed")
        remote = remote_tag_oids(context.repository, context.tag)
        found = remote.get(f"refs/tags/{context.tag}")
        if found is None:
            raise ReleaseError(f"tag {context.tag} is not on {context.repository}")
        if found != identity["obj"]:
            raise ReleaseError(
                f"remote tag object {found} differs from local {identity['obj']}"
            )
        return "remote tag object matches"

    def reachable() -> str | None:
        branch = default_branch(context.repository)
        if fresh:
            git(
                root,
                "fetch",
                "--no-tags",
                "--quiet",
                remote_url(context.repository),
                f"refs/heads/{branch}",
                timeout=300,
            )
            ref = "FETCH_HEAD"
        else:
            ref = f"refs/remotes/origin/{branch}"
            try:
                git(root, "rev-parse", "--verify", "--quiet", ref)
            except CommandError:
                return None
        if not is_ancestor(root, context.commit, ref):
            raise ReleaseError(f"{context.commit} is not reachable from {branch}")
        return f"reachable from {branch}" + (
            "" if fresh else " (local ref, not refreshed)"
        )

    return [
        _check("tag", local_tag),
        _check("signature", signature),
        _check("remote_tag", remote_tag),
        _check("default_branch", reachable),
    ]


def checks_failed(checks: list[dict], *, strict: bool) -> list[str]:
    return [
        c["check"]
        for c in checks
        if c["ok"] is False or (strict and c["ok"] is not True)
    ]


# ---------------------------------------------------------------------------
# Distro E2E gate
# ---------------------------------------------------------------------------


def expected_targets(targets_tsv: str) -> list[str]:
    """Target ids exactly as distro-e2e.yml resolves them, plus ``arch``.

    Mirrors the workflow: skip comment lines and lines with fewer than four
    tab-separated fields; the first field is the target id.
    """
    targets: list[str] = []
    for line in targets_tsv.splitlines():
        if re.match(r"^\s*#", line):
            continue
        fields = line.split("\t")
        if not line.strip() or len(fields) < 4:
            continue
        target = fields[0]
        if not TARGET_ID_RE.match(target):
            raise ReleaseError(f"{TARGETS_FILE}: invalid target id {target!r}")
        targets.append(target)
    targets.extend(EXTRA_TARGETS)
    if len(targets) == len(EXTRA_TARGETS):
        raise ReleaseError(f"{TARGETS_FILE} lists no targets")
    if len(set(targets)) != len(targets):
        raise ReleaseError(f"{TARGETS_FILE} lists a target twice")
    return targets


def classify_runs(runs: list[dict], expected: list[str], commit: str) -> dict:
    """Decide the gate from workflow runs (each with a ``jobs`` list).

    A run qualifies only if it is for ``commit``, completed with success, and
    has exactly one successful ``Distro E2E (<target>)`` job for every
    expected target and no other target, with no failed auxiliary job.
    Returns status ``ready``, ``pending``, ``needs_dispatch`` (no full run
    and no earlier workflow_dispatch for the commit) or ``failed``.
    """
    wanted = set(expected)
    own = [r for r in runs if r.get("head_sha") == commit]
    ready: list[dict] = []
    pending: list[dict] = []
    summaries: list[dict] = []
    for run in own:
        targets: dict[str, dict] = {}
        problems: list[str] = []
        for job in run.get("jobs") or []:
            match = GATE_JOB_RE.match(job.get("name", ""))
            if match:
                name = match.group("target")
                if name in targets:
                    problems.append(f"duplicate job {name}")
                targets[name] = job
            elif job.get("status") == "completed" and job.get("conclusion") not in (
                "success",
                "skipped",
            ):
                problems.append(f"job {job.get('name')!r} {job.get('conclusion')}")
        missing = sorted(wanted - targets.keys())
        unexpected = sorted(targets.keys() - wanted)
        bad = sorted(
            t
            for t in wanted & targets.keys()
            if targets[t].get("status") == "completed"
            and targets[t].get("conclusion") != "success"
        )
        if unexpected:
            problems.append("unexpected targets " + ", ".join(unexpected))
        if bad:
            problems.append("failed targets " + ", ".join(bad))
        summary = {
            "run_id": run.get("id"),
            "event": run.get("event"),
            "status": run.get("status"),
            "conclusion": run.get("conclusion"),
            "url": run.get("html_url"),
        }
        if run.get("status") != "completed":
            if targets and missing:
                problems.append("partial matrix: missing " + ", ".join(missing))
            if problems:
                summary["problems"] = problems
            else:
                pending.append(summary)
            summaries.append(summary)
            continue
        if missing:
            problems.append("partial matrix: missing " + ", ".join(missing))
        incomplete = [
            t
            for t in wanted & targets.keys()
            if targets[t].get("status") != "completed"
        ]
        if incomplete:
            problems.append("unfinished targets " + ", ".join(sorted(incomplete)))
        if run.get("conclusion") != "success":
            problems.append(f"run concluded {run.get('conclusion')}")
        if problems:
            summary["problems"] = problems
        else:
            ready.append(summary)
        summaries.append(summary)

    if ready:
        best = max(ready, key=lambda s: s["run_id"] or 0)
        return {
            "status": "ready",
            "run_id": best["run_id"],
            "url": best["url"],
            "runs": summaries,
        }
    if pending:
        best = max(pending, key=lambda s: s["run_id"] or 0)
        return {
            "status": "pending",
            "run_id": best["run_id"],
            "url": best["url"],
            "runs": summaries,
        }
    if not any(r.get("event") == "workflow_dispatch" for r in own):
        return {"status": "needs_dispatch", "runs": summaries}
    return {"status": "failed", "runs": summaries}


def fetch_runs(repository: str, commit: str) -> list[dict]:
    def lines(path: str, jq: str) -> list[dict]:
        out = command(["gh", "api", "--paginate", path, "--jq", jq], timeout=120)
        try:
            return [json.loads(line) for line in out.splitlines() if line.strip()]
        except ValueError:
            raise ReleaseError("gh returned invalid workflow JSON") from None

    runs = lines(
        f"repos/{repository}/actions/workflows/{GATE_WORKFLOW}/runs?head_sha={commit}&per_page=100",
        ".workflow_runs[] | {id, status, conclusion, event, head_sha, html_url}",
    )
    for run in runs:
        if run.get("head_sha") == commit:
            run["jobs"] = lines(
                f"repos/{repository}/actions/runs/{run['id']}/jobs?filter=latest&per_page=100",
                ".jobs[] | {name, status, conclusion}",
            )
    return runs


def evaluate_gate(context: ReleaseContext) -> dict:
    """Read-only gate evaluation from the context's tagged target list."""
    targets_path = context.source_dir / TARGETS_FILE
    if targets_path.is_symlink() or not targets_path.is_file():
        raise ReleaseError(f"tagged source lacks {TARGETS_FILE}")
    expected = expected_targets(targets_path.read_text(encoding="utf-8"))
    decision = classify_runs(
        fetch_runs(context.repository, context.commit), expected, context.commit
    )
    decision["expected_targets"] = expected
    return decision


def _gate_out(
    status: str, detail: str, context: ReleaseContext, decision: dict, **extra
) -> dict:
    out = {
        "gate": "distro-e2e",
        "status": status,
        "detail": detail,
        "commit": context.commit,
        "expected_targets": decision.get("expected_targets"),
        "runs": decision.get("runs", []),
    }
    for key in ("run_id", "url"):
        if decision.get(key) is not None:
            out[key] = decision[key]
    out.update(extra)
    return out


def gate(
    context: ReleaseContext, root: Path, signers: tuple[str, ...], *, execute: bool
) -> dict:
    decision = evaluate_gate(context)
    status = decision["status"]
    count = len(decision["expected_targets"])
    if status == "ready":
        return _gate_out(
            "ready", f"full {count}-target Distro E2E passed", context, decision
        )
    if status == "pending":
        return _gate_out("pending", "Distro E2E run in progress", context, decision)
    if status == "failed":
        return _gate_out(
            "failed",
            "no full green run; the dispatched run did not pass",
            context,
            decision,
        )

    marker = context.work_dir / DISPATCH_MARKER
    if marker.exists() or marker.is_symlink():
        try:
            dispatched = float(
                json.loads(marker.read_text(encoding="utf-8"))["dispatched_at"]
            )
        except (OSError, ValueError, KeyError, TypeError):
            raise ReleaseError(
                f"{marker} is corrupt; inspect Actions before removing it"
            ) from None
        if time.time() - dispatched > DISPATCH_VISIBLE_WITHIN:
            return _gate_out(
                "failed", "dispatched Distro E2E run never appeared", context, decision
            )
        return _gate_out(
            "pending", "Distro E2E dispatched; run not listed yet", context, decision
        )
    if not execute:
        return _gate_out(
            "blocked",
            "no full Distro E2E run for this commit; gate --execute dispatches one",
            context,
            decision,
        )
    checks = trust_checks(context, root, signers, fresh=True)
    failed = checks_failed(checks, strict=True)
    if failed:
        return _gate_out(
            "blocked",
            "not dispatching: " + ", ".join(failed) + " failed",
            context,
            decision,
            checks=checks,
        )
    # The marker is the one-dispatch record; it is only kept once gh accepted
    # the dispatch. A known failure removes it so the next pass can retry
    # instead of waiting DISPATCH_VISIBLE_WITHIN for a run that never existed.
    # A hard kill between creating the marker and gh returning may still leave
    # it; that case expires after DISPATCH_VISIBLE_WITHIN as before.
    fd = os.open(
        marker,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
        0o644,
    )
    with os.fdopen(fd, "w", encoding="utf-8") as handle:
        json.dump(
            {
                "dispatched_at": time.time(),
                "tag": context.tag,
                "commit": context.commit,
            },
            handle,
        )
    try:
        command(
            [
                "gh",
                "workflow",
                "run",
                GATE_WORKFLOW,
                "--repo",
                context.repository,
                "--ref",
                context.tag,
                "-f",
                "targets=all",
            ],
            timeout=60,
        )
    except BaseException as exc:
        marker.unlink(missing_ok=True)
        if isinstance(exc, ReleaseError):
            return _gate_out(
                "failed",
                f"dispatch of {GATE_WORKFLOW} failed: {redact(str(exc))}",
                context,
                decision,
                checks=checks,
            )
        raise
    return _gate_out(
        "pending",
        f"dispatched {GATE_WORKFLOW} for {context.tag}",
        context,
        decision,
        checks=checks,
    )


# ---------------------------------------------------------------------------
# prepare
# ---------------------------------------------------------------------------


def _git_archive(root: Path, commit: str, version: str) -> bytes:
    try:
        proc = run_process(
            [
                "git",
                "-C",
                str(root),
                "-c",
                "tar.umask=0022",
                "archive",
                "--format=tar",
                f"--prefix={source_root_name(version)}/",
                commit,
            ],
            timeout=300,
            binary=True,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        raise ReleaseError("git archive could not run") from None
    if proc.returncode != 0:
        raise ReleaseError(
            f"git archive failed: {redact(proc.stderr.decode(errors='replace'))[-400:]}"
        )
    return proc.stdout


def _published_archive(repository: str, tag: str, version: str) -> bytes | None:
    import release_github

    releases = [
        r for r in release_github.find_releases(repository, tag) if not r.get("draft")
    ]
    if len(releases) > 1:
        raise ReleaseError(f"more than one release uses tag {tag}")
    for release in releases:
        for asset in release.get("assets", []):
            if asset.get("name") == asset_name(version):
                if asset.get("state", "uploaded") != "uploaded":
                    raise ReleaseError(f"published {asset_name(version)} is incomplete")
                data = release_github.download_asset(
                    repository, tag, asset_name(version)
                )
                digest = asset.get("digest")
                if isinstance(digest, str) and digest != f"sha256:{sha256_bytes(data)}":
                    raise ReleaseError(
                        f"downloaded {asset_name(version)} does not match GitHub's digest"
                    )
                return data
    return None


def _write_new(path: Path, data: bytes) -> None:
    fd = os.open(
        path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o644
    )
    with os.fdopen(fd, "wb") as handle:
        handle.write(data)


def prepare(
    tag: str, output: Path, repo_root: Path, repository: str, signers: tuple[str, ...]
) -> dict:
    version = parse_tag(tag)
    if not REPOSITORY_RE.match(repository) or ".." in repository:
        raise ReleaseError(f"repository {repository!r} is not owner/name")
    _, commit = tag_identity(repo_root, tag)
    signer = verify_signature(repo_root, tag, signers)

    work_dir = output.expanduser().resolve()
    git_dir = Path(git(repo_root, "rev-parse", "--absolute-git-dir")).resolve()
    for protected in (repo_root, git_dir):
        if work_dir == protected or work_dir.is_relative_to(protected):
            raise ReleaseError(f"--output must be outside {protected}")
    if work_dir.exists():
        if work_dir.is_symlink() or not work_dir.is_dir() or any(work_dir.iterdir()):
            raise ReleaseError(f"--output {work_dir} must be a new or empty directory")
    else:
        work_dir.mkdir(mode=0o755, parents=True)
    work_dir = work_dir.resolve()

    data = gzip_deterministic(_git_archive(repo_root, commit, version))
    comment, manifest = archive_manifest(data, version)
    if comment != commit:
        raise ReleaseError("git archive did not record the tagged commit")
    origin = "generated"
    published = _published_archive(repository, tag, version)
    if published is not None and published != data:
        pub_comment, pub_manifest = archive_manifest(published, version)
        differences = compare_manifests(manifest, pub_manifest)
        if pub_comment != commit or differences:
            raise ReleaseError(
                f"published {asset_name(version)} does not match {tag}: "
                + (", ".join(differences) or "different commit")
            )
        data = published
    if published is not None:
        origin = "published"

    archive = work_dir / asset_name(version)
    _write_new(archive, data)
    source_dir = extract_archive(data, version, work_dir)
    revision = check_source_versions(source_dir, version)

    notes_path = work_dir / "notes.md"
    context = ReleaseContext(
        repository=repository,
        tag=tag,
        version=version,
        commit=commit,
        source_dir=source_dir,
        archive=archive,
        sha256=sha256_bytes(data),
        source_url=asset_url(repository, tag, version),
        work_dir=work_dir,
        notes_path=notes_path,
        debian_revision=revision,
    )
    # Same generator load_context() re-verifies the file against.
    _write_new(notes_path, expected_notes(context))
    context_path = work_dir / "context.json"
    _write_new(
        context_path, (json.dumps(context_to_json(context), indent=2) + "\n").encode()
    )
    load_context(context_path)
    return {
        "context": str(context_path),
        "tag": tag,
        "version": version,
        "commit": commit,
        "signer": signer,
        "sha256": context.sha256,
        "archive": str(archive),
        "archive_origin": origin,
        "source_url": context.source_url,
        "debian_revision": revision,
        "notes": str(notes_path),
    }


# ---------------------------------------------------------------------------
# channels
# ---------------------------------------------------------------------------


def channel_status(channel: str, context: ReleaseContext) -> dict:
    if channel == "github":
        import release_github

        return release_github.status(context)
    if channel == "aur":
        import release_aur

        return release_aur.status(context)
    if channel == "obs":
        import release_rpm

        return release_rpm.status_obs(context)
    if channel == "copr":
        import release_rpm

        return release_rpm.status_copr(context)
    if channel == "ppa":
        import release_ppa

        return release_ppa.status(context)
    raise ReleaseError(f"unknown channel {channel}")


def channel_publish(channel: str, context: ReleaseContext, *, execute: bool) -> dict:
    if channel == "github":
        import release_github

        return release_github.publish(context, execute=execute)
    if channel == "aur":
        import release_aur

        return release_aur.publish(context, execute=execute)
    if channel == "obs":
        import release_rpm

        return release_rpm.publish_obs(context, execute=execute)
    if channel == "copr":
        import release_rpm

        return release_rpm.publish_copr(context, execute=execute)
    if channel == "ppa":
        import release_ppa

        return release_ppa.publish(context, execute=execute)
    raise ReleaseError(f"unknown channel {channel}")


def _guarded(channel: str, func) -> dict:
    """Run one channel; a crash becomes that channel's ``failed`` result."""
    try:
        out = func()
    except ReleaseError as exc:
        return result(channel, "failed", str(exc))
    except Exception as exc:  # noqa: BLE001 - one channel must not stop the others
        return result(channel, "failed", f"{type(exc).__name__}: {exc}")
    if not isinstance(out, dict) or out.get("channel") != channel:
        return result(channel, "failed", "adapter returned a malformed result")
    return out


def publish(
    context: ReleaseContext,
    channels: list[str],
    root: Path,
    signers: tuple[str, ...],
    *,
    execute: bool,
) -> dict:
    checks = trust_checks(context, root, signers, fresh=execute)
    try:
        decision = evaluate_gate(context)
        gate_state = {
            "status": decision["status"],
            "run_id": decision.get("run_id"),
            "runs": decision["runs"],
        }
    except ReleaseError as exc:
        gate_state = {"status": "failed", "detail": redact(str(exc))}
    out = {
        "tag": context.tag,
        "version": context.version,
        "commit": context.commit,
        "execute": execute,
        "preflight": {"checks": checks, "gate": gate_state},
    }
    if not execute:
        out["results"] = [
            _guarded(ch, lambda ch=ch: channel_publish(ch, context, execute=False))
            for ch in channels
        ]
        return out

    failed = checks_failed(checks, strict=True)
    if failed:
        reason = "release trust checks failed: " + ", ".join(failed)
        out["results"] = [result(ch, "blocked", reason) for ch in channels]
        return out
    if gate_state["status"] != "ready":
        status = (
            "pending"
            if gate_state["status"] in ("pending", "needs_dispatch")
            else "blocked"
        )
        reason = f"Distro E2E gate is {gate_state['status']}; run gate --execute"
        out["results"] = [result(ch, status, reason) for ch in channels]
        return out

    results: list[dict] = []
    if "github" in channels:
        github = _guarded(
            "github", lambda: channel_publish("github", context, execute=True)
        )
        results.append(github)
    else:
        github = _guarded("github", lambda: channel_status("github", context))
    for ch in channels:
        if ch == "github":
            continue
        if github["status"] not in DONE:
            results.append(
                result(
                    ch,
                    "blocked",
                    f"GitHub source asset is not published ({github['status']}): {github['detail']}",
                )
            )
            continue
        results.append(
            _guarded(ch, lambda ch=ch: channel_publish(ch, context, execute=True))
        )
    out["results"] = results
    return out


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)

    def signer_arg(p: argparse.ArgumentParser) -> None:
        p.add_argument(
            "--signer",
            action="append",
            metavar="FPR",
            help=f"pinned OpenPGP fingerprint allowed to sign tags (repeatable; default {PINNED_SIGNERS[0]})",
        )

    def root_arg(p: argparse.ArgumentParser, extra: str = "") -> None:
        p.add_argument(
            "--repo-root",
            help="git repository holding the tag (default: current checkout)" + extra,
        )

    p = sub.add_parser("prepare", help="verify a tag and write a release context")
    p.add_argument("--tag", required=True)
    p.add_argument(
        "--output",
        required=True,
        type=Path,
        help="new or empty directory outside the repository",
    )
    p.add_argument("--repository", default=DEFAULT_REPOSITORY)
    root_arg(p)
    signer_arg(p)

    p = sub.add_parser("publish", help="plan or (--execute) publish channels")
    p.add_argument("--context", required=True, type=Path)
    p.add_argument("--channel", required=True, choices=[*CHANNELS, "all"])
    p.add_argument("--execute", action="store_true", help="perform external writes")
    root_arg(p)
    signer_arg(p)

    p = sub.add_parser("status", help="read-only per-channel publication report")
    p.add_argument("--context", required=True, type=Path)
    p.add_argument("--channel", choices=[*CHANNELS, "all"], default="all")
    root_arg(p, "; when given, local tag checks are included")
    signer_arg(p)

    p = sub.add_parser(
        "gate", help="inspect (--execute: dispatch once) the Distro E2E gate"
    )
    p.add_argument("--context", required=True, type=Path)
    p.add_argument(
        "--execute",
        action="store_true",
        help="dispatch distro-e2e.yml once if no full run exists",
    )
    root_arg(p)
    signer_arg(p)
    return parser


def run(args: argparse.Namespace) -> tuple[dict, int]:
    signers = normalize_signers(args.signer)
    if args.command == "prepare":
        root = resolve_repo_root(args.repo_root)
        return prepare(args.tag, args.output, root, args.repository, signers), 0

    context = load_context(args.context)

    if args.command == "gate":
        out = gate(
            context, resolve_repo_root(args.repo_root), signers, execute=args.execute
        )
        return out, 1 if out["status"] == "failed" else 0

    # Only the publish and status parsers define --channel.
    channels = list(CHANNELS) if args.channel in (None, "all") else [args.channel]

    if args.command == "status":
        out: dict = {
            "tag": context.tag,
            "version": context.version,
            "commit": context.commit,
        }
        if args.repo_root:
            out["preflight"] = {
                "checks": trust_checks(
                    context, resolve_repo_root(args.repo_root), signers, fresh=False
                )
            }
        out["results"] = [
            _guarded(ch, lambda ch=ch: channel_status(ch, context)) for ch in channels
        ]
    else:
        out = publish(
            context,
            channels,
            resolve_repo_root(args.repo_root),
            signers,
            execute=args.execute,
        )
    return out, 1 if any(r["status"] == "failed" for r in out["results"]) else 0


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        out, code = run(args)
    except ReleaseError as exc:
        out, code = {"error": redact(str(exc))}, 2
    print(json.dumps(out, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
