#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Headless contracts for E2E dynamic workqueue, multi-output discovery, and KWin oracle waits."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

APPIUM_DIR = Path(__file__).resolve().parents[1] / "appium"
if str(APPIUM_DIR) not in sys.path:
    sys.path.insert(0, str(APPIUM_DIR))

from gi.repository import GLib  # noqa: E402
from krema_e2e import kwin  # noqa: E402


SUITE_CONFTEST = textwrap.dedent(
    """\
    import json
    import os
    from pathlib import Path
    import pytest

    pytest_plugins = ["krema_e2e.pytest_plugin"]

    def _log_event(event: str) -> None:
        log_dir = os.environ.get("SCHED_TEST_LOG_DIR")
        if not log_dir:
            return
        worker = os.environ.get("SCHED_TEST_WORKER", "single")
        with (Path(log_dir) / f"{worker}.jsonl").open("a", encoding="utf-8") as fh:
            fh.write(json.dumps({"worker": worker, "event": event}) + "\\n")

    @pytest.fixture(scope="session", autouse=True)
    def _pointer_capability():
        _log_event("session_setup")
        yield
        _log_event("session_teardown")

    @pytest.fixture(scope="module")
    def mod_resource(request):
        _log_event(f"module_setup:{request.module.__name__}")
        yield request.module.__name__
        _log_event(f"module_teardown:{request.module.__name__}")

    @pytest.fixture(scope="class")
    def cls_resource(request):
        _log_event(f"class_setup:{request.cls.__name__}")
        yield request.cls.__name__
        _log_event(f"class_teardown:{request.cls.__name__}")

    @pytest.fixture
    def fn_resource(request):
        _log_event(f"fn_setup:{request.node.name}")
        yield request.node.name
        _log_event(f"fn_teardown:{request.node.name}")
    """
)

SUITE_MOD_A = textwrap.dedent(
    """\
    import os
    import time
    import pytest

    def test_a1(mod_resource, fn_resource):
        time.sleep(0.04)
        assert mod_resource == "test_alpha"

    @pytest.mark.outputs(2)
    def test_a2_two_outputs(mod_resource, fn_resource):
        assert mod_resource == "test_alpha"

    @pytest.mark.outputs(4)
    def test_a3_four_outputs(mod_resource, fn_resource):
        assert mod_resource == "test_alpha"

    class TestGrouped:
        def test_g1(self, mod_resource, cls_resource, fn_resource):
            time.sleep(0.02)
            assert cls_resource == "TestGrouped"

        def test_g2(self, mod_resource, cls_resource, fn_resource):
            if os.environ.get("SCHED_TEST_FAIL_G2") == "1":
                pytest.fail("injected g2 failure")
            assert cls_resource == "TestGrouped"
    """
)

SUITE_MOD_B = textwrap.dedent(
    """\
    import time
    import pytest

    @pytest.mark.parametrize("idx", range(6))
    def test_b_param(idx, mod_resource, fn_resource):
        time.sleep(0.01 if idx % 2 else 0.03)
        assert mod_resource == "test_beta"

    @pytest.mark.outputs(3)
    def test_b_three_outputs(mod_resource, fn_resource):
        assert mod_resource == "test_beta"
    """
)


def _write_suite(root: Path) -> Path:
    suite = root / "suite"
    suite.mkdir()
    (suite / "pytest.ini").write_text("[pytest]\naddopts = -p no:cacheprovider\n", encoding="utf-8")
    (suite / "conftest.py").write_text(SUITE_CONFTEST, encoding="utf-8")
    (suite / "test_alpha.py").write_text(SUITE_MOD_A, encoding="utf-8")
    (suite / "test_beta.py").write_text(SUITE_MOD_B, encoding="utf-8")
    return suite


