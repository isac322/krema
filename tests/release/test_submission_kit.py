"""Boundary tests for the store submission kit and the store receipt lifecycle.

Run from the repository root:

    python3 -m unittest discover -s tests/release -p 'test_*.py'
"""

from __future__ import annotations

import hashlib
import html
import io
import json
import shutil
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts"))

import release_stores  # noqa: E402
from prepare_submission_kit import CHANNELS, SCHEMA_DIR, build_kit  # noqa: E402
from release_common import ReleaseContext, ReleaseError, asset_url  # noqa: E402

COMMIT = "0123456789abcdef0123456789abcdef01234567"
OTHER_COMMIT = "fedcba9876543210fedcba9876543210fedcba98"
PNG_LFS_POINTER = (
    b"version https://git-lfs.github.com/spec/v1\noid sha256:"
    + b"0" * 64
    + b"\nsize 1\n"
)


def make_archive(
    version: str,
    *,
    cmake_version: str | None = None,
    commit: str = COMMIT,
    extra: dict[str, bytes] | None = None,
) -> bytes:
    """Return a gzip tarball in the release layout produced by ``git archive``."""
    root = f"krema-{version}"
    files = {
        f"{root}/CMakeLists.txt": f"cmake_minimum_required(VERSION 3.25)\nproject(krema VERSION {cmake_version or version} LANGUAGES CXX)\n".encode(),
        f"{root}/LICENSES/GPL-3.0-or-later.txt": b"GNU GENERAL PUBLIC LICENSE\n",
        f"{root}/src/main.cpp": b"int main() { return 0; }\n",
        **(extra or {}),
    }
    buffer = io.BytesIO()
    with tarfile.open(
        fileobj=buffer,
        mode="w:gz",
        format=tarfile.PAX_FORMAT,
        pax_headers={"comment": commit},
    ) as archive:
        for directory in (root, f"{root}/LICENSES", f"{root}/src"):
            info = tarfile.TarInfo(directory)
            info.type = tarfile.DIRTYPE
            info.mode = 0o755
            archive.addfile(info)
        link = tarfile.TarInfo(f"{root}/LICENSE")
        link.type = tarfile.SYMTYPE
        link.linkname = "LICENSES/GPL-3.0-or-later.txt"
        archive.addfile(link)
        for name, payload in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(payload)
            info.mode = 0o644
            archive.addfile(info, io.BytesIO(payload))
    return buffer.getvalue()


class ReleaseFixture(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="krema-kit-test-"))
        self.addCleanup(shutil.rmtree, self.tmp)

    def tagged_source(self) -> Path:
        """Copy the checked-in templates and their media into a tagged source tree."""
        source = self.tmp / "source"
        (source / SCHEMA_DIR).mkdir(parents=True)
        for channel in CHANNELS:
            schema = REPO / SCHEMA_DIR / f"{channel}.json"
            shutil.copyfile(schema, source / SCHEMA_DIR / schema.name)
            data = json.loads(schema.read_text(encoding="utf-8"))
            shutil.copyfile(
                REPO / data["description_file"], source / data["description_file"]
            )
            for item in data["media"]:
                target = source / item["source"]
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(REPO / item["source"], target)
        return source

    def context(
        self, version: str, payload: bytes, source: Path, *, sha256: str | None = None
    ) -> ReleaseContext:
        archive = self.tmp / f"krema-{version}.tar.gz"
        archive.write_bytes(payload)
        return ReleaseContext(
            repository="isac322/krema",
            tag=f"v{version}",
            version=version,
            commit=COMMIT,
            source_dir=source,
            archive=archive,
            sha256=sha256 or hashlib.sha256(payload).hexdigest(),
            source_url=asset_url("isac322/krema", f"v{version}", version),
            work_dir=self.tmp / "work",
            notes_path=self.tmp / "notes.md",
            debian_revision="1",
        )


