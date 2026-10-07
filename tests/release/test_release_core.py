# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Safety boundaries of the portable release tooling.

Runs offline with the standard library:

    python3 -m unittest discover -s tests/release -v
"""

from __future__ import annotations

import io
import json
import os
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import release  # noqa: E402
import release_github  # noqa: E402
from release_common import (  # noqa: E402
    BANNER_PATH,
    CommandError,
    ReleaseError,
    archive_manifest,
    check_source_versions,
    command,
    extract_archive,
    gzip_deterministic,
    load_context,
    parse_tag,
    release_notes,
    result,
    run_process,
    sha256_bytes,
)

COMMIT = "a964a908ecb8904c079a8148403bebaaa427a821"
PINNED = "AC48F8F7708DDBA12D959C1FCF1251E9D42EF5B9"


def source_files(
    version: str = "1.2.3", revision: str = "1", **overrides: str
) -> dict[str, str]:
    files = {
        "CMakeLists.txt": f"cmake_minimum_required(VERSION 3.22)\nproject(krema VERSION {version} LANGUAGES C CXX)\n",
        "CHANGELOG.md": f"# Changelog\n\n## [Unreleased]\n\n## [{version}] - 2026-10-05\n\n### Fixed\n\n- Something\n\n## [0.1.0] - 2026-01-01\n",
        "src/com.bhyoo.krema.metainfo.xml": (
            '<?xml version="1.0"?>\n<component><releases>'
            f'<release version="{version}" date="2026-10-05"/><release version="0.1.0"/>'
            "</releases></component>\n"
        ),
        "packaging/obs/krema.spec": f"Name: krema\nVersion:        {version}\nRelease: 0\n",
        "packaging/obs/krema.dsc": (
            f"Format: 1.0\nSource: krema\nVersion: {version}-{revision}\n"
            f"DEBTRANSFORM-TAR: krema-{version}.tar.gz\n"
        ),
        "packaging/obs/debian.changelog": f"krema ({version}-{revision}) unstable; urgency=medium\n\n  * x\n",
        "packaging/arch/PKGBUILD": "pkgver=0.0.1\n",
        "tests/distro/targets.tsv": "# comment\nfedora-42\tfedora\tfedora:42\tsha256:x\tFedora_42\t*.rpm\n",
    }
    files.update(overrides)
    return files


def make_archive(
    files: dict[str, str],
    version: str = "1.2.3",
    commit: str | None = COMMIT,
    links: dict[str, str] | None = None,
    raw_names: list[str] | None = None,
) -> bytes:
    raw = io.BytesIO()
    headers = {"comment": commit} if commit else {}
    root = f"krema-{version}"
    with tarfile.open(
        fileobj=raw, mode="w", format=tarfile.PAX_FORMAT, pax_headers=headers
    ) as tar:
        top = tarfile.TarInfo(root + "/")
        top.type = tarfile.DIRTYPE
        top.mode = 0o755
        tar.addfile(top)
        for name, text in sorted(files.items()):
            data = text.encode()
            info = tarfile.TarInfo(f"{root}/{name}")
            info.size = len(data)
            info.mode = 0o644
            tar.addfile(info, io.BytesIO(data))
        for name, target in (links or {}).items():
            info = tarfile.TarInfo(f"{root}/{name}")
            info.type = tarfile.SYMTYPE
            info.linkname = target
            tar.addfile(info)
        for name in raw_names or []:
            info = tarfile.TarInfo(name)
            info.size = 0
            tar.addfile(info, io.BytesIO(b""))
    return gzip_deterministic(raw.getvalue())


class TempDirTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name).resolve()

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def fresh(self) -> Path:
        return Path(tempfile.mkdtemp(dir=self.tmp)).resolve()


class VersionTests(TempDirTest):
    def tree(self, **overrides: str) -> Path:
        source = self.fresh() / "krema-1.2.3"
        for name, text in source_files(**overrides).items():
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        return source

    def test_tag_format(self) -> None:
        self.assertEqual(parse_tag("v0.10.0"), "0.10.0")
        for bad in ("0.10.0", "v0.10", "v01.2.3", "v1.2.3-rc1", "v1.2.3/../x"):
            with self.assertRaises(ReleaseError, msg=bad):
                parse_tag(bad)

    def test_agreeing_source_returns_revision_and_ignores_lagging_pkgbuild(
        self,
    ) -> None:
        self.assertEqual(check_source_versions(self.tree(), "1.2.3"), "1")

    def test_each_disagreeing_field_is_rejected(self) -> None:
        cases = {
            "CMakeLists.txt": "project(krema VERSION 1.2.2 LANGUAGES C CXX)\n",
            "CHANGELOG.md": "# Changelog\n\n## [Unreleased]\n",
            "src/com.bhyoo.krema.metainfo.xml": '<component><releases><release version="1.2.2"/></releases></component>',
            "packaging/obs/krema.spec": "Version: 1.2.2\n",
            "packaging/obs/krema.dsc": "Version: 1.2.3-1\nDEBTRANSFORM-TAR: krema-1.2.2.tar.gz\n",
            "packaging/obs/debian.changelog": "krema (1.2.3-2) unstable; urgency=medium\n",
        }
        for name, text in cases.items():
            with self.subTest(name=name):
                source = self.tree(**{name: text})
                with self.assertRaises(ReleaseError) as caught:
                    check_source_versions(source, "1.2.3")
                self.assertIn("mismatch", str(caught.exception))


class ArchiveSafetyTests(TempDirTest):
    def assert_rejected(self, data: bytes) -> None:
        with self.assertRaises(ReleaseError):
            archive_manifest(data, "1.2.3")
        with self.assertRaises(ReleaseError):
            extract_archive(data, "1.2.3", self.tmp)
        self.assertEqual(list(self.tmp.iterdir()), [])

    def test_path_traversal_and_foreign_roots(self) -> None:
        for name in (
            "krema-1.2.3/../evil",
            "/etc/evil",
            "other-1.2.3/file",
            "krema-1.2.3/./x",
        ):
            with self.subTest(name=name):
                self.assert_rejected(make_archive(source_files(), raw_names=[name]))

    def test_unsafe_symlinks(self) -> None:
        cases = [
            {"escape": "../../etc/passwd"},
            {"absolute": "/etc/passwd"},
            {"dot": ".", "chain": "dot/../outside"},
        ]
        for links in cases:
            with self.subTest(links=links):
                self.assert_rejected(make_archive(source_files(), links=links))

    def test_member_below_symlink(self) -> None:
        files = source_files()
        files["dir/payload"] = "x"
        self.assert_rejected(make_archive(files, links={"dir": "packaging"}))

    def test_internal_relative_symlink_is_kept(self) -> None:
        files = source_files()
        files["LICENSES/GPL.txt"] = "license"
        data = make_archive(files, links={"LICENSE": "LICENSES/GPL.txt"})
        comment, manifest = archive_manifest(data, "1.2.3")
        self.assertEqual(comment, COMMIT)
        self.assertEqual(manifest["LICENSE"], ("link", "LICENSES/GPL.txt", False))
        source = extract_archive(data, "1.2.3", self.tmp)
        self.assertEqual((source / "LICENSE").read_text(), "license")


class ContextFixture(TempDirTest):
    def write_context(
        self, files: dict[str, str] | None = None, **changes: object
    ) -> Path:
        """A context produced like ``prepare`` does, in its own work_dir."""
        files = source_files() if files is None else files
        work = self.fresh()
        data = make_archive(files)
        archive = work / "krema-1.2.3.tar.gz"
        archive.write_bytes(data)
        source = extract_archive(data, "1.2.3", work)
        banner = None
        if BANNER_PATH in files:
            banner = f"https://media.githubusercontent.com/media/isac322/krema/v1.2.3/{BANNER_PATH}"
        notes = work / "notes.md"
        notes.write_text(
            release_notes(files["CHANGELOG.md"], "1.2.3", banner), encoding="utf-8"
        )
        context = {
            "schema_version": 1,
            "repository": "isac322/krema",
            "tag": "v1.2.3",
            "version": "1.2.3",
            "commit": COMMIT,
            "source_dir": str(source),
            "archive": str(archive),
            "sha256": sha256_bytes(data),
            "source_url": "https://github.com/isac322/krema/releases/download/v1.2.3/krema-1.2.3.tar.gz",
            "work_dir": str(work),
            "notes_path": str(notes),
            "debian_revision": "1",
        }
        context.update(changes)
        path = work / "context.json"
        path.write_text(json.dumps(context))
        return path


class ContextTests(ContextFixture):
    def assert_invalid(self, path: Path, fragment: str) -> None:
        with self.assertRaises(ReleaseError) as caught:
            load_context(path)
        self.assertIn(fragment, str(caught.exception))

    def test_valid_context_loads(self) -> None:
        context = load_context(self.write_context())
        self.assertEqual(
            (context.tag, context.version, context.debian_revision),
            ("v1.2.3", "1.2.3", "1"),
        )

    def test_notes_are_bound_to_the_tagged_changelog(self) -> None:
        path = self.write_context()
        notes = path.parent / "notes.md"
        notes.write_text(
            notes.read_text(encoding="utf-8") + "\nUnreviewed text\n", encoding="utf-8"
        )
        self.assert_invalid(path, "notes_path does not match")

    def test_banner_line_follows_the_tagged_tree(self) -> None:
        files = source_files()
        files[BANNER_PATH] = "png"
        context = load_context(self.write_context(files))
        self.assertTrue(
            context.notes_path.read_text(encoding="utf-8").startswith(
                "![Krema v1.2.3 release banner"
            )
        )

        path = self.write_context(files)
        (path.parent / "notes.md").write_text(
            release_notes(files["CHANGELOG.md"], "1.2.3", None), encoding="utf-8"
        )
        self.assert_invalid(path, "notes_path does not match")

    def test_tampered_archive_bytes(self) -> None:
        path = self.write_context()
        archive = path.parent / "krema-1.2.3.tar.gz"
        archive.write_bytes(archive.read_bytes() + b"\0")
        self.assert_invalid(path, "does not match context sha256")

    def test_tampered_source_tree(self) -> None:
        path = self.write_context()
        (path.parent / "krema-1.2.3" / "CMakeLists.txt").write_text("changed\n")
        self.assert_invalid(path, "source_dir differs")

    def test_extra_file_in_source_tree(self) -> None:
        path = self.write_context()
        (path.parent / "krema-1.2.3" / "injected").write_text("x")
        self.assert_invalid(path, "unexpected injected")

    def test_symlink_injected_into_source_tree(self) -> None:
        path = self.write_context()
        os.symlink("/etc/passwd", path.parent / "krema-1.2.3" / "leak")
        self.assert_invalid(path, "symlink")

    def test_mismatched_identity(self) -> None:
        cases = [
            ({"commit": "b" * 40}, "not generated from the context commit"),
            ({"tag": "v1.2.4"}, "does not match tag"),
            ({"version": "1.2.4", "tag": "v1.2.4"}, "source_url"),
            ({"debian_revision": "2"}, "debian_revision"),
            ({"source_url": "https://example.com/krema-1.2.3.tar.gz"}, "source_url"),
            ({"repository": "../krema"}, "owner/name"),
            ({"schema_version": 2}, "schema_version"),
        ]
        for changes, fragment in cases:
            with self.subTest(changes=changes):
                self.assert_invalid(self.write_context(**changes), fragment)

    def test_paths_outside_work_dir_or_symlinked(self) -> None:
        outside = self.fresh()
        (outside / "notes.md").write_text("x")
        self.assert_invalid(
            self.write_context(notes_path=str(outside / "notes.md")), "outside work_dir"
        )

        path = self.write_context()
        linked = path.parent / "notes-link.md"
        os.symlink(path.parent / "notes.md", linked)
        context = json.loads(path.read_text())
        context["notes_path"] = str(linked)
        path.write_text(json.dumps(context))
        self.assert_invalid(path, "symlink")

        path = self.write_context()
        context = json.loads(path.read_text())
        context["archive"] = str(path.parent / "sub" / ".." / "krema-1.2.3.tar.gz")
        path.write_text(json.dumps(context))
        self.assert_invalid(path, "canonical")

    def test_unknown_or_missing_fields(self) -> None:
        self.assert_invalid(self.write_context(token="x"), "fields mismatch")


class GateTests(unittest.TestCase):
    EXPECTED = ["fedora-42", "debian-13", "arch"]

    def run_(
        self,
        rid: int,
        *,
        targets=None,
        conclusion="success",
        status="completed",
        event="push",
        sha=COMMIT,
    ):
        names = self.EXPECTED if targets is None else targets
        jobs = [
            {"name": "Resolve targets", "status": "completed", "conclusion": "success"}
        ]
        jobs += [
            {
                "name": f"Distro E2E ({t})",
                "status": status,
                "conclusion": conclusion if status == "completed" else None,
            }
            for t in names
        ]
        return {
            "id": rid,
            "head_sha": sha,
            "event": event,
            "status": status,
            "conclusion": conclusion if status == "completed" else None,
            "html_url": f"https://example/{rid}",
            "jobs": jobs,
        }

    def test_targets_follow_the_tagged_file(self) -> None:
        tsv = (
            "# header\n\n"
            "a\tsuse\timg\tsha256:1\tA\t*.rpm\n"
            "  # indented comment\n"
            "short\tline\n"
            "b\tdebian\timg\tpending\tB\t*.deb\n"
        )
        self.assertEqual(release.expected_targets(tsv), ["a", "b", "arch"])
        with self.assertRaises(ReleaseError):
            release.expected_targets("a\tx\ty\tz\na\tx\ty\tz\n")
        with self.assertRaises(ReleaseError):
            release.expected_targets("# nothing\n")

    def test_repository_target_file_has_targets(self) -> None:
        tsv = (ROOT / "tests/distro/targets.tsv").read_text()
        rows = [
            line
            for line in tsv.splitlines()
            if line and not line.lstrip().startswith("#")
        ]
        self.assertEqual(
            release.expected_targets(tsv)[:-1], [r.split("\t")[0] for r in rows]
        )

    def test_full_green_run_is_ready(self) -> None:
        decision = release.classify_runs([self.run_(7)], self.EXPECTED, COMMIT)
        self.assertEqual((decision["status"], decision["run_id"]), ("ready", 7))

    def test_partial_matrix_never_qualifies(self) -> None:
        partial = self.run_(8, targets=["fedora-42"], event="workflow_dispatch")
        decision = release.classify_runs([partial], self.EXPECTED, COMMIT)
        self.assertEqual(decision["status"], "failed")
        self.assertIn("partial matrix", " ".join(decision["runs"][0]["problems"]))

    def test_partial_without_dispatch_allows_one_dispatch(self) -> None:
        decision = release.classify_runs(
            [self.run_(8, targets=["arch"])], self.EXPECTED, COMMIT
        )
        self.assertEqual(decision["status"], "needs_dispatch")

    def test_wrong_sha_extra_target_and_failed_job(self) -> None:
        runs = [
            self.run_(1, sha="c" * 40),
            self.run_(2, targets=[*self.EXPECTED, "rogue"]),
            self.run_(3, conclusion="failure", event="workflow_dispatch"),
        ]
        decision = release.classify_runs(runs, self.EXPECTED, COMMIT)
        self.assertEqual(decision["status"], "failed")
        self.assertEqual({r["run_id"] for r in decision["runs"]}, {2, 3})

    def test_cancelled_or_skipped_target_fails(self) -> None:
        run = self.run_(4, event="workflow_dispatch")
        run["jobs"][1]["conclusion"] = "skipped"
        self.assertEqual(
            release.classify_runs([run], self.EXPECTED, COMMIT)["status"], "failed"
        )

    def test_in_progress_full_run_is_pending(self) -> None:
        run = self.run_(5, status="in_progress")
        self.assertEqual(
            release.classify_runs([run], self.EXPECTED, COMMIT)["status"], "pending"
        )
        partial = self.run_(
            6, status="in_progress", targets=["arch"], event="workflow_dispatch"
        )
        self.assertEqual(
            release.classify_runs([partial], self.EXPECTED, COMMIT)["status"], "failed"
        )

    def test_no_runs_needs_dispatch(self) -> None:
        self.assertEqual(
            release.classify_runs([], self.EXPECTED, COMMIT)["status"], "needs_dispatch"
        )


class TrustAndResultTests(unittest.TestCase):
    def status(self, *keywords: str, fpr: str = PINNED, primary: str = PINNED) -> str:
        lines = [f"[GNUPG:] {k} 1234 Byeonghoon Yoo" for k in keywords]
        lines.append(f"[GNUPG:] VALIDSIG {fpr} 2026-10-04 0 0 4 0 1 10 00 {primary}")
        return "\n".join(lines)

    def test_pinned_signature_accepted_including_subkey(self) -> None:
        self.assertEqual(
            release.parse_gpg_status(self.status("GOODSIG"), (PINNED,)), PINNED
        )
        sub = "1" * 40
        self.assertEqual(
            release.parse_gpg_status(self.status("GOODSIG", fpr=sub), (PINNED,)), PINNED
        )

    def test_unpinned_bad_or_missing_signature_rejected(self) -> None:
        other = "2" * 40
        for text in (
            self.status("GOODSIG", fpr=other, primary=other),
            self.status("BADSIG"),
            self.status("EXPKEYSIG"),
            "[GNUPG:] ERRSIG x\n[GNUPG:] NO_PUBKEY x",
            "",
        ):
            with self.subTest(text=text):
                with self.assertRaises(ReleaseError):
                    release.parse_gpg_status(text, (PINNED,))

    def test_signer_fingerprints_validated(self) -> None:
        self.assertEqual(release.normalize_signers(None), (PINNED,))
        with self.assertRaises(ReleaseError):
            release.normalize_signers(["DEADBEEF"])

    def test_result_vocabulary_and_secret_fields(self) -> None:
        self.assertEqual(result("aur", "submitted", "ok")["status"], "submitted")
        with self.assertRaises(ReleaseError):
            result("aur", "done", "x")
        with self.assertRaises(ReleaseError):
            result("aur", "planned", "x", api_token="x")
        self.assertNotIn(
            "ghp_", result("x", "failed", "token ghp_" + "a" * 36)["detail"]
        )

    def test_assets_are_never_overwritten(self) -> None:
        sha = "a" * 64
        self.assertEqual(release_github.classify_asset(sha, None, None), "missing")
        self.assertEqual(
            release_github.classify_asset(sha, {"state": "uploaded"}, sha), "match"
        )
        self.assertEqual(
            release_github.classify_asset(sha, {"state": "uploaded"}, "b" * 64),
            "mismatch",
        )
        self.assertEqual(
            release_github.classify_asset(sha, {"state": "starter"}, None), "incomplete"
        )

    def test_notes_come_from_the_changelog_section(self) -> None:
        notes = release_notes(
            source_files()["CHANGELOG.md"], "1.2.3", "https://b/banner.png"
        )
        self.assertTrue(notes.startswith("![Krema v1.2.3 release banner"))
        self.assertIn("- Something", notes)
        self.assertNotIn("0.1.0", notes)
        with self.assertRaises(ReleaseError):
            release_notes("## [1.2.3] - 2026-10-05\n\n## [1.2.2]\n", "1.2.3", None)


GRANDCHILD = """
import json, os, subprocess, sys, time
child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
tmp = sys.argv[1] + ".tmp"
with open(tmp, "w") as handle:
    json.dump([os.getpid(), child.pid], handle)
