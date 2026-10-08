#!/usr/bin/env python3
"""Prepare and check Krema's store, branding, and catalog surfaces for a verified release.

``prepare`` builds the offline store packet for the release in a verified
context plus the browser task list. ``status`` reads public pages and APIs and
reports each surface with the shared release status vocabulary. ``record``
keeps the local lock and receipt for a browser task so a second session does
not repeat an upload that is in progress or waiting for moderation:

- ``start`` takes the lock; it refuses while a lock or a pending receipt exists.
- ``pending`` records a submission with its reference and releases the lock;
  with no lock it updates an existing pending receipt's reference.
- ``published`` needs the lock or a pending receipt, re-reads the public store
  page, and refuses unless the release's own content is observed there.
- ``abort`` releases this release's lock without reading the packet, or, with
  no lock, withdraws a pending receipt given ``--reference`` evidence.

Nothing here writes to a store. KDE Store and AlternativeTo expose no stable
write API that this tooling can use, so their updates stay ``browser_required``
until a person following the browser playbook completes them.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import re
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))

from prepare_submission_kit import (
    ROOT,
    build_kit,
    load_submissions,
    release_identity,
    release_tokens,
)  # noqa: E402
from release_common import (  # noqa: E402
    ReleaseContext,
    ReleaseError,
    command,
    http_json,
    load_context,
    result,
    sha256_bytes,
    sha256_file,
)

PLAYBOOK = Path(".agents/skills/release/references/browser-publication.md")
KDE_RECORD = "2376676"
KDE_LISTING_URL = f"https://store.kde.org/p/{KDE_RECORD}"
KDE_OCS_URL = f"https://api.kde-look.org/ocs/v1/content/data/{KDE_RECORD}"
KDE_MIRRORS = (
    f"https://www.opendesktop.org/p/{KDE_RECORD}/",
    f"https://www.pling.com/p/{KDE_RECORD}/",
    f"https://www.linux-apps.org/p/{KDE_RECORD}/",
)
ALTERNATIVETO_ITEM = "9ea8dc3b-7a8b-4570-837f-9d9f2ef98af1"
ALTERNATIVETO_URL = "https://alternativeto.net/software/krema/"
ALTERNATIVETO_LATTE_URL = "https://alternativeto.net/software/latte-dock/"
LAUNCHPAD_PROJECT_API = "https://api.launchpad.net/1.0/krema"
LAUNCHPAD_OWNER = "https://api.launchpad.net/1.0/~isac322"
LAUNCHPAD_BRANDING = (
    ("project-icon", "icon_link"),
    ("project-logo", "logo_link"),
    ("project-brand", "brand_link"),
)
EXPECTED_HOMEPAGE = "https://krema.bhyoo.com/"
CATALOG_PULLS = {
    "awesome-kde": ("francoism90/awesome-kde", 24),
    "awesome-wayland": ("rcalixte/awesome-wayland", 109),
    "nixpkgs": ("NixOS/nixpkgs", 570731),
}
LINUXLINKS_SEARCH = "https://www.linuxlinks.com/?s=krema"
LINUXLINKS_SUBMITTED = "2026-10-06"
LINUXLINKS_LISTING_RE = re.compile(
    r'href="(https://www\.linuxlinks\.com/(?!search/|tag/|category/|page/)[^"#?]*krema[^"#?]*)"',
    re.IGNORECASE,
)
BROWSER_STORES = ("kde-store", "alternativeto")
STORES = (
    "kde-store",
    "alternativeto",
    "launchpad",
    "github",
    "awesome-kde",
    "awesome-wayland",
    "nixpkgs",
    "linuxlinks",
)
RECORD_STATES = ("start", "pending", "published", "abort")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
HTML_TAG_RE = re.compile(r"<[^>]*>")
MAX_FETCH_BYTES = 8 * 1024 * 1024
USER_AGENT = "Krema-release-stores/1.0"


def http_bytes(url: str, *, timeout: int = 30) -> tuple[int, bytes]:
    """GET a public URL and return the HTTP status and a bounded body."""
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read(MAX_FETCH_BYTES + 1)
            status = response.status
    except urllib.error.HTTPError as exc:
        return exc.code, b""
    except (OSError, urllib.error.URLError, ValueError) as exc:
        raise ReleaseError(f"GET {url} failed: {exc}") from None
    if len(body) > MAX_FETCH_BYTES:
        raise ReleaseError(f"GET {url} returned more than {MAX_FETCH_BYTES} bytes")
    return status, body


def archive_md5(context: ReleaseContext) -> str:
    return hashlib.md5(
        Path(context.archive).read_bytes(), usedforsecurity=False
    ).hexdigest()


def now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def receipts_dir(context: ReleaseContext) -> Path:
    return Path(context.work_dir) / "store-receipts"


def read_receipt(context: ReleaseContext, store: str) -> dict[str, Any] | None:
    path = receipts_dir(context) / f"{store}.json"
    if not path.exists():
        return None
    try:
        receipt = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ReleaseError(f"cannot read store receipt {path}: {exc}") from None
    if not isinstance(receipt, dict):
        raise ReleaseError(f"store receipt is not an object: {path}")
    for key, expected in (
        ("tag", context.tag),
        ("commit", context.commit),
        ("source_sha256", context.sha256.lower()),
    ):
        if receipt.get(key) != expected:
            raise ReleaseError(
                f"store receipt {path} records {key}={receipt.get(key)!r}, not this release"
            )
    return receipt


def read_lock(context: ReleaseContext, store: str) -> dict[str, Any] | None:
    path = receipts_dir(context) / f"{store}.lock"
    if not path.exists():
        return None
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ReleaseError(
            f"cannot read store lock {path}: {exc}; remove it by hand only after confirming no browser session is using it"
        ) from None
    return lock if isinstance(lock, dict) else {}


def check_lock_owner(context: ReleaseContext, store: str, lock: dict[str, Any]) -> None:
    """Refuse a lock that was taken for another store or another release."""
    path = receipts_dir(context) / f"{store}.lock"
    for key, expected in (
        ("store", store),
        ("tag", context.tag),
        ("commit", context.commit),
        ("source_sha256", context.sha256.lower()),
    ):
        if lock.get(key) != expected:
            raise ReleaseError(
                f"store lock {path} records {key}={lock.get(key)!r}, not this release; release it with the context that took it"
            )


def take_lock(path: Path, payload: dict[str, Any]) -> None:
    """Create ``path`` with its complete content in one step; raise FileExistsError if it exists."""
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    with temporary.open("x", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    try:
        os.link(temporary, path)
    finally:
        temporary.unlink()


def verified_receipt(
    receipt: dict[str, Any] | None, **expected: str
) -> dict[str, Any] | None:
    """Return the verification of a published receipt that a public read confirmed and that binds ``expected``."""
    if receipt is None or receipt.get("state") != "published":
        return None
    verification = receipt.get("verification")
    if (
        not isinstance(verification, dict)
        or verification.get("method") != "public-read"
    ):
        return None
    if any(verification.get(key) != value for key, value in expected.items()):
        return None
    return verification


def lock_pending(
    store: str, lock: dict[str, Any], extra: dict[str, Any]
) -> dict[str, Any]:
    return result(
        store,
        "pending",
        f"a browser session holds the {store} lock since {lock.get('started_at')}; do not start another",
        **extra,
    )


def ocs_record() -> dict[str, str]:
    status, body = http_bytes(KDE_OCS_URL)
    if status != 200:
        raise ReleaseError(f"KDE OCS record returned HTTP {status}")
    try:
        root = ET.fromstring(body)
    except ET.ParseError as exc:
        raise ReleaseError(f"KDE OCS record is not valid XML: {exc}") from None
    if root.findtext("meta/statuscode") != "100":
        raise ReleaseError(
            f"KDE OCS record status is {root.findtext('meta/statuscode')!r}"
        )
    content = root.find("data/content")
    if content is None or content.findtext("id") != KDE_RECORD:
        raise ReleaseError(f"KDE OCS response is not content record {KDE_RECORD}")
    return {child.tag: (child.text or "").strip() for child in content}


def kde_observe(context: ReleaseContext) -> dict[str, str]:
    """Compare the public OCS record with the verified archive.

    ``state`` is ``match`` (version and file MD5 are live), ``mismatch`` (the
    release file name is live with other bytes), or ``absent``.
    """
    filename = release_tokens(context)["source_filename"]
    record = ocs_record()
    expected_md5 = archive_md5(context)
    slots = sorted(
        int(key[len("downloadname") :])
        for key in record
        if re.fullmatch(r"downloadname[0-9]+", key)
    )
    named = [slot for slot in slots if record.get(f"downloadname{slot}") == filename]
    live_version = record.get("version", "")
    if named and not any(
        record.get(f"downloadmd5sum{slot}", "").lower() == expected_md5
        for slot in named
    ):
        state = "mismatch"
    elif named and live_version == context.version:
        state = "match"
    else:
        state = "absent"
    return {
        "state": state,
        "live_version": live_version,
        "filename": filename,
        "archive_md5": expected_md5,
    }


def kde_store_status(context: ReleaseContext) -> dict[str, Any]:
    extra: dict[str, Any] = {"record": KDE_RECORD, "listing_url": KDE_LISTING_URL}
    lock = read_lock(context, "kde-store")
    if lock is not None:
        return lock_pending("kde-store", lock, extra)
    receipt = read_receipt(context, "kde-store")
    pending = receipt is not None and receipt.get("state") == "pending"
    live = kde_observe(context)
    extra["live_version"] = live["live_version"]
    filename = live["filename"]
    if live["state"] == "mismatch":
        detail = f"record {KDE_RECORD} lists {filename} but its MD5 does not match the verified archive ({live['archive_md5']}); replace the file in the browser"
        if pending:
            detail += "; first withdraw the pending receipt with record --state abort --reference <evidence>"
        return result(
            "kde-store",
            "browser_required",
            detail,
            playbook=PLAYBOOK.as_posix(),
            **extra,
        )
    if pending:
        detail = f"submitted {receipt.get('updated_at')}; waiting for moderation or propagation; do not upload again"
        if live["state"] == "match":
            detail += "; the public record now matches, so record --state published to close the receipt"
        return result(
            "kde-store", "pending", detail, reference=receipt.get("reference"), **extra
        )
    if live["state"] == "match":
        return result(
            "kde-store",
            "already_published",
            f"record {KDE_RECORD} shows {context.version} with {filename} matching the verified archive",
            **extra,
        )
    verification = verified_receipt(receipt, archive_md5=live["archive_md5"])
    if verification is not None:
        return result(
            "kde-store",
            "already_published",
            f"a public read verified {context.version} with {filename} at {verification.get('observed_at')}; "
            f"the OCS record now shows version {live['live_version'] or 'none'} (cache lag), so do not upload again",
            reference=receipt.get("reference"),
            **extra,
        )
    return result(
        "kde-store",
        "browser_required",
        f"record {KDE_RECORD} shows version {live['live_version'] or 'none'}; update it to {context.version} with {filename} using the browser playbook",
        playbook=PLAYBOOK.as_posix(),
        **extra,
    )


def normalize_text(text: str) -> str:
    return " ".join(html.unescape(text).split())


def page_text(body: bytes) -> str:
    """Visible text of an HTML page with tags removed and whitespace collapsed."""
    return normalize_text(HTML_TAG_RE.sub(" ", body.decode("utf-8", "replace")))


def alternativeto_expected(context: ReleaseContext) -> dict[str, Any]:
    """The packet description for this release as normalized paragraphs and their digest."""
    submissions, _, _ = load_submissions(context)
    description = submissions["alternativeto"][2]
    paragraphs = [
        normalize_text(part)
        for part in re.split(r"\n\s*\n", description)
        if part.strip()
    ]
    if not paragraphs:
        raise ReleaseError("the AlternativeTo packet description is empty")
    return {
        "paragraphs": paragraphs,
        "description_sha256": sha256_bytes("\n".join(paragraphs).encode("utf-8")),
    }


def alternativeto_observe(context: ReleaseContext) -> dict[str, Any]:
    """Read the public listing and Latte Dock page and compare them with the packet description.

    ``observable`` is false when either page refuses a non-browser read; that
    is never evidence of publication.
    """
    expected = alternativeto_expected(context)
    listing_status, listing = http_bytes(ALTERNATIVETO_URL)
    if listing_status != 200:
        return {
            "observable": False,
            "http_status": listing_status,
            "expected": expected,
        }
    relation_status, relation = http_bytes(ALTERNATIVETO_LATTE_URL)
    if relation_status != 200:
        return {
            "observable": False,
            "http_status": relation_status,
            "expected": expected,
        }
    problems = []
    if ALTERNATIVETO_ITEM.encode("ascii") not in listing:
        problems.append(f"the listing page does not carry item {ALTERNATIVETO_ITEM}")
    text = page_text(listing)
    missing = [
        str(index)
        for index, paragraph in enumerate(expected["paragraphs"], 1)
        if paragraph not in text
    ]
    if missing:
        problems.append(
            f"description paragraph(s) {', '.join(missing)} of the packet are not on the public listing"
        )
    if b"/software/krema/" not in relation:
        problems.append("the Latte Dock page does not list Krema as an alternative")
    return {
        "observable": True,
        "http_status": 200,
        "problems": problems,
        "expected": expected,
    }


def alternativeto_status(context: ReleaseContext) -> dict[str, Any]:
    extra = {
        "item_id": ALTERNATIVETO_ITEM,
        "listing_url": ALTERNATIVETO_URL,
        "relation_url": ALTERNATIVETO_LATTE_URL,
    }
    lock = read_lock(context, "alternativeto")
    if lock is not None:
        return lock_pending("alternativeto", lock, extra)
    receipt = read_receipt(context, "alternativeto")
    if receipt is not None and receipt.get("state") == "pending":
        return result(
            "alternativeto",
            "pending",
            f"submitted {receipt.get('updated_at')}; waiting for moderation; do not submit again, and record --state published once the public listing shows the packet description",
            reference=receipt.get("reference"),
            **extra,
        )
    observed = alternativeto_observe(context)
    if observed["observable"] and not observed["problems"]:
        return result(
            "alternativeto",
            "already_published",
            "the public listing shows this release's packet description and item ID, and the Latte Dock page lists Krema",
            **extra,
        )
    if observed["observable"]:
        return result(
            "alternativeto",
            "browser_required",
            "; ".join(observed["problems"]) + "; update it with the browser playbook",
            playbook=PLAYBOOK.as_posix(),
            **extra,
        )
    verification = verified_receipt(
        receipt, description_sha256=observed["expected"]["description_sha256"]
    )
    if verification is not None and observed["http_status"] == 403:
        return result(
            "alternativeto",
            "already_published",
            f"a public read verified the packet description at {verification.get('observed_at')}; the current read returned HTTP {observed['http_status']}",
            reference=receipt.get("reference"),
            **extra,
        )
    return result(
        "alternativeto",
        "browser_required",
        f"AlternativeTo refused a non-browser read (HTTP {observed['http_status']}), so publication cannot be observed; compare the listing with the packet in the browser playbook",
        playbook=PLAYBOOK.as_posix(),
        **extra,
    )


def launchpad_status(context: ReleaseContext) -> dict[str, Any]:
    submissions, _, _ = load_submissions(context)
    schema = submissions["launchpad"][0]
    project = http_json(LAUNCHPAD_PROJECT_API)
    if not isinstance(project, dict):
        raise ReleaseError("Launchpad project resource is not an object")
    problems = []
    if project.get("name") != "krema" or project.get("owner_link") != LAUNCHPAD_OWNER:
        problems.append("project name or owner differs from krema/~isac322")
    if project.get("information_type") != "Public":
        problems.append(f"information type is {project.get('information_type')!r}")
    media = {item["role"]: item for item in schema["media"]}
    for role, link_key in LAUNCHPAD_BRANDING:
        item = media.get(role)
        link = project.get(link_key)
        if (
            item is None
            or not isinstance(link, str)
            or not link.startswith("https://api.launchpad.net/")
        ):
            problems.append(f"{role} has no recorded asset or live link")
            continue
        local = sha256_file(schema["_media_files"][item["source"]])
        if local != item["uploaded_sha256"]:
            problems.append(
                f"{role} repository file no longer matches the published receipt"
            )
        status, body = http_bytes(link)
        if status != 200 or sha256_bytes(body) != item["uploaded_sha256"]:
            problems.append(f"{role} live image differs from the published receipt")
    if problems:
        return result(
            "launchpad",
            "failed",
            "; ".join(problems),
            project_url="https://launchpad.net/krema",
        )
    return result(
        "launchpad",
        "already_published",
        "project, owner, and static icon/logo/brand match the published receipts; nothing to upload for a version bump",
        project_url="https://launchpad.net/krema",
    )


def github_status(context: ReleaseContext) -> dict[str, Any]:
    owner, name = context.repository.split("/", 1)
    repo = http_json(
        f"https://api.github.com/repos/{context.repository}",
        headers={"Accept": "application/vnd.github+json"},
    )
    if not isinstance(repo, dict):
        raise ReleaseError("GitHub repository resource is not an object")
    query = "query($o:String!,$n:String!){repository(owner:$o,name:$n){usesCustomOpenGraphImage openGraphImageUrl}}"
    try:
        graph = json.loads(
            command(
                [
                    "gh",
                    "api",
                    "graphql",
                    "-f",
                    f"query={query}",
                    "-f",
                    f"o={owner}",
                    "-f",
                    f"n={name}",
                ]
            )
        )
    except json.JSONDecodeError as exc:
        raise ReleaseError(f"gh api graphql returned invalid JSON: {exc}") from None
    if not isinstance(graph, dict):
        raise ReleaseError("gh api graphql returned a non-object response")
    errors = graph.get("errors")
    if errors:
        messages = [
            str(item.get("message")) if isinstance(item, dict) else str(item)
            for item in (errors if isinstance(errors, list) else [errors])
        ]
        raise ReleaseError(f"GitHub GraphQL returned errors: {'; '.join(messages)}")
    data = graph.get("data")
    preview = data.get("repository") if isinstance(data, dict) else None
    if not isinstance(preview, dict):
        raise ReleaseError(
            f"GitHub GraphQL returned no repository data for {context.repository}"
        )
    problems = []
    if repo.get("homepage") != EXPECTED_HOMEPAGE:
        problems.append(f"About homepage is {repo.get('homepage')!r}")
    if not repo.get("description"):
        problems.append("About description is empty")
    if preview.get("usesCustomOpenGraphImage") is not True:
        problems.append("no custom social preview image is set")
    extra = {
        "topics": repo.get("topics", []),
        "social_preview_url": preview.get("openGraphImageUrl"),
    }
    if problems:
        return result(
            "github",
            "browser_required",
            "; ".join(problems)
            + "; restore it in the repository settings (static branding, not a per-release task)",
            **extra,
        )
    return result(
        "github",
        "already_published",
        "About section and custom social preview are set; static branding is retained",
        **extra,
    )


def catalog_pull_status(store: str) -> dict[str, Any]:
    repository, number = CATALOG_PULLS[store]
    url = f"https://github.com/{repository}/pull/{number}"
    pull = http_json(
        f"https://api.github.com/repos/{repository}/pulls/{number}",
        headers={"Accept": "application/vnd.github+json"},
    )
    if not isinstance(pull, dict):
        raise ReleaseError(f"{url} resource is not an object")
    if pull.get("merged_at"):
        detail = f"one-time entry merged {pull['merged_at']}"
        if store == "nixpkgs":
            detail += "; later version updates follow nixpkgs' own update process"
        return result(store, "already_published", detail, pull_request=url)
    if pull.get("state") == "open":
        return result(
            store,
            "pending",
            "one-time entry waiting on the maintainer; not resubmitted per release",
            pull_request=url,
        )
    return result(
        store,
        "blocked",
        "pull request closed without merge; a new registration needs the user's approval",
        pull_request=url,
    )


def linuxlinks_status() -> dict[str, Any]:
    status, body = http_bytes(LINUXLINKS_SEARCH)
    if status != 200:
        raise ReleaseError(f"LinuxLinks search returned HTTP {status}")
    match = LINUXLINKS_LISTING_RE.search(body.decode("utf-8", "replace"))
    if match:
        return result(
            "linuxlinks",
            "already_published",
            "one-time listing found",
            listing_url=match.group(1),
        )
    return result(
        "linuxlinks",
        "pending",
        f"submitted {LINUXLINKS_SUBMITTED} with a correction request about its AI-use answer; no listing found yet",
        search_url=LINUXLINKS_SEARCH,
    )


def status(context: ReleaseContext, stores: list[str]) -> list[dict[str, Any]]:
    checks: dict[str, Callable[[], dict[str, Any]]] = {
        "kde-store": lambda: kde_store_status(context),
        "alternativeto": lambda: alternativeto_status(context),
        "launchpad": lambda: launchpad_status(context),
        "github": lambda: github_status(context),
        "awesome-kde": lambda: catalog_pull_status("awesome-kde"),
        "awesome-wayland": lambda: catalog_pull_status("awesome-wayland"),
        "nixpkgs": lambda: catalog_pull_status("nixpkgs"),
        "linuxlinks": linuxlinks_status,
    }
    results = []
    for store in stores:
        try:
            results.append(checks[store]())
        except ReleaseError as exc:
            results.append(
                result(store, "blocked", f"read-only status check failed: {exc}")
            )
    return results


def channel_content_sha256(kit_dir: Path, channel: str) -> str:
    """Hash a channel's packet files so a repeated browser task can detect identical content."""
    lines = [
        line
        for line in (kit_dir / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
        if line.split("  ", 1)[-1].startswith(f"{channel}/")
    ]
    if not lines:
        raise ReleaseError(f"kit/SHA256SUMS lists no {channel}/ files")
    return sha256_bytes(("\n".join(lines) + "\n").encode("utf-8"))


def browser_tasks(
    context: ReleaseContext, built: dict[str, Any], kit_dir: Path
) -> list[dict[str, Any]]:
    tokens = release_tokens(context)
    source = built["kit"]["source"]
    kde = built["submissions"]["kde-store"]
    alt = built["submissions"]["alternativeto"]
    templates = built["kit"]["templates"]
    return [
        result(
            "kde-store",
            "browser_required",
            f"update existing record {KDE_RECORD} to {context.version} and replace its source download with {source['filename']}",
            packet="kit/kde-store",
            record=KDE_RECORD,
            listing_url=KDE_LISTING_URL,
            ocs_url=KDE_OCS_URL,
            mirrors=list(KDE_MIRRORS),
            sign_in="GitHub as isac322 first, then OpenDesktop 'Login with GitHub'",
            version=context.version,
            source_filename=source["filename"],
            source_label=source["label"],
            source_sha256=tokens["source_sha256"],
            source_md5=archive_md5(context),
            media=[item["filename"] for item in kde["media"]],
            content_sha256=channel_content_sha256(kit_dir, "kde-store"),
            templates=templates["source"],
            claims_review_required=templates["human_review_required"],
            done_when=f"status --store kde-store reports already_published (OCS version {context.version}, {source['filename']} MD5 match)",
        ),
        result(
            "alternativeto",
            "browser_required",
            "review the existing listing and Latte Dock relationship; edit only if the packet content differs from the last applied packet",
            packet="kit/alternativeto",
            item_id=ALTERNATIVETO_ITEM,
            listing_url=ALTERNATIVETO_URL,
            relation_target=alt["relation"]["target_name"],
            relation_url=alt["relation"]["entry_url"],
            sign_in="existing AlternativeTo session in Camofox",
            content_sha256=channel_content_sha256(kit_dir, "alternativeto"),
            templates=templates["source"],
            claims_review_required=templates["human_review_required"],
            conditional=True,
            done_when="status --store alternativeto reports already_published (public listing shows this packet's description and item ID, Latte Dock page lists Krema); otherwise record pending with the moderation reference",
        ),
    ]


def static_surfaces() -> list[dict[str, Any]]:
    one_time = "one-time registration; not resubmitted per release; check with status"
    return [
        result(
            "launchpad",
            "not_applicable",
            "static project branding is retained on version bumps; check with status",
        ),
        result(
            "github",
            "not_applicable",
            "static About section and social preview are retained on version bumps; check with status",
        ),
        *(
            result(
                store,
                "not_applicable",
                one_time,
                pull_request=f"https://github.com/{repo}/pull/{number}",
            )
            for store, (repo, number) in CATALOG_PULLS.items()
        ),
        result("linuxlinks", "not_applicable", one_time, search_url=LINUXLINKS_SEARCH),
    ]


def render_tasks_markdown(
    release: dict[str, str],
    tasks: list[dict[str, Any]],
    static: list[dict[str, Any]],
    playbook_sha: str,
) -> str:
    lines = [
        f"# Browser tasks for Krema {release['tag']}",
        "",
        f"Release: `{release['repository']}` `{release['tag']}` at `{release['commit']}`.",
        f"Playbook: `{PLAYBOOK.as_posix()}` (sha256 `{playbook_sha}`). Follow it exactly; it covers sign-in, locks, idempotence, and moderation.",
        "",
        "Every task below is `browser_required`. Nothing has been submitted. Run `release_stores.py status` first and skip a task that already reports `already_published` or `pending`.",
        "",
    ]
    for task in tasks:
        lines.append(f"## {task['channel']}: `{task['status']}`")
        lines.append("")
        lines.append(task["detail"] + ".")
        lines.append("")
        for key, value in task.items():
            if key in {"channel", "status", "detail"}:
                continue
            rendered = (
                ", ".join(f"`{item}`" for item in value)
                if isinstance(value, list)
                else f"`{value}`"
            )
            lines.append(f"- {key}: {rendered}")
        lines.append("")
    lines.extend(
        [
            "## Static and one-time surfaces",
            "",
            "| Surface | Status | Note |",
            "|---|---|---|",
        ]
    )
    lines.extend(
        f"| {item['channel']} | `{item['status']}` | {item['detail']} |"
        for item in static
    )
    return "\n".join(lines) + "\n"


def write_new(path: Path, payload: bytes) -> None:
    with path.open("xb") as handle:
        handle.write(payload)


def prepare(context: ReleaseContext, output: Path) -> dict[str, Any]:
    output = output.expanduser().resolve()
    try:
        output.relative_to(ROOT)
    except ValueError:
        pass
    else:
        raise ReleaseError(f"output must be outside the repository: {output}")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ReleaseError(
            f"refusing to use a non-empty or non-directory output: {output}"
        )
    playbook = ROOT / PLAYBOOK
    if not playbook.is_file():
        raise ReleaseError(f"browser playbook is missing: {PLAYBOOK.as_posix()}")
    playbook_sha = sha256_file(playbook)
    output.mkdir(parents=True, exist_ok=True)
    kit_dir = output / "kit"
    built = build_kit(context, kit_dir)
    tasks = browser_tasks(context, built, kit_dir)
    static = static_surfaces()
    manifest = {
        "schema_version": 1,
        "release": built["kit"]["release"],
        "schema_baseline": built["kit"]["schema_baseline"],
        "templates": built["kit"]["templates"],
        "kit": {
            "path": "kit",
            "zip": "kit.zip",
            "sha256sums_sha256": sha256_file(kit_dir / "SHA256SUMS"),
            "zip_sha256": sha256_file(built["zip"]),
            "source": built["kit"]["source"],
        },
        "playbook": {"path": PLAYBOOK.as_posix(), "sha256": playbook_sha},
        "browser_tasks": tasks,
        "static_surfaces": static,
        "publication_state": "prepared; no external submission performed",
    }
    write_new(
        output / "stores.json",
        (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8"),
    )
    write_new(
        output / "BROWSER-TASKS.md",
        render_tasks_markdown(manifest["release"], tasks, static, playbook_sha).encode(
            "utf-8"
        ),
    )
    return manifest


def safe_relative(name: str) -> bool:
    return (
        bool(name)
        and not name.startswith("/")
        and "\\" not in name
        and all(part not in {"", ".", ".."} for part in name.split("/"))
    )


def verify_kit_files(kit_dir: Path) -> None:
    """Recompute every kit file's SHA-256 against kit/SHA256SUMS; refuse missing, unlisted, or linked files."""
    listed: dict[str, str] = {}
    for number, line in enumerate(
        (kit_dir / "SHA256SUMS").read_text(encoding="utf-8").splitlines(), 1
    ):
        digest, separator, name = line.partition("  ")
        if (
            not separator
            or not SHA256_RE.fullmatch(digest)
            or not safe_relative(name)
            or name == "SHA256SUMS"
            or name in listed
        ):
            raise ReleaseError(f"{kit_dir / 'SHA256SUMS'} line {number} is malformed")
        listed[name] = digest
    present: dict[str, Path] = {}
    for path in sorted(kit_dir.rglob("*")):
        relative = path.relative_to(kit_dir).as_posix()
        if path.is_symlink():
            raise ReleaseError(f"packet kit contains a symlink: {relative}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise ReleaseError(f"packet kit contains a special file: {relative}")
        if relative != "SHA256SUMS":
            present[relative] = path
    missing = sorted(set(listed) - set(present))
    unlisted = sorted(set(present) - set(listed))
    if missing or unlisted:
        raise ReleaseError(
            f"packet kit files differ from SHA256SUMS: missing {missing}, unlisted {unlisted}"
        )
    changed = [
        name for name, path in present.items() if sha256_file(path) != listed[name]
    ]
    if changed:
        raise ReleaseError(
            f"packet kit files changed after preparation: {', '.join(changed)}"
        )


def _load_packet(context: ReleaseContext, packet: Path) -> dict[str, Any]:
    manifest = json.loads((packet / "stores.json").read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ReleaseError(f"{packet / 'stores.json'} is not a schema 1 store manifest")
    expected = release_identity(context)
    if manifest.get("release") != expected:
        raise ReleaseError(
            f"packet {packet} was prepared for {manifest.get('release')}, not {expected}"
        )
    kit = manifest["kit"]
    source_sha256 = context.sha256.lower()
    if (
        kit["path"] != "kit"
        or kit["zip"] != "kit.zip"
        or kit["source"]["sha256"] != source_sha256
    ):
        raise ReleaseError(
            f"packet {packet} kit entry does not describe this release's source"
        )
    kit_dir = packet / "kit"
    zip_path = packet / "kit.zip"
    if kit_dir.is_symlink() or zip_path.is_symlink():
        raise ReleaseError(f"packet {packet} kit or kit.zip is a symlink")
    if sha256_file(kit_dir / "SHA256SUMS") != kit["sha256sums_sha256"]:
        raise ReleaseError(f"packet {packet} kit/SHA256SUMS changed after preparation")
    verify_kit_files(kit_dir)
    if sha256_file(zip_path) != kit["zip_sha256"]:
        raise ReleaseError(f"packet {packet} kit.zip changed after preparation")
    kit_info = json.loads((kit_dir / "kit.json").read_text(encoding="utf-8"))
    if (
        kit_info["release"] != expected
        or kit_info["source"] != kit["source"]
        or kit_info["templates"] != manifest["templates"]
    ):
        raise ReleaseError(
            f"packet {packet} kit/kit.json does not match stores.json and the context"
        )
    tasks = manifest["browser_tasks"]
    if sorted(task["channel"] for task in tasks) != sorted(BROWSER_STORES):
        raise ReleaseError(
            f"packet {packet} must hold exactly one browser task per store: {', '.join(BROWSER_STORES)}"
        )
    for task in tasks:
        if task["content_sha256"] != channel_content_sha256(kit_dir, task["channel"]):
            raise ReleaseError(
                f"packet {packet} {task['channel']} task content_sha256 does not match its kit files"
            )
        if (
            task["claims_review_required"]
            != manifest["templates"]["human_review_required"]
        ):
            raise ReleaseError(
                f"packet {packet} {task['channel']} task hides the template review flag"
            )
    kde = next(task for task in tasks if task["channel"] == "kde-store")
    if (
        kde["version"],
        kde["source_sha256"],
        kde["source_filename"],
        kde["source_md5"],
    ) != (
        context.version,
        source_sha256,
        kit["source"]["filename"],
        archive_md5(context),
    ):
        raise ReleaseError(
            f"packet {packet} KDE Store task does not match the context archive"
        )
    return manifest


def load_packet(context: ReleaseContext, packet: Path) -> dict[str, Any]:
    """Load stores.json and re-verify every packet file and binding against the context."""
    try:
        return _load_packet(context, packet)
    except (
        OSError,
        UnicodeError,
        ValueError,
        KeyError,
        TypeError,
        AttributeError,
        StopIteration,
    ) as exc:
        raise ReleaseError(
            f"packet {packet} is unreadable or malformed: {exc!r}"
        ) from None


def atomic_write_json(path: Path, value: dict[str, Any]) -> None:
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    with temporary.open("x", encoding="utf-8") as handle:
        json.dump(value, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def receipt_history(existing: dict[str, Any] | None) -> list[Any]:
    if existing is None:
        return []
    history = existing.get("history")
    previous = {
        key: existing.get(key)
        for key in ("state", "reference", "content_sha256", "updated_at")
    }
    return [*(history if isinstance(history, list) else []), previous]


def verify_published(
    context: ReleaseContext, store: str, reference: str | None
) -> tuple[str, dict[str, Any]]:
    """Re-read the public store and return the canonical reference and the observation, or refuse."""
    canonical = KDE_LISTING_URL if store == "kde-store" else ALTERNATIVETO_URL
    if reference not in (None, canonical):
        raise ReleaseError(
            f"recording {store} as published takes the public listing {canonical} as --reference; record other references as pending"
        )
    if store == "kde-store":
        live = kde_observe(context)
        if live["state"] != "match":
            raise ReleaseError(
                f"KDE Store record {KDE_RECORD} does not show {context.version} with the verified {live['filename']} "
                f"(state {live['state']}, version {live['live_version'] or 'none'}); record pending instead"
            )
        return canonical, {
            "method": "public-read",
            "observed_at": now(),
            "url": KDE_OCS_URL,
            "live_version": live["live_version"],
            "archive_md5": live["archive_md5"],
        }
    observed = alternativeto_observe(context)
    if not observed["observable"]:
        raise ReleaseError(
            f"AlternativeTo refused a non-browser read (HTTP {observed['http_status']}), so publication cannot be verified; "
            "keep or record the submission as pending with the browser observation as --reference"
        )
    if observed["problems"]:
        raise ReleaseError(
            "AlternativeTo does not show this release's packet content yet: "
            + "; ".join(observed["problems"])
        )
    return canonical, {
        "method": "public-read",
        "observed_at": now(),
        "url": ALTERNATIVETO_URL,
        "description_sha256": observed["expected"]["description_sha256"],
    }


def abort(context: ReleaseContext, store: str, reference: str | None) -> dict[str, Any]:
    """Release this release's lock, or withdraw a pending receipt; never reads the packet."""
    directory = receipts_dir(context)
    lock_path = directory / f"{store}.lock"
    receipt_path = directory / f"{store}.json"
    lock = read_lock(context, store)
    if lock is not None:
        check_lock_owner(context, store, lock)
        lock_path.unlink()
        return result(
            store,
            "browser_required",
            "lock released without a change",
            lock=str(lock_path),
        )
    existing = read_receipt(context, store)
    if existing is None or existing.get("state") != "pending":
        raise ReleaseError(f"no {store} lock or pending receipt to abort")
    if not reference or not reference.strip():
        raise ReleaseError(
            "withdrawing a pending submission requires --reference with the evidence that the store rejected or never received it"
        )
    atomic_write_json(
        receipt_path,
        {
            **existing,
            "state": "withdrawn",
            "reference": reference,
            "updated_at": now(),
            "history": receipt_history(existing),
        },
    )
    return result(
        store,
        "browser_required",
        "pending receipt withdrawn; run status, and start a new session only if the store still needs the update",
        reference=reference,
        receipt=str(receipt_path),
    )


def record(
    context: ReleaseContext,
    packet: Path | None,
    store: str,
    state: str,
    reference: str | None,
) -> dict[str, Any]:
    """Take, update, or release the local lock and receipt for one browser task."""
    if store not in BROWSER_STORES or state not in RECORD_STATES:
        raise ReleaseError(
            f"record supports stores {BROWSER_STORES} and states {RECORD_STATES}"
        )
    if state == "abort":
        return abort(context, store, reference)
    if packet is None:
        raise ReleaseError(
            f"recording {state} requires --packet with the prepare output"
        )
    manifest = load_packet(context, packet)
    content = next(
        item for item in manifest["browser_tasks"] if item["channel"] == store
    )["content_sha256"]
    directory = receipts_dir(context)
    directory.mkdir(parents=True, exist_ok=True)
    lock_path = directory / f"{store}.lock"
    receipt_path = directory / f"{store}.json"
    identity = {
        "store": store,
        "tag": context.tag,
        "commit": context.commit,
        "source_sha256": context.sha256.lower(),
        "content_sha256": content,
    }
    existing = read_receipt(context, store)
    pending = existing is not None and existing.get("state") == "pending"
    if state == "start":
        if pending:
            raise ReleaseError(
                f"{store} has a pending submission ({existing.get('reference')}, {existing.get('updated_at')}); do not upload again. "
                "Update its reference with --state pending, close it with --state published once it is public, "
                "or withdraw it with --state abort --reference <evidence>"
            )
        if (
            existing is not None
            and existing.get("state") == "published"
            and existing.get("content_sha256") == content
        ):
            raise ReleaseError(
                f"{store} is already published for this packet; do not repeat the upload"
            )
        try:
            take_lock(lock_path, {**identity, "started_at": now()})
        except FileExistsError:
            lock = read_lock(context, store) or {}
            raise ReleaseError(
                f"{store} lock is held since {lock.get('started_at')}; finish or abort that session first"
            ) from None
        return result(
            store,
            "browser_required",
            "lock taken; perform the playbook steps, then record pending or published",
            lock=str(lock_path),
        )
    lock = read_lock(context, store)
    if lock is not None:
        check_lock_owner(context, store, lock)
        if lock.get("content_sha256") != content:
            raise ReleaseError(
                f"{store} lock was taken for packet content {lock.get('content_sha256')}, not {content}; record with that packet or abort the lock"
            )
    elif pending:
        if existing.get("content_sha256") != content:
            raise ReleaseError(
                f"the pending {store} receipt is for packet content {existing.get('content_sha256')}, not {content}; record with that packet or withdraw it"
            )
    else:
        raise ReleaseError(
            f"recording {state} requires the {store} lock from --state start or an existing pending receipt"
        )
    payload: dict[str, Any] = {**identity, "state": state}
    if state == "pending":
        if not reference or not reference.strip():
            raise ReleaseError(
                "recording pending requires --reference with the moderation or submission reference"
            )
        payload["reference"] = reference
    else:
        payload["reference"], payload["verification"] = verify_published(
            context, store, reference
        )
    payload["updated_at"] = now()
    payload["history"] = receipt_history(existing)
    atomic_write_json(receipt_path, payload)
    if lock is not None:
        lock_path.unlink()
    status_word = "pending" if state == "pending" else "already_published"
    return result(
        store,
        status_word,
        f"recorded {state}",
        reference=payload["reference"],
        receipt=str(receipt_path),
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="action", required=True)
    prep = sub.add_parser(
        "prepare", help="build the store packet and browser task list"
    )
    prep.add_argument("--context", required=True, type=Path)
    prep.add_argument("--output", required=True, type=Path)
    stat = sub.add_parser(
        "status", help="read-only status of store, branding, and catalog surfaces"
    )
    stat.add_argument("--context", required=True, type=Path)
    stat.add_argument(
        "--store",
        action="append",
        choices=STORES,
        help="repeatable; default is every surface",
    )
    rec = sub.add_parser(
        "record", help="take or release the local browser-task lock and receipt"
    )
    rec.add_argument("--context", required=True, type=Path)
    rec.add_argument(
        "--packet",
        type=Path,
        help="output directory of prepare; required except for --state abort",
    )
    rec.add_argument("--store", required=True, choices=BROWSER_STORES)
    rec.add_argument("--state", required=True, choices=RECORD_STATES)
    rec.add_argument(
        "--reference",
        help="pending: submission or moderation reference; published: the public listing URL (default); abort of a pending receipt: rejection evidence",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(sys.argv[1:] if argv is None else argv)
        context = load_context(args.context)
        if args.action == "prepare":
            output_dir = args.output.expanduser().resolve()
            manifest = prepare(context, output_dir)
            output: dict[str, Any] = {
                "results": manifest["browser_tasks"] + manifest["static_surfaces"],
                "packet": str(output_dir),
                "stores_json": str(output_dir / "stores.json"),
                "browser_tasks": str(output_dir / "BROWSER-TASKS.md"),
                "kit": str(output_dir / "kit"),
            }
        elif args.action == "status":
            output = {"results": status(context, args.store or list(STORES))}
        else:
            packet = args.packet.expanduser().resolve() if args.packet else None
            output = {
                "results": [
                    record(context, packet, args.store, args.state, args.reference)
                ]
            }
        print(json.dumps(output, indent=2, ensure_ascii=False))
        return 0
    except ReleaseError as exc:
        print(json.dumps({"error": str(exc)}, ensure_ascii=False))
        print(f"release_stores.py: error: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("release_stores.py: interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
