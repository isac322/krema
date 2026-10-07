"""Boundary tests for scripts/release_aur.py (pure PKGBUILD/.SRCINFO decisions).

Run: python3 -m unittest discover -s tests/release
"""

from __future__ import annotations

import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import release_aur  # noqa: E402
from release_common import ReleaseContext, ReleaseError  # noqa: E402

SHA = "a" * 64
URL = "https://github.com/isac322/krema/releases/download/v1.2.3/krema-1.2.3.tar.gz"
TEMPLATE = (ROOT / "packaging/arch/PKGBUILD").read_text(encoding="utf-8")


def make_context(
    version: str = "1.2.3", sha256: str = SHA, source_url: str = URL
) -> ReleaseContext:
    tmp = Path(tempfile.gettempdir())
    return ReleaseContext(
        repository="isac322/krema",
        tag=f"v{version}",
        version=version,
        commit="0" * 40,
        source_dir=tmp,
        archive=tmp / f"krema-{version}.tar.gz",
        sha256=sha256,
        source_url=source_url,
        work_dir=tmp,
        notes_path=tmp / "notes.md",
        debian_revision="1",
    )


def srcinfo(
    version: str, rel: str = "1", source: str | None = None, sha: str = SHA
) -> str:
    source = source or f"krema-{version}.tar.gz::{URL}"
    return (
        f"pkgbase = krema\n\tpkgver = {version}\n\tpkgrel = {rel}\n"
        f"\tdepends = qt6-base>=6.8\n\tsource = {source}\n\tsha256sums = {sha}\n\npkgname = krema\n"
    )


class RenderPkgbuildTest(unittest.TestCase):
    def test_pins_version_source_hash_and_resets_pkgrel(self) -> None:
        out = release_aur.render_pkgbuild(TEMPLATE.replace("pkgrel=1", "pkgrel=4"), version="1.2.3",
                                          source_url=URL, sha256=SHA)  # fmt: skip
        self.assertIn("\npkgver=1.2.3\n", out)
        self.assertIn("\npkgrel=1\n", out)
        self.assertIn(f'source=("$pkgname-$pkgver.tar.gz::{URL}")', out)
        self.assertIn(f"sha256sums=('{SHA}')", out)

    def test_preserves_dependencies_arch_license_and_functions(self) -> None:
        out = release_aur.render_pkgbuild(
            TEMPLATE, version="1.2.3", source_url=URL, sha256=SHA
        )
        for marker in (
            "arch=(",
            "license=(",
            "depends=(",
            "makedepends=(",
            "build() {",
            "package() {",
        ):
            start = TEMPLATE.index(marker)
            block = TEMPLATE[start : TEMPLATE.index("\n", start)]
            self.assertIn(block, out)
        depends = TEMPLATE[
            TEMPLATE.index("depends=(") : TEMPLATE.index(
                ")", TEMPLATE.index("depends=(")
            )
        ]
        self.assertIn(depends, out)
        self.assertNotIn("archive/v$pkgver", out)

    def test_rejects_unsafe_inputs(self) -> None:
        for kwargs in (
            {"version": "1.2.3-rc1", "source_url": URL, "sha256": SHA},
            {
                "version": "1.2.3",
                "source_url": "http://example.com/x.tar.gz",
                "sha256": SHA,
            },
            {"version": "1.2.3", "source_url": URL + "$(id)", "sha256": SHA},
            {"version": "1.2.3", "source_url": URL, "sha256": "A" * 64},
        ):
            with self.subTest(kwargs=kwargs), self.assertRaises(ReleaseError):
                release_aur.render_pkgbuild(TEMPLATE, **kwargs)

    def test_rejects_ambiguous_template(self) -> None:
        with self.assertRaises(ReleaseError):
            release_aur.render_pkgbuild(
                TEMPLATE + "\npkgver=9.9.9\n",
                version="1.2.3",
                source_url=URL,
                sha256=SHA,
            )


class ClassifyRemoteTest(unittest.TestCase):
    def test_older_remote_is_planned(self) -> None:
        state, _, extra = release_aur.classify_remote(
            srcinfo("0.10.0"), make_context("1.2.3")
        )
        self.assertEqual(state, "planned")
        self.assertEqual(extra["aur_version"], "0.10.0-1")

    def test_numeric_not_lexical_ordering(self) -> None:
        state, _, _ = release_aur.classify_remote(
            srcinfo("0.9.0"), make_context("0.10.0")
        )
        self.assertEqual(state, "planned")

    def test_newer_remote_blocks_downgrade(self) -> None:
        state, _, _ = release_aur.classify_remote(
            srcinfo("1.10.0"), make_context("1.2.3")
        )
        self.assertEqual(state, "blocked")

    def test_equal_version_is_already_published(self) -> None:
        state, _, extra = release_aur.classify_remote(
            srcinfo("1.2.3", rel="2"), make_context("1.2.3")
        )
        self.assertEqual(state, "already_published")
        self.assertTrue(extra["source_matches"])

    def test_equal_version_with_other_source_is_not_rewritten(self) -> None:
        other = (
            "krema-1.2.3.tar.gz::https://github.com/isac322/krema/archive/v1.2.3.tar.gz"
        )
        state, detail, extra = release_aur.classify_remote(srcinfo("1.2.3", source=other, sha="b" * 64),
                                                           make_context("1.2.3"))  # fmt: skip
        self.assertEqual(state, "already_published")
        self.assertFalse(extra["source_matches"])
        self.assertIn("different source", detail)

    def test_missing_srcinfo_is_first_upload(self) -> None:
        state, _, extra = release_aur.classify_remote(None, make_context())
        self.assertEqual(state, "planned")
        self.assertIsNone(extra["aur_version"])


class GeneratedSrcinfoTest(unittest.TestCase):
    def test_accepts_matching_srcinfo(self) -> None:
        release_aur.validate_generated_srcinfo(srcinfo("1.2.3"), make_context())

    def test_rejects_mismatch(self) -> None:
        with self.assertRaises(ReleaseError):
            release_aur.validate_generated_srcinfo(
                srcinfo("1.2.3", sha="b" * 64), make_context()
            )

    @unittest.skipUnless(
        shutil.which("makepkg") or shutil.which("docker"), "neither makepkg nor docker"
    )
    def test_generated_srcinfo_matches_context_without_writing_stage(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp)
            (stage / "PKGBUILD").write_text(
                release_aur.render_pkgbuild(
                    TEMPLATE, version="1.2.3", source_url=URL, sha256=SHA
                ),
                encoding="utf-8",
            )
            text, _ = release_aur.generate_srcinfo(stage)
            release_aur.validate_generated_srcinfo(text, make_context())
            self.assertIn("depends = kpipewire", text)
            self.assertEqual(sorted(p.name for p in stage.iterdir()), ["PKGBUILD"])


if __name__ == "__main__":
    unittest.main()
