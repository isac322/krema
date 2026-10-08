"""Channel-state boundary tests for scripts/release_rpm.py (stdlib unittest).

Run: python3 -m unittest discover -s tests/release -p 'test_release_rpm.py'
"""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import release_rpm as rpm  # noqa: E402
from release_common import ReleaseError  # noqa: E402

CLONE = "https://github.com/isac322/krema.git"

SERVICE = """<services>
  <service name="tar_scm">
    <param name="scm">git</param>
    <param name="url">https://github.com/isac322/krema.git</param>
    <param name="revision">master</param>
    <param name="versionformat">@PARENT_TAG@</param>
  </service>
  <service name="recompress">
    <param name="compression">gz</param>
  </service>
</services>"""


def write_obs_tree(root: Path, version: str = "1.2.3", debrev: str = "1") -> Path:
    obs = root / "packaging" / "obs"
    obs.mkdir(parents=True)
    files = {
        "_service": SERVICE,
        "krema.spec": f"Name: krema\nVersion:        {version}\nRelease: 1\n",
        "krema.dsc": (
            f"Source: krema\nVersion: {version}-{debrev}\n"
            f"DEBTRANSFORM-TAR: krema-{version}.tar.gz\n"
        ),
        "debian.changelog": f"krema ({version}-{debrev}) unstable; urgency=medium\n",
        "debian.control": "Source: krema\n",
        "debian.rules": "#!/usr/bin/make -f\n",
        "debian.copyright": "Format: x\n",
        "project.meta.xml": "<project/>",
        "project.config": "Prefer: x\n",
        "apply-project-config.sh": "#!/bin/sh\n",
    }
    for name, text in files.items():
        (obs / name).write_text(text)
    return root


def inputs(root: Path, version: str = "1.2.3", tag: str = "v1.2.3") -> dict[str, bytes]:
    return rpm.build_obs_inputs(
        root, tag=tag, version=version, debian_revision="1", clone_url=CLONE
    )


def directory(
    entries: dict[str, str], code: str | None = "succeeded"
) -> rpm.ObsDirectory:
    return rpm.ObsDirectory(
        rev="7", entries=entries, service_code=code, service_error=None
    )


RESULTS_PUBLISHED = """<resultlist state="x">
  <result project="home:isac322" repository="Fedora_44" arch="x86_64" code="published" state="published">
    <status package="krema" code="succeeded"/>
  </result>
  <result project="home:isac322" repository="openSUSE_Slowroll" arch="aarch64" code="published" state="published">
    <status package="krema" code="excluded"/>
  </result>
</resultlist>"""