os.replace(tmp, sys.argv[1])
time.sleep(60)
"""

EARLY_EXIT = """
import json, os, subprocess, sys
child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
tmp = sys.argv[1] + ".tmp"
with open(tmp, "w") as handle:
    json.dump([child.pid], handle)
os.replace(tmp, sys.argv[1])
"""


class ProcessGroupTests(TempDirTest):
    """Real subprocess trees: no descendant may outlive a timeout or a stop."""

    def pids(self, path: Path) -> list[int]:
        deadline = time.monotonic() + 15
        while not path.exists():
            if time.monotonic() > deadline:
                self.fail("process tree never started")
            time.sleep(0.05)
        return json.loads(path.read_text())

    def assert_gone(self, pid: int) -> None:
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                return
            time.sleep(0.05)
        os.kill(pid, signal.SIGKILL)
        self.fail(f"descendant {pid} survived")

    def test_command_timeout_kills_descendants(self) -> None:
        pidfile = self.tmp / "tree.json"
        with self.assertRaises(ReleaseError) as caught:
            command([sys.executable, "-c", GRANDCHILD, str(pidfile)], timeout=3)
        self.assertIn("timed out after 3s", str(caught.exception))
        for pid in self.pids(pidfile):
            self.assert_gone(pid)

    def test_leader_exit_with_pipe_holding_descendant(self) -> None:
        """The leader exits at once; its child keeps the pipes open."""
        pidfile = self.tmp / "tree.json"
        with self.assertRaises(ReleaseError) as caught:
            command([sys.executable, "-c", EARLY_EXIT, str(pidfile)], timeout=3)
        self.assertIn("timed out after 3s", str(caught.exception))
        for pid in self.pids(pidfile):
            self.assert_gone(pid)

    def test_command_failure_keeps_redacted_diagnostics(self) -> None:
        with self.assertRaises(CommandError) as caught:
            command(
                [sys.executable, "-c", "import sys; sys.exit('token ghp_' + 'a' * 36)"]
            )
        self.assertEqual(caught.exception.returncode, 1)
        self.assertNotIn("ghp_", str(caught.exception))

    def test_sigterm_cascades_into_nested_sessions(self) -> None:
        """SIGTERM to a release.py-like parent stops the child's own session."""
        pidfile = self.tmp / "tree.json"
        parent = (
            f"import sys; sys.path.insert(0, {str(ROOT / 'scripts')!r})\n"
            "from release_common import run_process\n"
            f"run_process([sys.executable, '-c', {GRANDCHILD!r}, {str(pidfile)!r}], timeout=60)\n"
        )
        proc = subprocess.Popen([sys.executable, "-c", parent], start_new_session=True)
        try:
            tree = self.pids(pidfile)
            proc.send_signal(signal.SIGTERM)
            self.assertEqual(proc.wait(timeout=15), 128 + signal.SIGTERM)
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        for pid in tree:
            self.assert_gone(pid)


