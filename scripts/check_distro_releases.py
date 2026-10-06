#!/usr/bin/env python3
"""
check_distro_releases.py — Distro release watcher for Krema.

Detects distribution releases that a build channel already offers but Krema
does not build there yet, and keeps one GitHub issue per (release, channel)
pair in sync (label: distro-release). An issue therefore only appears once
the maintainer can actually add the release on that channel.

Detection rule:
    A distro version is *missing* on a channel when it is numerically greater
    than the highest version that channel currently builds and is not already
    tracked there. Versions <= the tracked max are deliberately-not-built
    older releases; EOL is never a removal reason, so EOL cycles are never
    flagged and only new releases produce issues.

    A missing version is *actionable* when the channel offers it (OBS has the
    distro project, COPR has the chroot, Launchpad has the series active).
    Pre-releases count as soon as the channel offers them. Missing versions a
    channel does not offer yet are only logged as waiting; their issue is
    opened by the first run after the channel starts offering them.

Channels:
    Fedora   : OBS (Fedora_<N> in project.meta.xml), COPR (fedora-<N>-* chroots)
    Ubuntu   : OBS (xUbuntu_<ver>), Launchpad PPA (published series)
    Debian   : OBS (Debian_<N>)
    openSUSE : OBS (openSUSE_Leap_<ver>)
    Rolling targets (Rawhide, Tumbleweed, Slowroll, AUR/Arch) are ignored.

Issue lifecycle (one issue per actionable gap, title
"Distro release: <Distro> <version> on <channel>"):
    absent -> create; open -> edit body when it changed; closed -> untouched.
    An open issue is commented and closed once its gap is gone (the channel
    builds the release, or stopped offering it). The close pass is skipped
    when any data source was unreachable, so a partial run never closes an
    issue prematurely.

Exit code:
    0  — run completed (new issues may or may not have been filed)
    1  — project.meta.xml could not be parsed, or a gh write failed
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from collections.abc import Callable
from dataclasses import dataclass, field
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

USER_AGENT = "krema-distro-release-watch (https://github.com/isac322/krema)"
HTTP_TIMEOUT = 20  # seconds

EOL_API = "https://endoflife.date/api/{distro}.json"
OBS_META_API = "https://api.opensuse.org/public/source/{project}/_meta"
COPR_PROJECT_API = (
    "https://copr.fedorainfracloud.org/api_3/project"
    "?ownername=isac322&projectname=krema"
)
COPR_CHROOTS_API = "https://copr.fedorainfracloud.org/api_3/mock-chroots/list"
LP_SOURCES_API = (
    "https://api.launchpad.net/devel/~isac322/+archive/ubuntu/krema"
    "?ws.op=getPublishedSources&source_name=krema"
)
LP_SERIES_API = "https://api.launchpad.net/devel/ubuntu/series"

LABEL = "distro-release"
LABEL_COLOR = "0E8A16"
LABEL_DESCRIPTION = (
    "New distribution release a build channel offers but Krema does not "
    "build there"
)

POLICY_LINK = (
    "See the [Distribution Support Policy]"
    "(https://github.com/isac322/krema/blob/master/AGENTS.md"
    "#distribution-support-policy) in AGENTS.md."
)

MARKER_RE = re.compile(r"<!--\s*distro-release:([\w-]+):([\d.]+):([\w-]+)\s*-->")


def warn(msg: str) -> None:
    print(f"warning: {msg}", file=sys.stderr)


def version_key(version: str) -> tuple[int, ...]:
    """Numeric version tuple: '26.04' -> (26, 4); '45' -> (45,)."""
    parts = []
    for chunk in version.split("."):
        m = re.match(r"\d+", chunk)
        if m is None:
            break
        parts.append(int(m.group(0)))
    return tuple(parts)


def fmt_version(key: tuple[int, ...]) -> str:
    return ".".join(str(p) for p in key)


# ---------------------------------------------------------------------------
# HTTP helpers (failures warn and return None, never abort the run)
# ---------------------------------------------------------------------------


def fetch_json(url: str, what: str) -> object | None:
    """GET JSON; return None (and warn) on any failure."""
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as resp:
            return json.load(resp)
    except (urllib.error.URLError, TimeoutError, OSError, ValueError) as e:
        warn(f"failed to fetch {what} ({url}): {e}")
        return None


def check_obs_project(project: str) -> bool | None:
    """True if OBS offers the distro project, False on 404, None on error."""
    req = urllib.request.Request(
        OBS_META_API.format(project=project),
        headers={"User-Agent": USER_AGENT},
    )
    try:
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT):
            return True
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return False
        warn(f"failed to check OBS project {project}: HTTP {e.code}")
        return None
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        warn(f"failed to check OBS project {project}: {e}")
        return None


# ---------------------------------------------------------------------------
# Tracked versions (what each channel currently builds)
# ---------------------------------------------------------------------------


def parse_obs_repos(meta_path: Path) -> set[str]:
    """Repository names declared in packaging/obs/project.meta.xml."""
    tree = ET.parse(meta_path)
    return {
        repo.attrib["name"]
        for repo in tree.getroot().iter("repository")
        if "name" in repo.attrib
    }


def tracked_versions(repo_names: set[str], prefix: str) -> set[tuple[int, ...]]:
    """Versions tracked by OBS repositories named '<prefix><version>'."""
    versions = set()
    for name in repo_names:
        if name.startswith(prefix):
            key = version_key(name[len(prefix) :])
            if key:
                versions.add(key)
    return versions


def fetch_copr_tracked() -> set[tuple[int, ...]] | None:
    """Fedora versions with a chroot enabled on the isac322/krema project."""
    data = fetch_json(COPR_PROJECT_API, "COPR project info")
    if data is None:
        return None
    versions = set()
    for name in data.get("chroot_repos", {}):
        m = re.fullmatch(r"fedora-(\d+)-\w+", name)
        if m:
            versions.add((int(m.group(1)),))
    return versions


def fetch_copr_available() -> set[tuple[int, ...]] | None:
    """Fedora versions COPR still offers as build chroots."""
    data = fetch_json(COPR_CHROOTS_API, "COPR mock-chroots list")
    if data is None:
        return None
    versions = set()
    for name in data:
        m = re.fullmatch(r"fedora-(\d+)-\w+", name)
        if m:
            versions.add((int(m.group(1)),))
    return versions


@dataclass
class LaunchpadInfo:
    """Ubuntu series knowledge from Launchpad."""

    name_to_version: dict[str, tuple[int, ...]] = field(default_factory=dict)
    version_str: dict[tuple[int, ...], str] = field(default_factory=dict)
    active: set[tuple[int, ...]] = field(default_factory=set)


def fetch_lp_series() -> LaunchpadInfo | None:
    """All Ubuntu series; active = active flag and not Obsolete/Future."""
    data = fetch_json(LP_SERIES_API, "Launchpad Ubuntu series")
    if data is None:
        return None
    info = LaunchpadInfo()
    for entry in data.get("entries", []):
        key = version_key(str(entry.get("version", "")))
        name = str(entry.get("name", ""))
        if not key or not name:
            continue
        info.name_to_version[name] = key
        info.version_str[key] = str(entry["version"])
        status = entry.get("status", "")
        if entry.get("active", False) and status not in ("Obsolete", "Future"):
            info.active.add(key)
    return info


def fetch_ppa_tracked(lp: LaunchpadInfo) -> set[tuple[int, ...]] | None:
    """Ubuntu versions with published krema sources in the PPA."""
    data = fetch_json(LP_SOURCES_API, "Launchpad published sources")
    if data is None:
        return None
    versions = set()
    for entry in data.get("entries", []):
        # distro_series_link ends with the series codename, e.g. ".../questing"
        name = (
            entry.get("distro_series_link", "").rstrip("/").rsplit("/", 1)[-1]
        )
        key = lp.name_to_version.get(name) or version_key(name)
        if key:
            versions.add(key)
    return versions


# ---------------------------------------------------------------------------
# Candidate versions (released cycles + pre-releases services already offer)
# ---------------------------------------------------------------------------
def eol_candidates(distro: str) -> dict[tuple[int, ...], tuple[str, str]] | None:
    """Released cycles from endoflife.date: version key -> (display, date)."""
    data = fetch_json(EOL_API.format(distro=distro), f"endoflife.date {distro}")
    if data is None:
        return None
    today = date.today().isoformat()
    out: dict[tuple[int, ...], tuple[str, str]] = {}
    for entry in data:
        cycle = str(entry.get("cycle", ""))
        release_date = str(entry.get("releaseDate", ""))
        if not release_date or release_date > today:
            continue  # not released yet
        if distro == "opensuse" and not re.fullmatch(r"\d+(\.\d+)*", cycle):
            continue  # numeric Leap cycles only; skip rolling/named ones
        key = version_key(cycle)
        if key:
            out[key] = (cycle, release_date)
    return out
# Model
# ---------------------------------------------------------------------------


@dataclass
class Gap:
    """A release one channel offers but Krema does not build there yet."""

    distro: str  # display name, e.g. "Fedora"
    slug: str  # distro slug used in the marker, e.g. "fedora"
    version: str  # display version, e.g. "45" or "26.10"
    channel: str  # display name, e.g. "OBS"
    channel_slug: str  # marker slug, e.g. "obs"
    target: str  # what to add on the channel, markdown
    action: str  # how to add it, markdown
    released: str | None  # upstream release date; None = pre-release

    @property
    def key(self) -> str:
        return f"{self.slug}:{self.version}:{self.channel_slug}"

    @property
    def title(self) -> str:
        return f"Distro release: {self.distro} {self.version} on {self.channel}"

    @property
    def marker(self) -> str:
        return f"<!-- distro-release:{self.key} -->"


def build_body(gap: Gap) -> str:
    status = (
        f"released {gap.released}"
        if gap.released
        else f"pre-release ({gap.channel} already offers it)"
    )
    lines = [
        f"**{gap.channel}** now offers **{gap.distro} {gap.version}**, but "
        "Krema does not build for it there yet.",
        "",
        f"- Release status: {status}",
        f"- Target: {gap.target}",
        f"- Action: {gap.action}",
        "- Also list the release in the README Installation table if it is "
        "not there yet.",
        "",
        "This issue closes automatically once the channel builds the target.",
        "",
        gap.marker,
        "",
        f"> Reminder: {POLICY_LINK} EOL is not a removal reason; a target is "
        "retired only when it breaks or blocks product work.",
    ]
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Detection
# ---------------------------------------------------------------------------


def missing(
    tracked: set[tuple[int, ...]],
    cand: tuple[int, ...],
    eol_dates: dict[tuple[int, ...], str],
) -> bool:
    """True when cand exceeds the channel's tracked max and is not tracked.

    `eol_dates` maps version keys to release dates (may be empty when the
    endoflife.date fetch failed). It guards against numbering schemes that
    are not monotonic in time — openSUSE Leap went 42.x -> 15.x -> 16.x, so
    a numerically larger cycle can be older than the tracked max.
    """
    if not tracked or cand in tracked or cand <= max(tracked):
        return False
    tmax_date = eol_dates.get(max(tracked))
    cand_date = eol_dates.get(cand)
    if tmax_date and cand_date and cand_date <= tmax_date:
        return False  # higher number but released earlier
    return True


def detect(repo_names: set[str]) -> tuple[list[Gap], list[str], bool]:
    """Return (actionable gaps, waiting gaps, had_unknown_source).

    Waiting gaps are missing releases the channel does not offer yet; they
    are only logged.
    """
    obs_fedora = tracked_versions(repo_names, "Fedora_")
    obs_ubuntu = tracked_versions(repo_names, "xUbuntu_")
    obs_debian = tracked_versions(repo_names, "Debian_")
    obs_leap = tracked_versions(repo_names, "openSUSE_Leap_")

    copr_tracked = fetch_copr_tracked()
    copr_available = fetch_copr_available()
    lp = fetch_lp_series()
    ppa_tracked = fetch_ppa_tracked(lp) if lp is not None else None

    eol_fedora = eol_candidates("fedora")
    eol_ubuntu = eol_candidates("ubuntu")
    eol_debian = eol_candidates("debian")
    eol_leap = eol_candidates("opensuse")

    had_unknown = any(
        x is None
        for x in (
            copr_tracked,
            copr_available,
            lp,
            ppa_tracked,
            eol_fedora,
            eol_ubuntu,
            eol_debian,
            eol_leap,
        )
    )

    # Candidate version key -> display string; dates kept separately for the
    # release-date guard inside missing() and the issue's release status.
    def split_eol(
        eol: dict[tuple[int, ...], tuple[str, str]] | None,
    ) -> tuple[dict[tuple[int, ...], str], dict[tuple[int, ...], str]]:
        cands = {k: v[0] for k, v in (eol or {}).items()}
        dates = {k: v[1] for k, v in (eol or {}).items()}
        return cands, dates

    fedora_cands, fedora_dates = split_eol(eol_fedora)
    for k in copr_available or ():
        fedora_cands.setdefault(k, fmt_version(k))

    ubuntu_cands, ubuntu_dates = split_eol(eol_ubuntu)
    ubuntu_codename: dict[tuple[int, ...], str] = {}
    if lp is not None:
        ubuntu_codename = {k: n for n, k in lp.name_to_version.items()}
        for k in lp.active:
            ubuntu_cands.setdefault(k, lp.version_str.get(k, fmt_version(k)))

    debian_cands, debian_dates = split_eol(eol_debian)
    leap_cands, leap_dates = split_eol(eol_leap)

    gaps: list[Gap] = []
    waiting: list[str] = []

    def scan(
        distro: str,
        slug: str,
        cands: dict[tuple[int, ...], str],
        dates: dict[tuple[int, ...], str],
        tracked: set[tuple[int, ...]] | None,
        channel: str,
        channel_slug: str,
        offered: Callable[[tuple[int, ...], str], bool | None],
        target: Callable[[tuple[int, ...], str], str],
        action: str,
    ) -> None:
        nonlocal had_unknown
        if tracked is None:
            return  # already counted as an unknown source
        for ver in sorted(cands):
            if not missing(tracked, ver, dates):
                continue
            vstr = cands[ver]
            state = offered(ver, vstr)
            if state is None:
                had_unknown = True
                warn(f"cannot tell whether {channel} offers {distro} {vstr}")
            elif not state:
                waiting.append(f"{distro} {vstr} on {channel}")
            else:
                gaps.append(
                    Gap(
                        distro=distro,
                        slug=slug,
                        version=vstr,
                        channel=channel,
                        channel_slug=channel_slug,
                        target=target(ver, vstr),
                        action=action,
                        released=dates.get(ver),
                    )
                )

    obs_action = (
        "add the repository to `packaging/obs/project.meta.xml` and the "
        "matching row to `tests/distro/targets.tsv`"
    )

    def obs(prefix: str) -> Callable[[tuple[int, ...], str], bool | None]:
        return lambda _ver, vstr: check_obs_project(f"{prefix}{vstr}")

    # --- Fedora ----------------------------------------------------------
    scan(
        "Fedora", "fedora", fedora_cands, fedora_dates, obs_fedora,
        "OBS", "obs", obs("Fedora:"),
        lambda _ver, vstr: f"repository `Fedora_{vstr}`",
        obs_action,
    )
    scan(
        "Fedora", "fedora", fedora_cands, fedora_dates, copr_tracked,
        "COPR", "copr",
        lambda ver, _vstr: (
            None if copr_available is None else ver in copr_available
        ),
        lambda _ver, vstr: (
            f"chroots `fedora-{vstr}-x86_64`, `fedora-{vstr}-aarch64`"
        ),
        "enable the chroots in the isac322/krema COPR project settings, "
        "then rebuild the latest release into them",
    )

    # --- Ubuntu ----------------------------------------------------------
    scan(
        "Ubuntu", "ubuntu", ubuntu_cands, ubuntu_dates, obs_ubuntu,
        "OBS", "obs", obs("Ubuntu:"),
        lambda _ver, vstr: f"repository `xUbuntu_{vstr}`",
        obs_action,
    )
    scan(
        "Ubuntu", "ubuntu", ubuntu_cands, ubuntu_dates, ppa_tracked,
        "Launchpad PPA", "ppa",
        lambda ver, _vstr: None if lp is None else ver in lp.active,
        lambda ver, vstr: f"series `{ubuntu_codename.get(ver, vstr)}`",
        "upload the latest release's source package for the series "
        "(release skill, section *Launchpad PPA*)",
    )

    # --- Debian ----------------------------------------------------------
    scan(
        "Debian", "debian", debian_cands, debian_dates, obs_debian,
        "OBS", "obs", obs("Debian:"),
        lambda _ver, vstr: f"repository `Debian_{vstr}`",
        obs_action,
    )

    # --- openSUSE Leap ---------------------------------------------------
    scan(
        "openSUSE Leap", "opensuse-leap", leap_cands, leap_dates, obs_leap,
        "OBS", "obs", obs("openSUSE:Leap:"),
        lambda _ver, vstr: f"repository `openSUSE_Leap_{vstr}`",
        obs_action,
    )

    return gaps, waiting, had_unknown


# ---------------------------------------------------------------------------
# Issue reconciliation via gh CLI
# ---------------------------------------------------------------------------


def gh_available() -> bool:
    try:
        return (
            subprocess.run(
                ["gh", "--version"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            ).returncode
            == 0
        )
    except FileNotFoundError:
        return False


def list_issues(repo: str) -> list[dict] | None:
    """All distro-release issues; None when gh or the listing failed."""
    if not gh_available():
        warn("gh CLI not found; treating existing issues as empty")
        return []
    cmd = [
        "gh", "issue", "list",
        "--label", LABEL,
        "--state", "all",
        "--limit", "200",
        "--json", "number,title,state,body",
    ]
    if repo:
        cmd += ["--repo", repo]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except subprocess.CalledProcessError as e:
        warn(f"gh issue list failed: {e.stderr.strip()}")
        return None
    return json.loads(proc.stdout)


def ensure_label(repo: str, dry_run: bool) -> bool:
    if dry_run:
        print(f"[dry-run] would ensure label {LABEL!r} exists")
        return True
    cmd = [
        "gh", "label", "create", LABEL,
        "--color", LABEL_COLOR,
        "--description", LABEL_DESCRIPTION,
        "--force",
    ]
    if repo:
        cmd += ["--repo", repo]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        warn("gh CLI not found; cannot create label")
        return False
    if proc.returncode != 0:
        warn(f"gh label create failed: {proc.stderr.strip()}")
        return False
    return True


def gh_write(args: list[str], repo: str) -> bool:
    cmd = ["gh", *args] + (["--repo", repo] if repo else [])
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        warn("gh CLI not found; cannot perform write")
        return False
    if proc.returncode != 0:
        warn(f"gh {' '.join(args[:2])} failed: {proc.stderr.strip()}")
        return False
    return True


def reconcile(
    gaps: list[Gap], repo: str, dry_run: bool, can_close: bool
) -> int:
    """Sync issues; return 0 on success, 1 if any gh write failed."""
    existing = list_issues(repo)
    if existing is None:
        if dry_run:
            existing = []
        else:
            warn("cannot reconcile without the issue list; skipping writes")
            return 1

    by_title = {i["title"]: i for i in existing}
    open_by_marker: dict[str, dict] = {}
    for issue in existing:
        m = MARKER_RE.search(issue.get("body") or "")
        if m and issue.get("state") == "OPEN":
            open_by_marker[":".join(m.groups())] = issue

    failures = 0

    # Close open issues whose gap is gone.
    if can_close:
        wanted = {g.key for g in gaps}
        for marker_key, issue in open_by_marker.items():
            if marker_key in wanted:
                continue
            number, title = issue["number"], issue["title"]
            comment = (
                "Nothing to do on this channel anymore: Krema builds this "
                "release there, or the channel stopped offering it. Closing."
            )
            if dry_run:
                print(
                    f"[dry-run] would comment + close issue #{number} "
                    f"({title})"
                )
                continue
            ok = gh_write(
                ["issue", "comment", str(number), "--body", comment], repo
            )
            ok &= gh_write(["issue", "close", str(number)], repo)
            if ok:
                print(f"closed issue #{number} ({title})")
            else:
                failures = 1
    elif open_by_marker:
        warn("some data sources were unreachable; skipping issue close pass")

    for rel in gaps:
        body = build_body(rel)
        issue = by_title.get(rel.title)

        if issue is None:
            if dry_run:
                print(f"[dry-run] would create issue {rel.title!r}")
                print(body)
                print("-" * 60)
                continue
            if gh_write(
                [
                    "issue", "create",
                    "--title", rel.title,
                    "--label", LABEL,
                    "--body", body,
                ],
                repo,
            ):
                print(f"created issue {rel.title!r}")
            else:
                failures = 1
            continue

        if issue.get("state") != "OPEN":
            print(
                f"issue #{issue['number']} {rel.title!r} is closed; "
                "leaving it alone"
            )
            continue

        if (issue.get("body") or "") == body:
            print(f"issue #{issue['number']} {rel.title!r} up to date")
            continue

        if dry_run:
            print(
                f"[dry-run] would update body of issue #{issue['number']} "
                f"{rel.title!r}"
            )
            print(body)
            print("-" * 60)
            continue
        if gh_write(
            ["issue", "edit", str(issue["number"]), "--body", body], repo
        ):
            print(f"updated issue #{issue['number']} {rel.title!r}")
        else:
            failures = 1

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--repo",
        default=os.environ.get("GITHUB_REPOSITORY", ""),
        help="OWNER/NAME for gh issue operations "
        "(default: $GITHUB_REPOSITORY, else the repo gh infers)",
    )
    parser.add_argument(
        "--meta",
        type=Path,
        default=ROOT / "packaging/obs/project.meta.xml",
        help="path to OBS project meta (default: "
        "packaging/obs/project.meta.xml relative to repo root)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="no gh write calls; print planned actions and full issue bodies",
    )
    args = parser.parse_args()

    try:
        repo_names = parse_obs_repos(args.meta)
    except (ET.ParseError, OSError) as e:
        warn(f"cannot parse tracked targets from {args.meta}: {e}")
        return 1

    gaps, waiting, had_unknown = detect(repo_names)

    for where in waiting:
        print(f"waiting: {where} (channel does not offer it yet)")
    for gap in gaps:
        print(f"actionable: {gap.distro} {gap.version} on {gap.channel}")
    if not gaps:
        print("No channel offers a release Krema does not build there.")

    if not ensure_label(args.repo, args.dry_run):
        return 1
    return reconcile(gaps, args.repo, args.dry_run, can_close=not had_unknown)


if __name__ == "__main__":
    sys.exit(main())
