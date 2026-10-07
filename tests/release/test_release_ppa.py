"""Boundary tests for scripts/release_ppa.py: versions, series policy, RFC 822 checksums.

Run: python3 -m unittest discover -s tests/release -p 'test_*.py'
"""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import release_ppa as ppa  # noqa: E402
from release_common import ReleaseError  # noqa: E402


class DebianVersionTest(unittest.TestCase):
    def test_dpkg_ordering(self) -> None:
        cases = [
            ("1.0~rc1", "1.0", -1),
            ("1.0", "1.0+", -1),
            ("1.0a", "1.0", 1),
            ("1:0.1", "9.9", 1),
            ("1.0-1", "1.0-1", 0),
            ("0.10.0-1~ppa1~resolute1", "0.9.0-1~ppa1~resolute1", 1),
            ("0.10.0-1~ppa1~stonking1", "0.10.0-1", -1),
            ("0.10.0-2~ppa1~resolute1", "0.10.0-1~ppa1~resolute1", 1),
            ("0.7.0-2~ppa3~questing1", "0.7.0-2~ppa2~questing1", 1),
            ("6.8.3+dfsg-0ubuntu2", "6.8.0", 1),
            ("6.8.0~beta1+dfsg-1", "6.8.0", -1),
            ("6.4.2+dfsg-21.1build5", "6.8.0", -1),
        ]
        for left, right, expected in cases:
            with self.subTest(left=left, right=right):
                self.assertEqual(ppa.compare_versions(left, right), expected)
                self.assertEqual(ppa.compare_versions(right, left), -expected)

    def test_package_version_format(self) -> None:
        self.assertEqual(
            ppa.package_version("0.10.0", "1", "resolute"), "0.10.0-1~ppa1~resolute1"
        )
        self.assertEqual(
            ppa.package_version("1.2.3", "2", "stonking"), "1.2.3-2~ppa1~stonking1"
        )
        self.assertEqual(ppa.upstream_version("0.10.0-1~ppa1~resolute1"), "0.10.0")

    def test_package_version_rejects_ambiguous_parts(self) -> None:
        for args in (
            ("0.10.0", "1-1", "resolute"),
            ("0.10.0-1", "1", "resolute"),
            ("0.10.0", "1", "Resolute"),
            ("0.10.0", "", "resolute"),
        ):
            with self.subTest(args=args), self.assertRaises(ReleaseError):
                ppa.package_version(*args)


def _line(data: bytes, name: str, algorithm: str, extra: str = "") -> str:
    digest = hashlib.new(algorithm, data).hexdigest()
    return f" {digest} {len(data)}{extra} {name}"


class ControlChecksumTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.orig = b"orig tarball bytes"
        self.dsc = b"Format: 3.0 (quilt)\nSource: krema\n"
        (self.dir / "krema_1.0.orig.tar.gz").write_bytes(self.orig)
        (self.dir / "krema_1.0-1.dsc").write_bytes(self.dsc)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def changes(self, dsc: bytes) -> str:
        files = (("krema_1.0-1.dsc", dsc), ("krema_1.0.orig.tar.gz", self.orig))
        lines = [
            "Format: 1.8",
            "Source: krema",
            "Version: 1.0-1~ppa1~resolute1",
            "Distribution: resolute",
        ]
        for field, algorithm in (
            ("Checksums-Sha1", "sha1"),
            ("Checksums-Sha256", "sha256"),
            ("Files", "md5"),
        ):
            lines.append(f"{field}:")
            extra = " kde optional" if field == "Files" else ""
            lines.extend(_line(data, name, algorithm, extra) for name, data in files)
        return "\n".join(lines) + "\n"

    def test_signed_dsc_requires_rewritten_checksums(self) -> None:
        text = self.changes(self.dsc)
        self.assertEqual(
            ppa.verify_listed_files(text, self.dir),
            ["krema_1.0-1.dsc", "krema_1.0.orig.tar.gz"],
        )
        signed = (
            b"-----BEGIN PGP SIGNED MESSAGE-----\n"
            + self.dsc
            + b"-----BEGIN PGP SIGNATURE-----\n"
        )
        (self.dir / "krema_1.0-1.dsc").write_bytes(signed)
        with self.assertRaises(ReleaseError):
            ppa.verify_listed_files(text, self.dir)
        updated = ppa.update_checksums(text, "krema_1.0-1.dsc", signed)
        ppa.verify_listed_files(updated, self.dir)
        entry = ppa.listed_files(updated)["krema_1.0-1.dsc"]
        self.assertEqual(
            entry["sha256"], (hashlib.sha256(signed).hexdigest(), len(signed))
        )
        self.assertEqual(entry["md5"], (hashlib.md5(signed).hexdigest(), len(signed)))
        self.assertIn(" kde optional krema_1.0-1.dsc", updated)
        self.assertEqual(
            ppa.listed_files(updated)["krema_1.0.orig.tar.gz"],
            ppa.listed_files(text)["krema_1.0.orig.tar.gz"],
        )

    def test_update_refuses_signed_or_partial_listing(self) -> None:
        text = self.changes(self.dsc)
        with self.assertRaises(ReleaseError):
            ppa.update_checksums(
                "-----BEGIN PGP SIGNED MESSAGE-----\n" + text, "krema_1.0-1.dsc", b"x"
            )
        with self.assertRaises(ReleaseError):
            ppa.update_checksums(text, "krema_1.0-1.debian.tar.xz", b"x")
        partial = (
            "\n".join(
                line
                for line in text.splitlines()
                if not (
                    line.startswith(" ")
                    and len(line.split()[0]) == 40
                    and line.endswith(".dsc")
                )
            )
            + "\n"
        )
        with self.assertRaises(ReleaseError):
            ppa.update_checksums(partial, "krema_1.0-1.dsc", b"x")

    def test_size_mismatch_and_unsafe_names_are_rejected(self) -> None:
        text = self.changes(self.dsc).replace(
            f" {len(self.orig)} krema_1.0.orig", f" {len(self.orig) + 1} krema_1.0.orig"
        )
        with self.assertRaises(ReleaseError):
            ppa.verify_listed_files(text, self.dir)
        with self.assertRaises(ReleaseError):
            ppa.listed_files("Files:\n 00 1 ../escape\n")

    def test_clearsign_body_is_parsed(self) -> None:
        signed = (
            "-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA512\n\n"
            "Source: krema\nVersion: 1.0-1\nDescription: x\n - dash line\n"
            "-----BEGIN PGP SIGNATURE-----\n\nabc\n-----END PGP SIGNATURE-----\n"
        )
        fields = ppa.parse_control(signed)
        self.assertEqual(fields["Version"], "1.0-1")
        self.assertEqual(
            ppa.strip_clearsign(
                "- -dashed\n".join(
                    [
                        "-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA512\n\n",
                        "-----BEGIN PGP SIGNATURE-----\n",
                    ]
                )
            ),
            "-dashed\n",
        )
        with self.assertRaises(ReleaseError):
            ppa.strip_clearsign(
                "-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA512\n\nSource: krema\n"
            )


CHANGELOG = """krema (0.10.0-1) unstable; urgency=medium

  * New upstream release v0.10.0
  * Fixed something

 -- Byeonghoon Yoo <bhyoo@bhyoo.com>  Sat, 03 Oct 2026 00:00:00 +0900

krema (0.9.0-1) unstable; urgency=medium

  * Older

 -- Byeonghoon Yoo <bhyoo@bhyoo.com>  Mon, 28 Sep 2026 00:00:00 +0900
"""