class SubmissionKitBoundaryTest(ReleaseFixture):
    def assert_refused(self, context: ReleaseContext) -> None:
        output = self.tmp / "out" / "kit"
        with self.assertRaises(ReleaseError):
            build_kit(context, output)
        self.assertFalse(output.exists(), "a refused kit must not leave output behind")
        self.assertFalse(output.with_name("kit.zip").exists())

    # Version binding

    def test_arbitrary_version_renders_complete_packet(self) -> None:
        payload = make_archive("9.8.7")
        context = self.context("9.8.7", payload, self.tagged_source())
        built = build_kit(context, self.tmp / "out" / "kit")
        kit = built["output"]

        sums = (kit / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(sums), 26)
        for line in sums:
            digest, name = line.split("  ", 1)
            self.assertEqual(
                hashlib.sha256((kit / name).read_bytes()).hexdigest(), digest, name
            )
        with zipfile.ZipFile(built["zip"]) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(len(archive.namelist()), 27)

        info = json.loads((kit / "kit.json").read_text(encoding="utf-8"))
        self.assertEqual(info["release"]["tag"], "v9.8.7")
        self.assertEqual(info["release"]["commit"], COMMIT)
        self.assertEqual(info["schema_baseline"]["tag"], "v0.10.0")
        self.assertEqual(
            info["templates"],
            {"source": "tagged-source", "human_review_required": False},
        )
        self.assertEqual(info["source"]["sha256"], context.sha256)

        staged = kit / "kde-store" / "downloads" / "krema-9.8.7.tar.gz"
        self.assertEqual(staged.read_bytes(), payload)
        self.assertEqual(
            json.loads((kit / "kde-store" / "fields.json").read_text())["Version"],
            "9.8.7",
        )
        description = (kit / "kde-store" / "description.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "https://github.com/isac322/krema/blob/v9.8.7/README.md#build--run",
            description,
        )
        self.assertNotIn("v0.10.0", description)
        for path in kit.rglob("*"):
            if path.suffix in {".txt", ".json"}:
                self.assertNotIn("{{", path.read_text(encoding="utf-8"), path)

        launchpad = json.loads(
            (kit / "launchpad" / "submission.json").read_text(encoding="utf-8")
        )
        self.assertEqual(
            [item["uploaded_url"] for item in launchpad["media"]],
            [
                "https://launchpadlibrarian.net/878876949/launchpad-icon-14.png",
                "https://launchpadlibrarian.net/878876950/launchpad-logo-64.png",
                "https://launchpadlibrarian.net/878876951/launchpad-brand-192.png",
            ],
        )
        self.assertEqual(launchpad["observed_release"]["tag"], "v0.10.0")
        self.assertEqual(launchpad["release"]["tag"], "v9.8.7")

    def test_untagged_templates_fall_back_and_require_review(self) -> None:
        source = self.tmp / "bare-source"
        source.mkdir()
        context = self.context("9.8.7", make_archive("9.8.7"), source)
        built = build_kit(context, self.tmp / "out" / "kit")
        self.assertEqual(
            built["kit"]["templates"],
            {"source": "installed-fallback", "human_review_required": True},
        )
        notes = (built["output"] / "kde-store" / "upload-order.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("installed fallback", notes)

    def test_archive_version_must_match_context(self) -> None:
        context = self.context(
            "9.8.7", make_archive("9.8.7", cmake_version="9.8.6"), self.tagged_source()
        )
        self.assert_refused(context)

    def test_archive_root_must_match_context_version(self) -> None:
        payload = make_archive("9.8.6")
        context = self.context("9.8.7", payload, self.tagged_source())
        self.assert_refused(context)

    def test_archive_commit_must_match_context(self) -> None:
        context = self.context(
            "9.8.7", make_archive("9.8.7", commit=OTHER_COMMIT), self.tagged_source()
        )
        self.assert_refused(context)

    # Hash mismatch

    def test_context_hash_must_match_archive_bytes(self) -> None:
        payload = make_archive("9.8.7")
        context = self.context(
            "9.8.7",
            payload,
            self.tagged_source(),
            sha256=hashlib.sha256(payload + b"x").hexdigest(),
        )
        self.assert_refused(context)

    def test_archive_modified_after_context_is_refused(self) -> None:
        context = self.context("9.8.7", make_archive("9.8.7"), self.tagged_source())
        Path(context.archive).write_bytes(
            make_archive("9.8.7", extra={"krema-9.8.7/src/extra.cpp": b"// changed\n"})
        )
        self.assert_refused(context)

    # Path corruption

    def test_schema_media_path_escape_is_refused(self) -> None:
        source = self.tagged_source()
        schema_path = source / SCHEMA_DIR / "alternativeto.json"
        data = json.loads(schema_path.read_text(encoding="utf-8"))
        data["media"][0]["source"] = "../outside.png"
        schema_path.write_text(json.dumps(data), encoding="utf-8")
        self.assert_refused(self.context("9.8.7", make_archive("9.8.7"), source))

    def test_lfs_pointer_media_is_refused(self) -> None:
        source = self.tagged_source()
        data = json.loads(
            (source / SCHEMA_DIR / "kde-store.json").read_text(encoding="utf-8")
        )
        (source / data["media"][0]["source"]).write_bytes(PNG_LFS_POINTER)
        self.assert_refused(self.context("9.8.7", make_archive("9.8.7"), source))

    def test_lfs_pointer_license_symlink_target_is_refused(self) -> None:
        payload = make_archive(
            "9.8.7",
            extra={"krema-9.8.7/LICENSES/GPL-3.0-or-later.txt": PNG_LFS_POINTER},
        )
        self.assert_refused(self.context("9.8.7", payload, self.tagged_source()))

    def test_lfs_pointer_additional_license_is_refused(self) -> None:
        payload = make_archive(
            "9.8.7", extra={"krema-9.8.7/LICENSES/CC0-1.0.txt": PNG_LFS_POINTER}
        )
        self.assert_refused(self.context("9.8.7", payload, self.tagged_source()))

    def test_unsafe_archive_member_is_refused(self) -> None:
        payload = make_archive(
            "9.8.7", extra={"krema-9.8.7/../escape.txt": b"escape\n"}
        )
        self.assert_refused(self.context("9.8.7", payload, self.tagged_source()))

    def test_incomplete_tagged_template_set_is_refused(self) -> None:
        source = self.tagged_source()
        (source / SCHEMA_DIR / "launchpad.json").unlink()
        self.assert_refused(self.context("9.8.7", make_archive("9.8.7"), source))

    # Token substitution

    def test_unknown_release_token_is_refused(self) -> None:
        source = self.tagged_source()
        schema_path = source / SCHEMA_DIR / "kde-store.json"
        data = json.loads(schema_path.read_text(encoding="utf-8"))
        data["fields"]["Summary"] = "{{bogus}}"
        schema_path.write_text(json.dumps(data), encoding="utf-8")
        self.assert_refused(self.context("9.8.7", make_archive("9.8.7"), source))

    def test_malformed_release_token_is_refused(self) -> None:
        source = self.tagged_source()
        schema_path = source / SCHEMA_DIR / "kde-store.json"
        data = json.loads(schema_path.read_text(encoding="utf-8"))
        data["fields"]["Summary"] = "Krema {{version }"
        schema_path.write_text(json.dumps(data), encoding="utf-8")
        self.assert_refused(self.context("9.8.7", make_archive("9.8.7"), source))


class StoreReceiptLifecycleTest(ReleaseFixture):
    """State transitions of release_stores record/status with the public reads replaced by fixed responses."""

    def setUp(self) -> None:
        super().setUp()
        self.ctx = self.context("9.8.7", make_archive("9.8.7"), self.tagged_source())
        self.packet = self.tmp / "packet"
        release_stores.prepare(self.ctx, self.packet)
        description = (
            self.packet / "kit" / "alternativeto" / "description.txt"
        ).read_text(encoding="utf-8")
        self.paragraphs = [part for part in description.split("\n\n") if part.strip()]
        self.md5 = hashlib.md5(Path(self.ctx.archive).read_bytes()).hexdigest()
        self.receipts = Path(self.ctx.work_dir) / "store-receipts"

    # Helpers

    def record(
        self,
        store: str,
        state: str,
        reference: str | None = None,
        packet: Path | None = None,
    ) -> dict:
        return release_stores.record(
            self.ctx, self.packet if packet is None else packet, store, state, reference
        )

    def status(self, store: str) -> dict:
        return release_stores.status(self.ctx, [store])[0]

    def receipt(self, store: str) -> dict:
        return json.loads((self.receipts / f"{store}.json").read_text(encoding="utf-8"))

    def alternativeto_pages(
        self,
        *,
        paragraphs: list[str] | None = None,
        listing_status: int = 200,
        relation: bool = True,
    ):
        paragraphs = self.paragraphs if paragraphs is None else paragraphs
        # Rendered pages escape entities and reflow whitespace; the comparison must survive both.
        body = "".join(
            f"<p class='d'>\n    {html.escape(part).replace(' ', chr(10) + '  ', 3)}\n</p>"
            for part in paragraphs
        )
        listing = f'<html><body><main data-id="{release_stores.ALTERNATIVETO_ITEM}"><h1>Krema</h1>{body}</main></body></html>'.encode()
        latte = (
            b'<a href="/software/krema/">Krema</a>'
            if relation
            else b"<a href='/software/plank/'>Plank</a>"
        )

        def fetch(url: str, **_: object) -> tuple[int, bytes]:
            if url == release_stores.ALTERNATIVETO_URL:
                return listing_status, listing if listing_status == 200 else b""
            if url == release_stores.ALTERNATIVETO_LATTE_URL:
                return 200, latte
            raise AssertionError(f"unexpected fetch {url}")

        return mock.patch.object(release_stores, "http_bytes", side_effect=fetch)

    def ocs(self, version: str, filename: str | None = None, md5: str | None = None):
        record = {
            "id": release_stores.KDE_RECORD,
            "version": version,
            "downloadname1": filename or f"krema-{version}.tar.gz",
            "downloadmd5sum1": md5 or (self.md5 if version == "9.8.7" else "0" * 32),
        }
        return mock.patch.object(release_stores, "ocs_record", return_value=record)

    # AlternativeTo: lock precedence and content-bound publication

    def test_alternativeto_lock_precedes_matching_public_page(self) -> None:
        self.record("alternativeto", "start")
        with self.alternativeto_pages():
            self.assertEqual(self.status("alternativeto")["status"], "pending")

    def test_alternativeto_matching_public_page_is_published(self) -> None:
        with self.alternativeto_pages():
            self.assertEqual(
                self.status("alternativeto")["status"], "already_published"
            )

    def test_alternativeto_existing_listing_with_other_description_is_not_published(
        self,
    ) -> None:
        stale = ["Krema is a dock.", *self.paragraphs[1:]]
        with self.alternativeto_pages(paragraphs=stale):
            self.assertEqual(self.status("alternativeto")["status"], "browser_required")
            self.record("alternativeto", "start")
            with self.assertRaises(ReleaseError):
                self.record("alternativeto", "published")
        self.assertFalse((self.receipts / "alternativeto.json").exists())
        self.assertTrue((self.receipts / "alternativeto.lock").exists())

    def test_alternativeto_missing_relationship_is_not_published(self) -> None:
        with self.alternativeto_pages(relation=False):
            self.assertEqual(self.status("alternativeto")["status"], "browser_required")

    def test_alternativeto_blocked_read_never_records_published(self) -> None:
        self.record("alternativeto", "start")
        with self.alternativeto_pages(listing_status=403):
            with self.assertRaises(ReleaseError):
                self.record("alternativeto", "published")
            self.assertTrue((self.receipts / "alternativeto.lock").exists())
            self.assertEqual(
                self.record(
                    "alternativeto",
                    "pending",
                    "moderation queue, saved 2026-10-07T00:00Z",
                )["status"],
                "pending",
            )
            self.assertFalse((self.receipts / "alternativeto.lock").exists())
            self.assertEqual(self.status("alternativeto")["status"], "pending")
        with self.alternativeto_pages():
            self.assertEqual(self.status("alternativeto")["status"], "pending")
            self.assertEqual(
                self.record("alternativeto", "published")["status"], "already_published"
            )
        receipt = self.receipt("alternativeto")
        self.assertEqual(
            (receipt["state"], receipt["reference"]),
            ("published", release_stores.ALTERNATIVETO_URL),
        )
        self.assertEqual(receipt["verification"]["method"], "public-read")
        with self.alternativeto_pages(listing_status=403):
            self.assertEqual(
                self.status("alternativeto")["status"], "already_published"
            )
        with self.alternativeto_pages(listing_status=404):
            self.assertEqual(self.status("alternativeto")["status"], "browser_required")
        with self.alternativeto_pages(paragraphs=["Krema is a dock."]):
            self.assertEqual(self.status("alternativeto")["status"], "browser_required")

    def test_alternativeto_published_reference_must_be_canonical_listing(self) -> None:
        self.record("alternativeto", "start")
        with self.alternativeto_pages(), self.assertRaises(ReleaseError):
            self.record(
                "alternativeto", "published", "https://example.com/screenshot.png"
            )
        self.assertFalse((self.receipts / "alternativeto.json").exists())

    # Pending recovery and abort

    def test_pending_receipt_recovers_without_a_second_upload_session(self) -> None:
        self.record("kde-store", "start")
        self.record("kde-store", "pending", "saved 01:00, awaiting refresh")
        with self.assertRaises(ReleaseError):
            self.record("kde-store", "start")
        self.record("kde-store", "pending", "moderation ticket 42")
        receipt = self.receipt("kde-store")
        self.assertEqual(
            (receipt["state"], receipt["reference"]),
            ("pending", "moderation ticket 42"),
        )
        self.assertEqual(
            receipt["history"][-1]["reference"], "saved 01:00, awaiting refresh"
        )
        self.assertFalse((self.receipts / "kde-store.lock").exists())
        with self.assertRaises(ReleaseError):
            self.record("kde-store", "abort")
        self.record("kde-store", "abort", "moderator rejected the upload")
        self.assertEqual(self.receipt("kde-store")["state"], "withdrawn")
        self.record("kde-store", "start")
        self.assertTrue((self.receipts / "kde-store.lock").exists())

    def test_abort_releases_lock_without_a_readable_packet(self) -> None:
        self.record("kde-store", "start")
        shutil.rmtree(self.packet)
        release_stores.record(self.ctx, None, "kde-store", "abort", None)
        self.assertFalse((self.receipts / "kde-store.lock").exists())
        with self.assertRaises(ReleaseError):
            release_stores.record(self.ctx, None, "kde-store", "abort", None)

    def test_abort_refuses_a_lock_owned_by_another_release(self) -> None:
        self.receipts.mkdir(parents=True)
        lock = self.receipts / "kde-store.lock"
        lock.write_text(
            json.dumps(
                {
                    "store": "kde-store",
                    "tag": "v0.0.1",
                    "commit": COMMIT,
                    "source_sha256": self.ctx.sha256,
                }
            ),
            encoding="utf-8",
        )
        with self.assertRaises(ReleaseError):
            release_stores.record(self.ctx, None, "kde-store", "abort", None)
        self.assertTrue(lock.exists())

    def test_record_requires_a_lock_or_pending_receipt(self) -> None:
        with self.assertRaises(ReleaseError):
            self.record("kde-store", "pending", "ticket")
        with self.ocs("9.8.7"), self.assertRaises(ReleaseError):
            self.record("kde-store", "published")
        self.record("kde-store", "start")
        with self.assertRaises(ReleaseError):
            self.record("kde-store", "start")

    # KDE Store: receipt survives OCS lag, never a contradiction

    def test_kde_published_receipt_survives_ocs_lag_but_not_contradiction(self) -> None:
        self.record("kde-store", "start")
        with self.ocs("9.8.7"):
            self.assertEqual(self.status("kde-store")["status"], "pending")
            self.record("kde-store", "published")
            self.assertEqual(self.status("kde-store")["status"], "already_published")
        with self.ocs("0.10.0"):
            self.assertEqual(self.status("kde-store")["status"], "already_published")
        with self.ocs("9.8.7", md5="f" * 32):
            self.assertEqual(self.status("kde-store")["status"], "browser_required")
        with self.assertRaises(ReleaseError):
            self.record("kde-store", "start")

    def test_kde_published_requires_the_live_archive(self) -> None:
        self.record("kde-store", "start")
        for live in (self.ocs("0.10.0"), self.ocs("9.8.7", md5="f" * 32)):
            with live, self.assertRaises(ReleaseError):
                self.record("kde-store", "published")
        self.assertTrue((self.receipts / "kde-store.lock").exists())
        self.assertFalse((self.receipts / "kde-store.json").exists())

    def test_kde_pending_receipt_states(self) -> None:
        self.record("kde-store", "start")
        self.record("kde-store", "pending", "saved, awaiting refresh")
        with self.ocs("0.10.0"):
            self.assertEqual(self.status("kde-store")["status"], "pending")
        with self.ocs("9.8.7", md5="f" * 32):
            self.assertEqual(self.status("kde-store")["status"], "browser_required")
        with self.ocs("9.8.7"):
            self.assertEqual(self.status("kde-store")["status"], "pending")
            self.record("kde-store", "published")
            self.assertEqual(self.status("kde-store")["status"], "already_published")

    # Packet corruption

    def test_corrupted_packet_is_refused_before_a_lock(self) -> None:
        def edit_description(packet: Path) -> None:
            (packet / "kit" / "kde-store" / "description.txt").write_text(
                "changed\n", encoding="utf-8"
            )

        def add_unlisted_file(packet: Path) -> None:
            (packet / "kit" / "kde-store" / "media" / "extra.png").write_bytes(
                b"\x89PNG\r\n\x1a\n"
            )

        def drop_zip(packet: Path) -> None:
            (packet / "kit.zip").unlink()

        def symlink_description(packet: Path) -> None:
            target = packet / "kit" / "kde-store" / "description.txt"
            copy = packet / "description-copy.txt"
            shutil.copyfile(target, copy)
            target.unlink()
            target.symlink_to(copy)

        def forge_task_hash(packet: Path) -> None:
            stores = json.loads((packet / "stores.json").read_text(encoding="utf-8"))
            stores["browser_tasks"][0]["content_sha256"] = "0" * 64
            (packet / "stores.json").write_text(json.dumps(stores), encoding="utf-8")

        def consistent_rewrite(packet: Path) -> None:
            # Every checksum is rewritten to agree, but the task still names the prepared content.
            edit_description(packet)
            sums = packet / "kit" / "SHA256SUMS"
            lines = []
            for line in sums.read_text(encoding="utf-8").splitlines():
                name = line.split("  ", 1)[1]
                lines.append(
                    f"{hashlib.sha256((packet / 'kit' / name).read_bytes()).hexdigest()}  {name}"
                )
            sums.write_text("\n".join(lines) + "\n", encoding="utf-8")
            stores = json.loads((packet / "stores.json").read_text(encoding="utf-8"))
            stores["kit"]["sha256sums_sha256"] = hashlib.sha256(
                sums.read_bytes()
            ).hexdigest()
            (packet / "stores.json").write_text(json.dumps(stores), encoding="utf-8")

        def hide_review_flag(packet: Path) -> None:
            stores = json.loads((packet / "stores.json").read_text(encoding="utf-8"))
            stores["browser_tasks"][1]["claims_review_required"] = not stores[
                "browser_tasks"
            ][1]["claims_review_required"]
            (packet / "stores.json").write_text(json.dumps(stores), encoding="utf-8")

        def truncate_manifest(packet: Path) -> None:
            (packet / "stores.json").write_text(
                '{"schema_version": 1, "release": ', encoding="utf-8"
            )

        for mutate in (
            edit_description,
            add_unlisted_file,
            drop_zip,
            symlink_description,
            forge_task_hash,
            consistent_rewrite,
            hide_review_flag,
            truncate_manifest,
        ):
            with self.subTest(mutate.__name__):
                packet = self.tmp / f"packet-{mutate.__name__}"
                shutil.copytree(self.packet, packet, symlinks=True)
                mutate(packet)
                with self.assertRaises(ReleaseError):
                    self.record("kde-store", "start", packet=packet)
                self.assertFalse((self.receipts / "kde-store.lock").exists())

    # GitHub GraphQL error payloads

    def test_github_graphql_error_payloads_are_blocked(self) -> None:
        repo = {
            "homepage": release_stores.EXPECTED_HOMEPAGE,
            "description": "Dock",
            "topics": [],
        }
        for payload in (
            '{"errors": [], "data": null}',
            '{"errors": [{"message": "Bad credentials"}]}',
            "[]",
            '{"data": {"repository": null}}',
        ):
            with (
                self.subTest(payload),
                mock.patch.object(release_stores, "http_json", return_value=repo),
                mock.patch.object(release_stores, "command", return_value=payload),
            ):
                self.assertEqual(self.status("github")["status"], "blocked")


if __name__ == "__main__":
    unittest.main()