class ObsInputs(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = write_obs_tree(Path(self.tmp.name))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_only_package_files_and_pinned_revision(self) -> None:
        staged = inputs(self.root)
        self.assertEqual(set(staged), set(rpm.OBS_PACKAGE_FILES))
        self.assertTrue(rpm.OBS_PROJECT_FILES.isdisjoint(staged))
        service = staged["_service"].decode()
        self.assertEqual(rpm.service_revision(service), "v1.2.3")
        self.assertIn("@PARENT_TAG@", service)

    def test_staging_is_deterministic(self) -> None:
        self.assertEqual(rpm.md5_map(inputs(self.root)), rpm.md5_map(inputs(self.root)))

    def test_rejects_tagged_packaging_version_mismatch(self) -> None:
        with self.assertRaises(ReleaseError):
            inputs(self.root, version="1.2.4", tag="v1.2.4")

    def test_rejects_debian_revision_mismatch(self) -> None:
        with self.assertRaises(ReleaseError):
            rpm.build_obs_inputs(
                self.root,
                tag="v1.2.3",
                version="1.2.3",
                debian_revision="2",
                clone_url=CLONE,
            )

    def test_rejects_foreign_scm_url(self) -> None:
        with self.assertRaises(ReleaseError):
            rpm.pin_service_revision(SERVICE, "v1.2.3", "https://example.com/other.git")

    def test_rejects_ambiguous_revision(self) -> None:
        doubled = SERVICE.replace(
            '<param name="scm">git</param>',
            '<param name="scm">git</param><param name="revision">x</param>',
        )
        with self.assertRaises(ReleaseError):
            rpm.pin_service_revision(doubled, "v1.2.3", CLONE)

    def test_rejects_symlinked_input(self) -> None:
        spec = self.root / "packaging" / "obs" / "krema.spec"
        target = self.root / "elsewhere.spec"
        target.write_text(spec.read_text())
        spec.unlink()
        spec.symlink_to(target)
        with self.assertRaises(ReleaseError):
            inputs(self.root)


class ObsDecision(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.staged = rpm.md5_map(inputs(write_obs_tree(Path(self.tmp.name))))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def decide(
        self,
        entries,
        *,
        remote_version="1.2.3",
        revision="v1.2.3",
        results=None,
        code="succeeded",
    ):
        return rpm.decide_obs(
            version="1.2.3",
            tag="v1.2.3",
            staged=self.staged,
            directory=directory(entries, code),
            remote_version=remote_version,
            remote_revision=revision,
            results=rpm.parse_obs_results(results or RESULTS_PUBLISHED),
        )

    def test_older_remote_is_planned_with_changed_files(self) -> None:
        entries = dict(self.staged, **{"krema.spec": "old", "_service": "old"})
        plan = self.decide(entries, remote_version="1.2.2", revision="master")
        self.assertEqual(plan.status, "planned")
        self.assertEqual(plan.differing, ["_service", "krema.spec"])

    def test_same_version_unpinned_service_is_planned(self) -> None:
        plan = self.decide(dict(self.staged, _service="unpinned"), revision="master")
        self.assertEqual((plan.status, plan.differing), ("planned", ["_service"]))

    def test_identical_published_sources(self) -> None:
        extra = dict(self.staged, **{"_service:tar_scm:krema-1.2.3.tar.gz": "z"})
        self.assertEqual(self.decide(extra).status, "published")

    def test_newer_remote_version_blocks_downgrade(self) -> None:
        plan = self.decide({}, remote_version="1.3.0", revision="v1.3.0")
        self.assertEqual(plan.status, "blocked")

    def test_newer_pinned_revision_blocks_downgrade(self) -> None:
        plan = self.decide(dict(self.staged), remote_version=None, revision="v2.0.0")
        self.assertEqual(plan.status, "blocked")

    def test_service_failure_is_failed(self) -> None:
        self.assertEqual(self.decide(dict(self.staged), code="failed").status, "failed")

    def test_building_is_pending(self) -> None:
        building = RESULTS_PUBLISHED.replace(
            'state="published">\n    <status package="krema" code="succeeded"/>',
            'state="building">\n    <status package="krema" code="building"/>',
            1,
        )
        self.assertEqual(
            self.decide(dict(self.staged), results=building).status, "pending"
        )

    def test_dirty_repository_is_pending(self) -> None:
        dirty = RESULTS_PUBLISHED.replace(
            'state="published">', 'state="published" dirty="true">', 1
        )
        self.assertEqual(
            self.decide(dict(self.staged), results=dirty).status, "pending"
        )

    def test_unresolvable_target_is_failed(self) -> None:
        bad = RESULTS_PUBLISHED.replace('code="succeeded"', 'code="unresolvable"')
        self.assertEqual(self.decide(dict(self.staged), results=bad).status, "failed")


def package(committish: str = "v1.2.2", **source) -> dict:
    src = {
        "type": "git",
        "clone_url": CLONE,
        "subdirectory": "packaging/obs",
        "committish": committish,
        "spec": "krema.spec",
        "source_build_method": "rpkg",
    }
    src.update(source)
    return {
        "name": "krema",
        "source_type": "scm",
        "auto_rebuild": False,
        "source_dict": src,
    }


def build(bid: int, state: str, version: str | None) -> dict:
    sp = {"name": "krema", "version": f"{version}-1" if version else None}
    return {"id": bid, "state": state, "source_package": sp}


def config(committish: str, clone_url: str = CLONE, **source) -> dict:
    """Build source-build-config: the SCM identity recorded at submission."""
    src = {
        "type": "git",
        "clone_url": clone_url,
        "subdirectory": "packaging/obs",
        "committish": committish,
        "spec": "krema.spec",
        "srpm_build_method": "rpkg",
    }
    src.update(source)
    return {"source_type": "scm", "source_dict": src}


class CoprDecision(unittest.TestCase):
    def decide(self, builds, pkg=None, configs=None):
        return rpm.decide_copr(
            version="1.2.3",
            tag="v1.2.3",
            package=pkg or package(),
            builds=builds,
            configs=configs or {},
            clone_url=CLONE,
        )

    def test_no_build_plans_edit_and_build(self) -> None:
        plan = self.decide([build(1, "succeeded", "1.2.2")])
        self.assertEqual((plan.status, plan.needs_edit), ("planned", True))

    def test_already_pinned_plans_build_only(self) -> None:
        plan = self.decide([], package("v1.2.3"))
        self.assertEqual((plan.status, plan.needs_edit), ("planned", False))

    def test_succeeded_exact_version_is_published(self) -> None:
        plan = self.decide(
            [build(2, "succeeded", "1.2.3"), build(1, "succeeded", "1.2.2")],
            configs={2: config("v1.2.3")},
        )
        self.assertEqual((plan.status, plan.build_ids), ("published", [2]))

    def test_same_version_other_tag_is_not_published(self) -> None:
        # Another branch/tag building the same version must not satisfy us.
        plan = self.decide(
            [build(2, "succeeded", "1.2.3")],
            configs={2: config("release-candidate")},
        )
        self.assertEqual(plan.status, "planned")

    def test_later_success_wins_over_earlier_failure(self) -> None:
        plan = self.decide(
            [build(3, "failed", "1.2.3"), build(4, "succeeded", "1.2.3")],
            configs={3: config("v1.2.3"), 4: config("v1.2.3")},
        )
        self.assertEqual(plan.status, "published")

    def test_our_submitted_build_without_srpm_is_pending(self) -> None:
        plan = self.decide(
            [build(5, "pending", None), build(1, "succeeded", "1.2.2")],
            configs={5: config("v1.2.3")},
        )
        self.assertEqual((plan.status, plan.build_ids), ("pending", [5]))

    def test_unrelated_pending_build_does_not_block(self) -> None:
        plan = self.decide(
            [build(5, "running", None)],
            configs={5: config("master")},
        )
        self.assertEqual(plan.status, "planned")

    def test_failed_build_of_our_tag_is_failed(self) -> None:
        plan = self.decide([build(6, "failed", "1.2.3")], configs={6: config("v1.2.3")})
        self.assertEqual((plan.status, plan.build_ids), ("failed", [6]))

    def test_our_tag_build_with_unknown_version_is_failed(self) -> None:
        # Pinned to our tag but reported no SRPM version: honest failure.
        plan = self.decide([build(6, "succeeded", None)], configs={6: config("v1.2.3")})
        self.assertEqual(plan.status, "failed")

    def test_newer_version_of_our_tag_blocks_not_failed(self) -> None:
        # A terminal newer-version build hits the downgrade guard first.
        plan = self.decide(
            [build(6, "succeeded", "9.9.9")], configs={6: config("v1.2.3")}
        )
        self.assertEqual(plan.status, "blocked")

    def test_newer_build_blocks_downgrade(self) -> None:
        self.assertEqual(
            self.decide([build(7, "succeeded", "1.3.0")]).status, "blocked"
        )

    def test_newer_active_build_blocks_downgrade(self) -> None:
        self.assertEqual(self.decide([build(7, "running", "2.0.0")]).status, "blocked")

    def test_active_build_of_newer_tag_blocks_downgrade(self) -> None:
        plan = self.decide([build(7, "running", None)], configs={7: config("v2.0.0")})
        self.assertEqual(plan.status, "blocked")

    def test_newer_committish_blocks_downgrade(self) -> None:
        self.assertEqual(self.decide([], package("v1.4.0")).status, "blocked")

    def test_foreign_source_settings_block_edit(self) -> None:
        plan = self.decide([], package(subdirectory="rpm"))
        self.assertEqual(plan.status, "blocked")


class Versions(unittest.TestCase):
    def test_semver_ordering_is_numeric(self) -> None:
        self.assertGreater(rpm.version_key("0.10.0"), rpm.version_key("0.9.0"))

    def test_branch_is_not_tag_version(self) -> None:
        self.assertIsNone(rpm.tag_version("master"))
        self.assertEqual(rpm.tag_version("v0.10.0"), "0.10.0")

    def test_md5_matches_obs_entry_format(self) -> None:
        self.assertEqual(rpm.md5_map({"a": b"x"})["a"], hashlib.md5(b"x").hexdigest())


if __name__ == "__main__":
    unittest.main()