class PackagingInputTest(unittest.TestCase):
    def test_series_changelog_uses_top_entry(self) -> None:
        entry = ppa.parse_top_changelog_entry(CHANGELOG)
        self.assertEqual(entry.version, "0.10.0-1")
        rendered = ppa.render_series_changelog(
            entry, "0.10.0-1~ppa1~resolute1", "resolute"
        )
        self.assertEqual(
            rendered,
            "krema (0.10.0-1~ppa1~resolute1) resolute; urgency=medium\n\n"
            "  * New upstream release v0.10.0\n  * Fixed something\n\n"
            " -- Byeonghoon Yoo <bhyoo@bhyoo.com>  Sat, 03 Oct 2026 00:00:00 +0900\n",
        )
        self.assertEqual(
            entry.timestamp,
            int(datetime(2026, 10, 2, 15, tzinfo=timezone.utc).timestamp()),
        )

    def test_changelog_without_trailer_or_date_is_rejected(self) -> None:
        with self.assertRaises(ReleaseError):
            ppa.parse_top_changelog_entry(
                "krema (1.0-1) unstable; urgency=medium\n\n  * x\n"
            )
        with self.assertRaises(ReleaseError):
            ppa.parse_top_changelog_entry(
                "krema (1.0-1) unstable; urgency=medium\n\n  * x\n\n -- A <a@b>  not a date\n"
            )

    def test_repository_packaging_inputs(self) -> None:
        control = (ROOT / "packaging" / "obs" / "debian.control").read_text(
            encoding="utf-8"
        )
        self.assertEqual(ppa.parse_qt_baseline(control), "6.8.0")
        image = ppa.builder_image(
            (ROOT / "tests" / "distro" / "targets.tsv").read_text(encoding="utf-8")
        )
        self.assertRegex(image, r"^ubuntu:\d+\.\d+@sha256:[0-9a-f]{64}$")

    def test_builder_image_picks_newest_pinned_ubuntu(self) -> None:
        rows = "\n".join(
            [
                "# comment",
                "ubuntu-26.04\tdebian\tubuntu:26.04\tsha256:"
                + "a" * 64
                + "\txUbuntu_26.04\t*.deb",
                "ubuntu-26.10\tdebian\tubuntu:26.10\tpending\txUbuntu_26.10\t*.deb",
                "ubuntu-25.10\tdebian\tubuntu:25.10\tsha256:"
                + "b" * 64
                + "\txUbuntu_25.10\t*.deb",
                "debian-13\tdebian\tdebian:13-slim\tsha256:"
                + "c" * 64
                + "\tDebian_13\t*.deb",
            ]
        )
        self.assertEqual(ppa.builder_image(rows), "ubuntu:26.04@sha256:" + "a" * 64)


SERIES = [
    {
        "name": "stonking",
        "version": "26.10",
        "status": "Pre-release Freeze",
        "active": True,
    },
    {
        "name": "resolute",
        "version": "26.04",
        "status": "Current Stable Release",
        "active": True,
    },
    {"name": "questing", "version": "25.10", "status": "Obsolete", "active": False},
    {"name": "plucky", "version": "25.04", "status": "Obsolete", "active": False},
    {"name": "oracular", "version": "24.10", "status": "Obsolete", "active": False},
    {"name": "noble", "version": "24.04", "status": "Supported", "active": True},
    {"name": "mantic", "version": "23.10", "status": "Obsolete", "active": False},
    {"name": "future", "version": "27.04", "status": "Future", "active": False},
]
QT = {
    "stonking": "6.11.2+dfsg-4ubuntu2",
    "resolute": "6.10.2+dfsg-7",
    "questing": "6.9.2+dfsg-1ubuntu1",
    "plucky": "6.8.3+dfsg-0ubuntu2",
    "noble": "6.4.2+dfsg-21.1build5",
    "mantic": "6.8.0+dfsg-1",
}