class GitArchiveTests(TempDirTest):
    """``release._git_archive`` returns the raw tar bytes of a real commit."""

    GIT_ENV = {
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_AUTHOR_NAME": "Release Test",
        "GIT_AUTHOR_EMAIL": "test@example.com",
        "GIT_COMMITTER_NAME": "Release Test",
        "GIT_COMMITTER_EMAIL": "test@example.com",
    }

    def test_git_archive_returns_tar_bytes(self) -> None:
        repo = self.tmp / "repo"
        repo.mkdir()
        release.git(repo, "init", "-b", "main")
        (repo / "README.md").write_text("hello\n")
        release.git(repo, "add", "README.md")
        command(
            ["git", "-C", str(repo), "commit", "-m", "init"],
            env=self.GIT_ENV,
        )
        commit = release.git(repo, "rev-parse", "HEAD")
        data = release._git_archive(repo, commit, "1.2.3")
        self.assertIsInstance(data, bytes)
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as tar:
            self.assertIn("krema-1.2.3/README.md", tar.getnames())

    def test_git_archive_rejects_bad_commit(self) -> None:
        repo = self.tmp / "repo"
        repo.mkdir()
        release.git(repo, "init", "-b", "main")
        with self.assertRaises(ReleaseError) as caught:
            release._git_archive(repo, "0" * 40, "1.2.3")
        self.assertIn("git archive failed", str(caught.exception))

    def test_run_process_binary_keeps_raw_bytes(self) -> None:
        """binary=True must not UTF-8-decode stdout; the default still does."""
        script = "import sys; sys.stdout.buffer.write(b'\\xff\\xfe\\x80')"
        proc = run_process([sys.executable, "-c", script], binary=True)
        self.assertEqual(proc.stdout, b"\xff\xfe\x80")
        text = run_process([sys.executable, "-c", script])
        self.assertNotEqual(text.stdout, b"\xff\xfe\x80")
        self.assertIsInstance(text.stdout, str)


