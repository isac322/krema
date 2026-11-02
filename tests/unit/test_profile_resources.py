# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Docker resource collector lifecycle contracts."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
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