class SeriesPolicyTest(unittest.TestCase):
    def test_qt_baseline_and_upload_policy(self) -> None:
        looked_up: list[str] = []

        def lookup(name: str) -> str | None:
            looked_up.append(name)
            return QT.get(name)

        selected = ppa.select_series(SERIES, lookup, "6.8.0")
        roles = {c.name: c.role for c in selected}
        self.assertEqual(
            roles,
            {
                "stonking": "target",
                "resolute": "target",
                "questing": "platform_blocked",
                "plucky": "platform_blocked",
                "noble": "not_applicable",
            },
        )
        self.assertNotIn("future", looked_up)
        self.assertNotIn("mantic", looked_up)  # obsolete walk stopped at oracular


NOW = datetime(2026, 10, 7, tzinfo=timezone.utc)
TARGET = "0.10.0-1~ppa1~resolute1"


def pub(status: str, source_id: str = "1", version: str = TARGET) -> dict:
    return {
        "status": status,
        "source_package_version": version,
        "self_link": f"https://api.launchpad.net/devel/~isac322/+archive/ubuntu/krema/+sourcepub/{source_id}",
    }


class SeriesStateTest(unittest.TestCase):
    def classify(self, exact=(), summaries=None, newest=None, marker=None):
        return ppa.classify_target(
            TARGET, list(exact), summaries or {}, newest, marker, NOW
        )[0]

    def test_launchpad_states(self) -> None:
        self.assertEqual(self.classify([pub("Pending")]), "pending")
        self.assertEqual(
            self.classify([pub("Published")], {"1": {"status": "FULLYBUILT"}}),
            "published",
        )
        self.assertEqual(
            self.classify([pub("Superseded")], {"1": {"status": "FULLYBUILT"}}),
            "published",
        )
        self.assertEqual(
            self.classify([pub("Published")], {"1": {"status": "FULLYBUILT_PENDING"}}),
            "pending",
        )
        self.assertEqual(
            self.classify([pub("Published")], {"1": {"status": "BUILDING"}}), "pending"
        )
        self.assertEqual(
            self.classify([pub("Published")], {"1": {"status": "FAILEDTOBUILD"}}),
            "failed",
        )
        self.assertEqual(self.classify([pub("Published")], {}), "failed")
        self.assertEqual(self.classify([pub("Deleted")]), "blocked")
        self.assertEqual(self.classify(newest="0.11.0-1~ppa1~resolute1"), "blocked")
        self.assertEqual(self.classify(newest="0.9.0-1~ppa1~resolute1"), "missing")
        with self.assertRaises(ReleaseError):
            self.classify([pub("Published", version="0.9.0-1~ppa1~resolute1")])

    def test_upload_record_window(self) -> None:
        fresh = {
            "version": TARGET,
            "uploaded_at": (NOW - timedelta(minutes=10)).isoformat(),
        }
        stale = {
            "version": TARGET,
            "uploaded_at": (NOW - timedelta(hours=4)).isoformat(),
        }
        self.assertEqual(self.classify(marker=fresh), "pending")
        self.assertEqual(self.classify(marker=stale), "failed")
        with self.assertRaises(ReleaseError):
            self.classify(
                marker={
                    "version": "0.9.0-1~ppa1~resolute1",
                    "uploaded_at": NOW.isoformat(),
                }
            )

    def test_aggregate(self) -> None:
        self.assertEqual(
            ppa.aggregate_state(["published", "published"]), "already_published"
        )
        self.assertEqual(ppa.aggregate_state(["published", "missing"]), "planned")
        self.assertEqual(ppa.aggregate_state(["submitted", "published"]), "submitted")
        self.assertEqual(ppa.aggregate_state(["pending", "published"]), "pending")
        self.assertEqual(ppa.aggregate_state(["failed", "missing"]), "failed")
        self.assertEqual(ppa.aggregate_state(["blocked", "pending"]), "blocked")
        self.assertEqual(ppa.aggregate_state([]), "blocked")


if __name__ == "__main__":
    unittest.main()