class GateDispatchTests(ContextFixture):
    def gate(self, context, dispatch) -> dict:
        decision = {
            "status": "needs_dispatch",
            "runs": [],
            "expected_targets": ["fedora-42", "arch"],
        }
        checks = [{"check": "tag", "ok": True, "detail": "ok"}]
        with (
            mock.patch.object(release, "evaluate_gate", return_value=decision),
            mock.patch.object(release, "trust_checks", return_value=checks),
            mock.patch.object(release, "command", side_effect=dispatch),
        ):
            return release.gate(context, self.tmp, (PINNED,), execute=True)

    def test_failed_dispatch_rolls_back_the_marker(self) -> None:
        context = load_context(self.write_context())
        marker = context.work_dir / release.DISPATCH_MARKER

        def fail(argv, **_):
            raise CommandError(
                "gh workflow failed (exit 1): HTTP 502", returncode=1, stderr="HTTP 502"
            )

        out = self.gate(context, fail)
        self.assertEqual(out["status"], "failed")
        self.assertIn("HTTP 502", out["detail"])
        self.assertFalse(os.path.lexists(marker))

        out = self.gate(context, lambda argv, **_: "")
        self.assertEqual(out["status"], "pending")
        self.assertEqual(json.loads(marker.read_text())["commit"], COMMIT)


