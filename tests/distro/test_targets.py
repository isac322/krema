#!/usr/bin/env python3
"""Validate the distro target matrix against the OBS project metadata."""

from __future__ import annotations

from collections import Counter
import csv
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
META = ROOT / "packaging" / "obs" / "project.meta.xml"
TARGETS = ROOT / "tests" / "distro" / "targets.tsv"
DIGEST_RE = re.compile(r"sha256:[0-9a-f]{64}")


def obs_repositories() -> set[str]:
    root = ET.parse(META).getroot()
    return {
        repository.attrib["name"]
        for repository in root.findall("repository")
    }


def target_rows() -> list[list[str]]:
    with TARGETS.open(newline="", encoding="utf-8") as stream:
        return [
            row
            for row in csv.reader(stream, delimiter="\t")
            if row and not row[0].startswith("#")
        ]


class DistroTargetMatrixTests(unittest.TestCase):
    def test_every_obs_repository_has_one_digest_pinned_target(self) -> None:
        rows = target_rows()
        self.assertTrue(rows)
        self.assertTrue(all(len(row) == 6 for row in rows))
        self.assertEqual(len({row[0] for row in rows}), len(rows))
        self.assertEqual(Counter(row[4] for row in rows), Counter(obs_repositories()))
        self.assertTrue(all(DIGEST_RE.fullmatch(row[3]) for row in rows))

    def test_fedora_45_target_contract(self) -> None:
        root = ET.parse(META).getroot()
        repository = root.find("repository[@name='Fedora_45']")
        self.assertIsNotNone(repository)
        assert repository is not None
        path = repository.find("path[@project='Fedora:45'][@repository='standard']")
        self.assertIsNotNone(path)
        self.assertEqual(
            {node.text for node in repository.findall("arch")},
            {"aarch64", "x86_64"},
        )

        rows = [row for row in target_rows() if row[0] == "fedora-45"]
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][0:3], ["fedora-45", "fedora", "fedora:45"])
        self.assertRegex(rows[0][3], DIGEST_RE)
        self.assertEqual(rows[0][4:], ["Fedora_45", "*.rpm"])


if __name__ == "__main__":
    unittest.main()
