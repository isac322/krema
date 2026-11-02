# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Docker resource collector lifecycle contracts."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


PROFILE_PATH = Path(__file__).resolve().parents[1] / "distro/profile-resources.py"
spec = importlib.util.spec_from_file_location("profile_resources", PROFILE_PATH)
profile_resources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile_resources)


def state(status: str) -> dict:
    return {"State": {"Status": status}, "Config": {"Image": "krema:test"}}


class ProfileResourcesTests(unittest.TestCase):
    def setUp(self):
        profile_resources._STOP.clear()

    def tearDown(self):
        profile_resources._STOP.clear()

    @staticmethod
    def stats(cpu="0.00%", memory="0B / 0B", block_io="0B / 0B", pids="0"):
        return json.dumps(
            {"CPUPerc": cpu, "MemUsage": memory, "BlockIO": block_io, "PIDs": pids}
        )

    def test_stopped_container_zero_stats_are_not_reported(self):
        with mock.patch.object(
            profile_resources,
            "_inspect_container",
            side_effect=[state("running"), state("exited")],
        ), mock.patch.object(
            profile_resources,
            "_run",
            return_value=(0, self.stats(), ""),
        ) as run:
            record = profile_resources._sample("cid-1")

        self.assertEqual(record["status"], "incomplete")
        self.assertEqual(record["reason"], "container exited")
        self.assertNotIn("stats", record)
        self.assertEqual(run.call_args.args[0][-1], "cid-1")
        self.assertEqual(run.call_count, 1)

    def test_running_idle_container_zero_metrics_are_valid(self):
        with mock.patch.object(
            profile_resources,
            "_inspect_container",
            side_effect=[state("running"), state("running")],
        ), mock.patch.object(
            profile_resources,
            "_run",
            return_value=(0, self.stats(), ""),
        ):
            record = profile_resources._sample("cid-1")

        self.assertNotIn("status", record)
        self.assertEqual(record["stats"]["cpu_percent"], 0.0)
        self.assertEqual(record["stats"]["memory_usage_bytes"], 0)
        self.assertEqual(record["stats"]["pids"], 0)

    def test_created_running_exited_waits_before_metadata_and_stops(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cidfile = root / "container.cid"
            output = root / "stats.jsonl"
            metadata = root / "metadata.json"
            cidfile.write_text("cid-1\n", encoding="utf-8")
            inspect = mock.patch.object(
                profile_resources,
                "_inspect_container",
                side_effect=[state("created"), state("running"), state("running"), state("exited")],
            )
            run = mock.patch.object(
                profile_resources,
                "_run",
                return_value=(0, self.stats(), ""),
            )
            metadata_builder = mock.patch.object(
                profile_resources,
                "_metadata",
                return_value={"container_id": "cid-1", "package_inventory": {"status": "complete"}},
            )
            with inspect, run as run_mock, metadata_builder as metadata_mock, mock.patch.object(
                profile_resources._STOP, "wait", return_value=False
            ):
                result = profile_resources.collect(cidfile, output, metadata, interval=0.01)

            self.assertEqual(result, 0)
            self.assertEqual(metadata_mock.call_count, 1)
            self.assertEqual(metadata_mock.call_args.args[0], "cid-1")
            records = [json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(records), 1)
            self.assertEqual(records[0]["reason"], "container exited")
            self.assertNotIn("stats", records[0])
            self.assertEqual(run_mock.call_count, 1)
            self.assertEqual(run_mock.call_args.args[0][-1], "cid-1")

    def test_snapshot_binary_digest_comes_from_query_payload(self):
        digest = "A" * 64
        payload = (
            "__KREMA_BINARY__\n"
            f"{digest}  /usr/bin/krema\n"
            "__KREMA_PACKAGES__\n"
            "rpm\n"
            "pkg-b-2\n"
            "pkg-a-1\n"
        )
        with mock.patch.object(
            profile_resources,
            "_image_metadata",
            return_value={"reference": "krema:test", "id": "image-id"},
        ), mock.patch.object(
            profile_resources,
            "_run",
            return_value=(0, payload, ""),
        ):
            metadata = profile_resources._snapshot_metadata("krema:test")

        self.assertEqual(metadata["status"], "complete")
        self.assertEqual(metadata["binary"]["status"], "complete")
        self.assertEqual(metadata["binary"]["sha256"], digest.lower())

    def test_package_inventory_hash_is_order_independent(self):
        first = profile_resources._package_inventory_from_lines(
            ["rpm", "pkg-b-2", "pkg-a-1"],
            returncode=0,
        )
        second = profile_resources._package_inventory_from_lines(
            ["rpm", "pkg-a-1", "pkg-b-2"],
            returncode=0,
        )
        expected = hashlib.sha256("pkg-a-1\npkg-b-2".encode("utf-8")).hexdigest()

        self.assertEqual(first["status"], "complete")
        self.assertEqual(first["inventory_hash"], expected)
        self.assertEqual(second["inventory_hash"], expected)

    def test_failed_snapshot_query_is_explicitly_incomplete(self):
        with mock.patch.object(
            profile_resources,
            "_image_metadata",
            return_value={"reference": "krema:test", "id": "image-id"},
        ), mock.patch.object(
            profile_resources,
            "_run",
            return_value=(17, "", "docker run failed"),
        ):
            metadata = profile_resources._snapshot_metadata("krema:test")

        self.assertEqual(metadata["status"], "incomplete")
        self.assertEqual(metadata["binary"]["status"], "incomplete")
        self.assertEqual(metadata["package_inventory"]["status"], "incomplete")
        self.assertTrue(metadata["errors"])

    def test_unsupported_package_query_is_not_empty_success(self):
        payload = (
            "__KREMA_BINARY__\n"
            f"{'A' * 64}  /usr/bin/krema\n"
            "__KREMA_PACKAGES__\n"
            "unavailable\n"
        )
        with mock.patch.object(
            profile_resources,
            "_image_metadata",
            return_value={"reference": "krema:test", "id": "image-id"},
        ), mock.patch.object(
            profile_resources,
            "_run",
            return_value=(0, payload, ""),
        ):
            metadata = profile_resources._snapshot_metadata("krema:test")

        self.assertEqual(metadata["status"], "incomplete")
        self.assertEqual(metadata["package_inventory"]["status"], "incomplete")
        self.assertEqual(metadata["package_inventory"]["manager"], "unavailable")
        self.assertTrue(metadata["package_inventory"]["error"])

    def test_main_routes_snapshot_and_sampling_modes(self):
        with tempfile.TemporaryDirectory() as directory:
            metadata = Path(directory) / "metadata.json"
            with mock.patch.object(
                sys,
                "argv",
                [
                    "profile-resources.py",
                    "--snapshot-image",
                    "krema:test",
                    "--metadata",
                    str(metadata),
                ],
            ), mock.patch.object(
                profile_resources,
                "collect_snapshot",
                return_value=7,
            ) as snapshot, mock.patch.object(
                profile_resources,
                "collect",
            ) as collect:
                result = profile_resources.main()

        self.assertEqual(result, 7)
        snapshot.assert_called_once_with("krema:test", metadata)
        collect.assert_not_called()

        with tempfile.TemporaryDirectory() as directory:
            cidfile = Path(directory) / "container.cid"
            output = Path(directory) / "stats.jsonl"
            metadata = Path(directory) / "metadata.json"
            with mock.patch.object(
                sys,
                "argv",
                [
                    "profile-resources.py",
                    "--cidfile",
                    str(cidfile),
                    "--output",
                    str(output),
                    "--metadata",
                    str(metadata),
                    "--interval",
                    "0.25",
                ],
            ), mock.patch.object(
                profile_resources,
                "collect",
                return_value=11,
            ) as collect, mock.patch.object(
                profile_resources,
                "collect_snapshot",
            ) as snapshot:
                result = profile_resources.main()

        self.assertEqual(result, 11)
        collect.assert_called_once_with(cidfile, output, metadata, 0.25)
        snapshot.assert_not_called()

    def test_package_manager_unavailable_is_incomplete(self):
        with mock.patch.object(
            profile_resources,
            "_run",
            return_value=(0, "unavailable\n", ""),
        ):
            inventory = profile_resources._package_inventory("cid-1")

        self.assertEqual(inventory["status"], "incomplete")
        self.assertEqual(inventory["manager"], "unavailable")
        self.assertEqual(inventory["packages"], [])

    def test_missing_container_does_not_query_other_containers(self):
        with mock.patch.object(profile_resources, "_inspect_container", return_value=None), mock.patch.object(
            profile_resources, "_run"
        ) as run:
            record = profile_resources._sample("cid-1")

        self.assertEqual(record["reason"], "container missing")
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