class GithubPublishTests(ContextFixture):
    TAG_OBJECT = "1" * 40
    MATCH = {"krema-1.2.3.tar.gz": "match", "SHA256SUMS": "match"}

    def refs(
        self, tag_object: str = TAG_OBJECT, commit: str = COMMIT
    ) -> dict[str, str]:
        return {"refs/tags/v1.2.3": tag_object, "refs/tags/v1.2.3^{}": commit}

    def publish(self, context, inspections, refs) -> tuple[dict, list]:
        calls: list = []
        with (
            mock.patch.object(release_github, "inspect", side_effect=inspections),
            mock.patch.object(release_github, "remote_tag_oids", side_effect=refs),
            mock.patch.object(
                release_github,
                "command",
                side_effect=lambda argv, **_: calls.append(argv) or "",
            ),
        ):
            return release_github.publish(context, execute=True), calls

    def draft(self) -> dict:
        return {
            "state": "draft",
            "id": 7,
            "release": "https://x/7",
            "assets": dict(self.MATCH),
            "ours": True,
        }

    def test_tag_move_leaves_the_draft_unpublished(self) -> None:
        context = load_context(self.write_context())
        out, calls = self.publish(
            context,
            [self.draft(), self.draft()],
            [self.refs(), self.refs(tag_object="2" * 40)],
        )
        self.assertEqual(out["status"], "failed")
        self.assertIn("moved", out["detail"])
        self.assertIn("unpublished draft", out["detail"])
        self.assertFalse(any("draft=false" in argv for argv in calls))

    def test_verified_draft_is_published_by_id_after_the_final_tag_check(self) -> None:
        context = load_context(self.write_context())
        present = {
            "state": "present",
            "id": 7,
            "release": "https://x/7",
            "assets": dict(self.MATCH),
        }
        out, calls = self.publish(
            context, [self.draft(), self.draft(), present], [self.refs()] * 3
        )
        self.assertEqual(out["status"], "published")
        self.assertEqual(
            calls,
            [
                [
                    "gh",
                    "api",
                    "--method",
                    "PATCH",
                    "repos/isac322/krema/releases/7",
                    "-F",
                    "draft=false",
                ]
            ],
        )

    def test_notes_changed_after_validation_are_never_published(self) -> None:
        context = load_context(self.write_context())
        context.notes_path.write_text("Unreviewed\n", encoding="utf-8")
        missing = {"state": "missing", "id": None, "release": None, "assets": {}}
        out, calls = self.publish(context, [missing], [self.refs()])
        self.assertEqual(out["status"], "failed")
        self.assertIn("notes changed", out["detail"])
        self.assertEqual(calls, [])

    def test_tag_peeling_to_another_commit_is_refused(self) -> None:
        context = load_context(self.write_context())
        missing = {"state": "missing", "id": None, "release": None, "assets": {}}
        out, calls = self.publish(context, [missing], [self.refs(commit="b" * 40)])
        self.assertEqual(out["status"], "failed")
        self.assertEqual(calls, [])

    def test_foreign_draft_blocks(self) -> None:
        context = load_context(self.write_context())
        foreign = dict(self.draft(), ours=False)
        out, calls = self.publish(context, [foreign], [])
        self.assertEqual(out["status"], "blocked")
        self.assertEqual(calls, [])

    def test_sums_file_is_created_atomically_and_never_follows_links(self) -> None:
        context = load_context(self.write_context())
        path = release_github._sums_file(context)
        self.assertEqual(path.read_text(), f"{context.sha256}  krema-1.2.3.tar.gz\n")
        self.assertEqual(sorted(p.name for p in path.parent.iterdir()), ["SHA256SUMS"])
        self.assertEqual(release_github._sums_file(context), path)

        path.unlink()
        os.symlink(self.tmp / "absent", path)
        with self.assertRaises(ReleaseError):
            release_github._sums_file(context)
        self.assertFalse(os.path.lexists(self.tmp / "absent"))

        path.unlink()
        path.write_text("truncated")
        with self.assertRaises(ReleaseError):
            release_github._sums_file(context)


if __name__ == "__main__":
    unittest.main()