def _run_pytest(suite: Path, args: list[str], extra_env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env.pop("KREMA_E2E_SHARD", None)
    env.pop("KREMA_E2E_WORKQUEUE", None)
    env["KREMA_E2E_OUTPUT_COUNT"] = "1"
    env["PYTHONPATH"] = str(APPIUM_DIR) + (
        os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else ""
    )
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env.update(extra_env)
    return subprocess.run(
        [sys.executable, "-m", "pytest", *args],
        cwd=suite,
        env=env,
        capture_output=True,
        text=True,
        timeout=30,
    )


def _junit_cases(junit_path: Path) -> dict[str, str]:
    root = ET.parse(junit_path).getroot()
    cases: dict[str, str] = {}
    for case in root.iter("testcase"):
        key = f"{case.get('classname', '')}::{case.get('name', '')}"
        if case.find("failure") is not None or case.find("error") is not None:
            cases[key] = "failed"
        elif case.find("skipped") is not None:
            cases[key] = "skipped"
        else:
            cases[key] = "passed"
    return cases


class MultiOutputDiscoveryAndFilterTests(unittest.TestCase):
    def test_list_multi_outputs_discovers_all_arities_and_respects_filters(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            suite = _write_suite(root)
            out_all = root / "all-outputs.txt"
            res = _run_pytest(suite, ["--list-multi-outputs", str(out_all)], {})
            self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
            self.assertEqual(out_all.read_text(encoding="utf-8").splitlines(), ["2", "3", "4"])

            out_filtered = root / "filtered-outputs.txt"
            res_filtered = _run_pytest(
                suite,
                ["--list-multi-outputs", str(out_filtered), "-k", "test_b_param"],
                {},
            )
            self.assertEqual(res_filtered.returncode, 0, res_filtered.stderr + res_filtered.stdout)
            self.assertEqual(out_filtered.read_text(encoding="utf-8"), "")

    def test_only_matching_outputs_selects_exact_output_topology_without_skips(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            suite = _write_suite(root)
            for count, expected_name in ((2, "test_a2_two_outputs"), (3, "test_b_three_outputs"), (4, "test_a3_four_outputs")):
                junit = root / f"junit-{count}out.xml"
                res = _run_pytest(
                    suite,
                    ["--only-matching-outputs", f"--junitxml={junit}"],
                    {"KREMA_E2E_OUTPUT_COUNT": str(count)},
                )
                self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
                cases = _junit_cases(junit)
                self.assertEqual(list(cases.values()), ["passed"])
                self.assertEqual(len(cases), 1)
                self.assertTrue(next(iter(cases)).endswith(f"::{expected_name}"))


class DynamicWorkqueueTests(unittest.TestCase):
    def test_workqueue_partitions_items_bijectively_and_balances_fixture_teardowns(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            suite = _write_suite(root)
            base_junit = root / "base.xml"
            base_res = _run_pytest(suite, [f"--junitxml={base_junit}"], {})
            self.assertEqual(base_res.returncode, 0, base_res.stderr + base_res.stdout)
            expected_cases = _junit_cases(base_junit)
            self.assertEqual(len(expected_cases), 12)

            queue_dir = root / "workqueue"
            log_dir = root / "logs"
            queue_dir.mkdir()
            log_dir.mkdir()

            base_env = os.environ.copy()
            base_env["KREMA_E2E_OUTPUT_COUNT"] = "1"
            base_env["KREMA_E2E_WORKQUEUE"] = str(queue_dir)
            base_env["SCHED_TEST_LOG_DIR"] = str(log_dir)
            base_env["PYTHONPATH"] = str(APPIUM_DIR) + (
                os.pathsep + base_env["PYTHONPATH"] if base_env.get("PYTHONPATH") else ""
            )
            base_env["PYTHONDONTWRITEBYTECODE"] = "1"

            procs: list[tuple[int, subprocess.Popen[str], Path]] = []
            for idx in range(3):
                junit = root / f"shard-{idx}.xml"
                w_env = dict(base_env, KREMA_E2E_SHARD=f"{idx}/4", SCHED_TEST_WORKER=f"w{idx}")
                proc = subprocess.Popen(
                    [sys.executable, "-m", "pytest", f"--junitxml={junit}"],
                    cwd=suite,
                    env=w_env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                procs.append((idx, proc, junit))

            for _, proc, _ in procs:
                out, err = proc.communicate(timeout=30)
                self.assertEqual(proc.returncode, 0, err + out)

            # Worker 3 joins after the queue is already drained (0 claimed items).
            late_junit = root / "shard-3.xml"
            late_res = _run_pytest(
                suite,
                [f"--junitxml={late_junit}"],
                {
                    "KREMA_E2E_WORKQUEUE": str(queue_dir),
                    "KREMA_E2E_SHARD": "3/4",
                    "SCHED_TEST_LOG_DIR": str(log_dir),
                    "SCHED_TEST_WORKER": "w3",
                },
            )
            self.assertEqual(late_res.returncode, 0, late_res.stderr + late_res.stdout)
            self.assertEqual(_junit_cases(late_junit), {})
            self.assertFalse((log_dir / "w3.jsonl").exists())

            combined: dict[str, str] = {}
            total_seen = 0
            for idx in range(4):
                part = _junit_cases(root / f"shard-{idx}.xml")
                total_seen += len(part)
                for key, status in part.items():
                    self.assertNotIn(key, combined, f"duplicate execution of {key}")
                    combined[key] = status
            self.assertEqual(total_seen, len(expected_cases))
            self.assertEqual(combined, expected_cases)

            for idx in range(3):
                log_file = log_dir / f"w{idx}.jsonl"
                if not log_file.exists():
                    continue
                events = [json.loads(line)["event"] for line in log_file.read_text(encoding="utf-8").splitlines()]
                self.assertEqual(events[0], "session_setup")
                self.assertEqual(events[-1], "session_teardown")
                self.assertEqual(events.count("session_setup"), 1)
                self.assertEqual(events.count("session_teardown"), 1)
                open_scopes: list[str] = []
                for ev in events[1:-1]:
                    kind, target = ev.split(":", 1)
                    if kind.endswith("_setup"):
                        open_scopes.append(f"{kind[:-6]}:{target}")
                    elif kind.endswith("_teardown"):
                        expected_scope = f"{kind[:-9]}:{target}"
                        self.assertEqual(open_scopes.pop(), expected_scope)
                self.assertEqual(open_scopes, [])

    def test_workqueue_propagates_failure_and_tears_down_session(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            suite = _write_suite(root)
            queue_dir = root / "workqueue"
            log_dir = root / "logs"
            queue_dir.mkdir()
            log_dir.mkdir()
            res = _run_pytest(
                suite,
                ["-x"],
                {
                    "KREMA_E2E_WORKQUEUE": str(queue_dir),
                    "KREMA_E2E_SHARD": "0/1",
                    "SCHED_TEST_LOG_DIR": str(log_dir),
                    "SCHED_TEST_WORKER": "w0",
                    "SCHED_TEST_FAIL_G2": "1",
                },
            )
            self.assertNotEqual(res.returncode, 0)
            events = [
                json.loads(line)["event"]
                for line in (log_dir / "w0.jsonl").read_text(encoding="utf-8").splitlines()
            ]
            self.assertEqual(events[0], "session_setup")
            self.assertEqual(events[-1], "session_teardown")


class EventDrivenOraclePumpTests(unittest.TestCase):
    @staticmethod
    def _headless_oracle() -> kwin._Oracle:
        oracle = kwin._Oracle.__new__(kwin._Oracle)
        oracle.results = {}
        return oracle

    def test_pump_until_wakes_immediately_on_glib_callback(self) -> None:
        oracle = self._headless_oracle()
        token = "t_wakeup_1"

        def _deliver() -> bool:
            oracle.results[token] = '{"ok": true}'
            return GLib.SOURCE_REMOVE

        GLib.timeout_add(15, _deliver)
        t0 = time.monotonic()
        payload = oracle._pump_until(token, timeout=5.0)
        elapsed = time.monotonic() - t0
        self.assertEqual(payload, '{"ok": true}')
        self.assertLess(elapsed, 1.0)
        self.assertNotIn(token, oracle.results)

    def test_pump_until_times_out_cleanly_when_unanswered(self) -> None:
        oracle = self._headless_oracle()
        t0 = time.monotonic()
        with self.assertRaises(TimeoutError):
            oracle._pump_until("t_missing", timeout=0.05)
        elapsed = time.monotonic() - t0
        self.assertGreaterEqual(elapsed, 0.04)
        self.assertLess(elapsed, 1.0)

if __name__ == "__main__":
    unittest.main()
