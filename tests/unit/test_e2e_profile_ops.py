# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Optional profiler contracts without AT-SPI, GI, or wall-clock thresholds."""

from __future__ import annotations

import atexit
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import threading
import types
import unittest
from unittest import mock


PROFILE_PATH = Path(__file__).resolve().parents[1] / "appium/krema_e2e/profile_ops.py"
PYTEST_AVAILABLE = importlib.util.find_spec("pytest") is not None
SKIP_REASON = "pytest is required for profiler contract tests"

# Import the file, not krema_e2e: the package eagerly imports Linux GI helpers.
if PYTEST_AVAILABLE:
    spec = importlib.util.spec_from_file_location("krema_profile_ops_test", PROFILE_PATH)
    profile_ops = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = profile_ops
    previous_bytecode_setting = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        spec.loader.exec_module(profile_ops)
    finally:
        sys.dont_write_bytecode = previous_bytecode_setting
    # Unit tests must never emit a profile to the production /artifacts default.
    atexit.unregister(profile_ops._flush_at_exit)


class Clock:
    def __init__(self):
        self.now = 0

    def monotonic_ns(self):
        return self.now

    def advance(self, nanoseconds):
        self.now += nanoseconds


@unittest.skipUnless(PYTEST_AVAILABLE, SKIP_REASON)
class ProfileOpsTests(unittest.TestCase):
    def setUp(self):
        self.state = profile_ops._State()
        self.clock = Clock()
        state_patch = mock.patch.object(profile_ops, "_STATE", self.state)
        clock_patch = mock.patch.object(profile_ops, "_NOW", self.clock.monotonic_ns)
        state_patch.start()
        clock_patch.start()
        self.addCleanup(state_patch.stop)
        self.addCleanup(clock_patch.stop)

    def assert_bucket(self, key, *, calls, inclusive, own, failures=0, exceptions=None):
        bucket = self.state.aggregate[key]
        self.assertEqual(bucket["calls"], calls)
        self.assertEqual(bucket["inclusive_ns"], inclusive)
        self.assertEqual(bucket["self_ns"], own)
        self.assertEqual(bucket["failures"], failures)
        self.assertEqual(bucket["exceptions"], exceptions or {})

    def test_nested_self_times_do_not_double_count_descendants(self):
        def leaf():
            self.clock.advance(5)

        leaf = profile_ops._wrap(leaf, "leaf")

        def child():
            self.clock.advance(3)
            leaf()
            self.clock.advance(2)

        child = profile_ops._wrap(child, "child")

        def parent():
            self.clock.advance(7)
            child()
            self.clock.advance(4)
            child()
            self.clock.advance(9)
            return "result"

        self.assertEqual(profile_ops._wrap(parent, "parent")(), "result")
        self.assert_bucket("leaf", calls=2, inclusive=10, own=10)
        self.assert_bucket("child", calls=2, inclusive=20, own=10)
        self.assert_bucket("parent", calls=1, inclusive=40, own=20)
        self.assertEqual(sum(row["self_ns"] for row in self.state.aggregate.values()), 40)
        self.assertEqual(self.state.aggregate["child"]["max_inclusive_ns"], 10)
        self.assertEqual(self.state.stack(), [])

    def test_wrapper_executes_once_preserving_arguments_and_return_identity(self):
        result = object()
        operation = mock.Mock(return_value=result)
        wrapped = profile_ops._wrap(operation, "operation")
        self.assertIs(wrapped("argument", flag=True), result)
        operation.assert_called_once_with("argument", flag=True)
        self.assertEqual(self.state.aggregate["operation"]["calls"], 1)
        self.assertIs(profile_ops._wrap(wrapped, "duplicate"), wrapped)

    def test_wrapper_preserves_identical_exception_and_counts_one_failure(self):
        expected = ValueError("original failure")
        operation = mock.Mock(side_effect=expected)
        with self.assertRaises(ValueError) as caught:
            profile_ops._wrap(operation, "operation")()
        self.assertIs(caught.exception, expected)
        operation.assert_called_once_with()
        self.assert_bucket(
            "operation", calls=1, inclusive=0, own=0,
            failures=1, exceptions={"ValueError": 1},
        )

    def test_clock_failures_preserve_both_success_and_operation_error(self):
        # A start-clock failure must not skip/retry the operation. A finish-clock
        # failure must not replace its return or exception, nor strand a frame.
        for fail_at in ("start", "finish"):
            for fails in (False, True):
                with self.subTest(fail_at=fail_at, operation_fails=fails):
                    state = profile_ops._State()
                    expected = LookupError("operation failure")
                    result = object()
                    operation = mock.Mock(side_effect=expected) if fails else mock.Mock(return_value=result)
                    ticks = [RuntimeError("clock failure")] if fail_at == "start" else [0, RuntimeError("clock failure")]
                    with mock.patch.object(profile_ops, "_STATE", state), mock.patch.object(
                        profile_ops, "_NOW", side_effect=ticks,
                    ):
                        if fails:
                            with self.assertRaises(LookupError) as caught:
                                profile_ops._wrap(operation, "operation")()
                            self.assertIs(caught.exception, expected)
                        else:
                            self.assertIs(profile_ops._wrap(operation, "operation")(), result)
                    operation.assert_called_once_with()
                    state.finished_ns = 0
                    self.assertEqual(state.stack(), [])
                    self.assertEqual(state.aggregate, {})
                    self.assertEqual(state.errors, [{
                        "operation": "begin_call" if fail_at == "start" else "finish_call",
                        "exception": "RuntimeError",
                    }])
                    self.assertFalse(state.snapshot()["measurements_complete"])

    def test_aggregation_failure_preserves_both_success_and_operation_error(self):
        for fails in (False, True):
            with self.subTest(operation_fails=fails):
                state = profile_ops._State()
                expected = OSError("operation failure")
                result = object()
                operation = mock.Mock(side_effect=expected) if fails else mock.Mock(return_value=result)
                with mock.patch.object(profile_ops, "_STATE", state), mock.patch.object(
                    state, "_add", side_effect=RuntimeError("aggregation failure"),
                ):
                    if fails:
                        with self.assertRaises(OSError) as caught:
                            profile_ops._wrap(operation, "operation")()
                        self.assertIs(caught.exception, expected)
                    else:
                        self.assertIs(profile_ops._wrap(operation, "operation")(), result)
                operation.assert_called_once_with()
                self.assertEqual(state.stack(), [])
                self.assertEqual(state.aggregate, {})
                self.assertEqual(state.errors, [{
                    "operation": "finish_call", "exception": "RuntimeError",
                }])
                self.assertFalse(state.snapshot()["measurements_complete"])

    def test_failed_measurement_keeps_successful_measurements_but_marks_run_incomplete(self):
        def measured():
            self.clock.advance(5)
            return "kept"

        def unmeasured():
            self.clock.advance(7)
            return "still executed"

        self.assertEqual(profile_ops._wrap(measured, "measured")(), "kept")
        self.assertTrue(self.state.snapshot()["measurements_complete"])
        with mock.patch.object(profile_ops, "_NOW", side_effect=RuntimeError("clock failure")):
            self.assertEqual(profile_ops._wrap(unmeasured, "unmeasured")(), "still executed")
        self.state.finished_ns = self.clock.now
        payload = self.state.snapshot()
        self.assertFalse(payload["measurements_complete"])
        self.assertEqual(set(payload["aggregates"]), {"measured"})
        self.assert_bucket("measured", calls=1, inclusive=5, own=5)
        self.assertEqual(payload["errors"], [{
            "operation": "begin_call", "exception": "RuntimeError",
        }])
        self.assertEqual(payload["omitted"], [])
        self.assertIsNone(payload["install_error"])

    def test_wait_aliases_share_canonical_wrapper_and_repatch_is_idempotent(self):
        calls = {"wait_until": 0, "wait_stable": 0}
        result = object()

        def wait_until():
            calls["wait_until"] += 1
            self.clock.advance(3)
            return result

        def wait_stable():
            calls["wait_stable"] += 1
            self.clock.advance(5)
            return result

        waits = types.ModuleType("krema_e2e.waits")
        package = types.ModuleType("krema_e2e")
        helper = types.ModuleType("krema_e2e.profile_aliases")
        waits.wait_until = package.wait_until = helper.until_alias = wait_until
        waits.wait_stable = package.wait_stable = helper.stable_alias = wait_stable
        modules = {module.__name__: module for module in (waits, package, helper)}
        with mock.patch.dict(sys.modules, modules):
            profile_ops._patch_wait_aliases(waits)
            until_wrapper = waits.wait_until
            stable_wrapper = waits.wait_stable
            profile_ops._patch_wait_aliases(waits)
            until_bindings = (waits.wait_until, package.wait_until, helper.until_alias)
            stable_bindings = (waits.wait_stable, package.wait_stable, helper.stable_alias)
            self.assertIsNot(until_wrapper, wait_until)
            self.assertIsNot(stable_wrapper, wait_stable)
            for name, bindings, wrapper in (
                ("wait_until", until_bindings, until_wrapper),
                ("wait_stable", stable_bindings, stable_wrapper),
            ):
                for index, binding in enumerate(bindings, 1):
                    self.assertIs(binding, wrapper)
                    self.assertIs(binding(), result)
                    self.assertEqual(calls[name], index)
        self.assertEqual(set(self.state.aggregate), {"waits.wait_until", "waits.wait_stable"})
        self.assert_bucket("waits.wait_until", calls=3, inclusive=9, own=9)
        self.assert_bucket("waits.wait_stable", calls=3, inclusive=15, own=15)
        self.assertEqual(self.state.errors, [])
        self.assertEqual(self.state.stack(), [])

    def test_background_operation_is_aggregate_only_during_main_thread_node(self):
        result = object()
        received = []

        def background():
            self.clock.advance(7)
            return result

        wrapped = profile_ops._wrap(background, "background")

        def foreground():
            self.clock.advance(3)
            thread = threading.Thread(target=lambda: received.append(wrapped()))
            thread.start()
            thread.join()

        previous = self.state.enter_node(types.SimpleNamespace(nodeid="main-thread-test"))
        try:
            profile_ops._wrap(foreground, "foreground")()
        finally:
            self.state.leave_node(previous)
        self.assertEqual(received, [result])
        self.assert_bucket("background", calls=1, inclusive=7, own=7)
        self.assert_bucket("foreground", calls=1, inclusive=10, own=10)
        self.assertEqual(set(self.state.nodes), {"main-thread-test"})
        node_profile = self.state.nodes["main-thread-test"]["profile"]
        self.assertEqual(set(node_profile), {"foreground"})
        self.assertEqual(node_profile["foreground"], self.state.aggregate["foreground"])
        self.assertEqual(self.state.errors, [])
        self.assertEqual(self.state.stack(), [])

    def test_write_failures_do_not_mask_an_active_operation_error(self):
        expected = RuntimeError("operation failure")
        operation = mock.Mock(side_effect=expected)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "profile.json"
            env = {"KREMA_E2E_PROFILE_OPS_OUTPUT": str(output)}
            with mock.patch.dict(os.environ, env), mock.patch.object(
                Path, "write_text", side_effect=OSError("write failure")
            ):
                try:
                    profile_ops._wrap(operation, "operation")()
                except RuntimeError as caught:
                    self.state.flush(1)
                    self.assertIs(caught, expected)
                else:
                    self.fail("operation failure was swallowed")
        operation.assert_called_once_with()
        self.assertTrue(any(row["operation"] == "flush" for row in self.state.errors))
        self.assertEqual(self.state.aggregate["operation"]["failures"], 1)

    def test_key_factory_failure_does_not_prevent_operation(self):
        result = object()
        operation = mock.Mock(return_value=result)
        broken_key = mock.Mock(side_effect=RuntimeError("bad key"))
        self.assertIs(profile_ops._wrap(operation, "fallback", broken_key)(), result)
        operation.assert_called_once_with()
        self.assertEqual(self.state.aggregate["fallback"]["calls"], 1)

    def test_context_factory_is_immediate_and_yield_exit_happen_once(self):
        events = []
        result = object()

        @contextlib.contextmanager
        def context():
            events.append("enter")
            self.clock.advance(3)
            yield result
            self.clock.advance(5)
            events.append("exit")

        factory = mock.Mock(side_effect=context)
        timed = profile_ops._wrap_contextmanager(factory, "context")()
        factory.assert_called_once_with()
        self.assertEqual(events, [])
        with timed as value:
            self.assertIs(value, result)
            self.clock.advance(7)
        self.assertEqual(events, ["enter", "exit"])
        self.assert_bucket("context", calls=1, inclusive=15, own=15)

    def test_context_factory_error_preserves_identity_and_runs_once(self):
        expected = ValueError("factory failure")
        factory = mock.Mock(side_effect=expected)
        with self.assertRaises(ValueError) as caught:
            profile_ops._wrap_contextmanager(factory, "context")()
        self.assertIs(caught.exception, expected)
        factory.assert_called_once_with()
        self.assertEqual(self.state.aggregate, {})

    def test_context_preserves_unsuppressed_body_exception(self):
        expected = LookupError("body failure")
        exits = []

        @contextlib.contextmanager
        def context():
            try:
                yield
            finally:
                exits.append("exit")

        with self.assertRaises(LookupError) as caught:
            with profile_ops._wrap_contextmanager(context, "context")():
                raise expected
        self.assertIs(caught.exception, expected)
        self.assertEqual(exits, ["exit"])
        self.assert_bucket(
            "context", calls=1, inclusive=0, own=0,
            failures=1, exceptions={"LookupError": 1},
        )

    def test_context_preserves_suppression_and_exception_identity_at_exit(self):
        expected = ValueError("suppressed body error")
        seen = []

        @contextlib.contextmanager
        def context():
            try:
                yield "resource"
            except ValueError as caught:
                seen.append(caught)

        with profile_ops._wrap_contextmanager(context, "context")() as value:
            self.assertEqual(value, "resource")
            raise expected
        self.assertEqual(len(seen), 1)
        self.assertIs(seen[0], expected)
        self.assert_bucket("context", calls=1, inclusive=0, own=0, failures=0)
        self.assertEqual(self.state.stack(), [])

    def test_context_enter_error_preserves_exception_and_does_not_run_body_or_exit(self):
        expected = RuntimeError("enter failure")
        events = []

        class Context:
            def __enter__(self):
                events.append("enter")
                raise expected

            def __exit__(self, *_):
                events.append("exit")

        with self.assertRaises(RuntimeError) as caught:
            with profile_ops._wrap_contextmanager(Context, "context")():
                events.append("body")
        self.assertIs(caught.exception, expected)
        self.assertEqual(events, ["enter"])
        self.assert_bucket(
            "context", calls=1, inclusive=0, own=0,
            failures=1, exceptions={"RuntimeError": 1},
        )
        self.assertEqual(self.state.stack(), [])

    def test_context_exit_error_dominates_body_result_or_exception(self):
        for body_fails in (False, True):
            with self.subTest(body_fails=body_fails):
                state = profile_ops._State()
                expected = OSError("exit failure")
                exits = []

                @contextlib.contextmanager
                def context():
                    try:
                        yield "resource"
                    finally:
                        exits.append("exit")
                        raise expected

                with mock.patch.object(profile_ops, "_STATE", state):
                    with self.assertRaises(OSError) as caught:
                        with profile_ops._wrap_contextmanager(context, "context")():
                            if body_fails:
                                raise ValueError("body failure")
                self.assertIs(caught.exception, expected)
                self.assertEqual(exits, ["exit"])
                self.assertEqual(state.aggregate["context"]["calls"], 1)
                self.assertEqual(state.aggregate["context"]["failures"], 1)
                self.assertEqual(state.aggregate["context"]["exceptions"], {"OSError": 1})
                self.assertEqual(state.stack(), [])

    def test_context_timing_failure_does_not_mask_exit_failure(self):
        expected = OSError("exit failure")

        @contextlib.contextmanager
        def context():
            yield "resource"
            raise expected

        with mock.patch.object(self.state, "_add", side_effect=RuntimeError("aggregation failure")):
            with self.assertRaises(OSError) as caught:
                with profile_ops._wrap_contextmanager(context, "context")():
                    pass
        self.assertIs(caught.exception, expected)
        self.assertEqual(self.state.stack(), [])


@unittest.skipUnless(PYTEST_AVAILABLE, SKIP_REASON)
class PytestHookSmokeTests(unittest.TestCase):
    def test_real_hooks_keep_failure_exit_and_record_every_phase(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            test_file = root / "test_profile_contract.py"
            test_file.write_text(textwrap.dedent("""\
                import pytest

                @pytest.mark.outputs(3)
                def test_pass():
                    pass

                def test_call_failure():
                    assert False, "original call failure"

                @pytest.fixture
                def setup_failure():
                    raise RuntimeError("original setup failure")

                def test_setup_failure(setup_failure):
                    raise AssertionError("body must not execute")

                @pytest.fixture
                def teardown_failure():
                    yield
                    raise RuntimeError("original teardown failure")

                def test_teardown_failure(teardown_failure):
                    pass

                @pytest.mark.skip(reason="intentional skip reason")
                def test_skipped():
                    pass

                @pytest.mark.xfail(reason="expected call failure")
                def test_xfailed():
                    assert False, "expected failure body"

                @pytest.mark.xfail(reason="unexpected pass")
                def test_xpassed():
                    pass

                @pytest.mark.xfail(reason="strict unexpected pass", strict=True)
                def test_strict_xpassed():
                    pass

                @pytest.mark.outputs(2)
                def test_deselected():
                    raise AssertionError("deselected body must not execute")
                """), encoding="utf-8")
            output = root / "profile.json"
            fake_package = root / "fake_imports" / "krema_e2e"
            fake_package.mkdir(parents=True)
            (fake_package / "__init__.py").write_text("", encoding="utf-8")
            env = os.environ.copy()
            env.update({
                "PYTHONPATH": os.pathsep.join((str(fake_package.parent), str(PROFILE_PATH.parent))),
                "PYTHONDONTWRITEBYTECODE": "1",
                "PYTEST_DISABLE_PLUGIN_AUTOLOAD": "1",
                "PYTEST_ADDOPTS": "",
                "KREMA_E2E_PROFILE_OPS_OUTPUT": str(output),
                "KREMA_E2E_OUTPUT_COUNT": "3",
            })
            command = [
                sys.executable, "-m", "pytest", "-p", "profile_ops", "-p", "no:cacheprovider",
                "-q", "-k", "not deselected", str(test_file),
            ]
            result = subprocess.run(
                command, cwd=root, env=env, capture_output=True, text=True,
                timeout=20, check=False,
            )
            details = result.stdout + result.stderr
            self.assertEqual(result.returncode, 1, details)
            self.assertTrue(output.is_file(), details)
            payload = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(payload["exitstatus"], 1)
        manifest = payload["collection_manifest"]
        self.assertEqual(payload["selected_nodeids"], [entry["nodeid"] for entry in manifest])
        by_name = {entry["nodeid"].rsplit("::", 1)[-1]: entry for entry in manifest}
        expected_names = {
            "test_pass", "test_call_failure", "test_setup_failure",
            "test_teardown_failure", "test_skipped", "test_xfailed",
            "test_xpassed", "test_strict_xpassed",
        }
        self.assertEqual(set(by_name), expected_names)
        self.assertEqual(by_name["test_pass"]["requested_outputs"], 3)
        self.assertTrue(all(entry["session_outputs"] == 3 for entry in manifest))
        full_manifest = {
            entry["nodeid"].rsplit("::", 1)[-1]: entry
            for entry in payload["full_collection_manifest"]
        }
        self.assertEqual(set(full_manifest), expected_names | {"test_deselected"})
        self.assertEqual(full_manifest["test_deselected"]["requested_outputs"], 2)
        self.assertFalse(full_manifest["test_deselected"]["selected"])
        self.assertTrue(all(full_manifest[name]["selected"] for name in expected_names))
        nodes = {nodeid.rsplit("::", 1)[-1]: node for nodeid, node in payload["nodes"].items()}
        self.assertEqual(set(nodes), expected_names)
        self.assertEqual(nodes["test_pass"]["status"], "passed")
        self.assertEqual(nodes["test_skipped"]["status"], "skipped")
        self.assertIn(
            "intentional skip reason",
            nodes["test_skipped"]["phases"]["setup"]["skip_reason"],
        )
        self.assertEqual(nodes["test_xfailed"]["status"], "xfailed")
        self.assertEqual(nodes["test_xpassed"]["status"], "xpassed")
        self.assertEqual(nodes["test_strict_xpassed"]["status"], "failed")
        for name in ("test_call_failure", "test_setup_failure", "test_teardown_failure", "test_strict_xpassed"):
            self.assertEqual(nodes[name]["status"], "failed", name)
        call_failure = nodes["test_call_failure"]["phases"]["call"]
        self.assertEqual(call_failure["outcome"], "failed")
        self.assertEqual(call_failure["failure_kind"], "failure")
        self.assertIn("original call failure", call_failure["failure_reason"])
        setup_failure = nodes["test_setup_failure"]["phases"]["setup"]
        self.assertEqual(setup_failure["outcome"], "failed")
        self.assertEqual(setup_failure["failure_kind"], "error")
        self.assertIn("original setup failure", setup_failure["failure_reason"])
        self.assertNotIn("call", nodes["test_setup_failure"]["phases"])
        self.assertEqual(nodes["test_teardown_failure"]["phases"]["call"]["outcome"], "passed")
        teardown_failure = nodes["test_teardown_failure"]["phases"]["teardown"]
        self.assertEqual(teardown_failure["outcome"], "failed")
        self.assertEqual(teardown_failure["failure_kind"], "error")
        self.assertIn("original teardown failure", teardown_failure["failure_reason"])
        xfailed = nodes["test_xfailed"]["phases"]["call"]
        self.assertEqual(xfailed["outcome"], "skipped")
        self.assertEqual(xfailed["xfail_reason"], "expected call failure")
        self.assertIsNone(xfailed["failure_kind"])
        self.assertIsNone(xfailed["failure_reason"])
        xpassed = nodes["test_xpassed"]["phases"]["call"]
        self.assertEqual(xpassed["outcome"], "passed")
        self.assertEqual(xpassed["xfail_reason"], "unexpected pass")
        self.assertIsNone(xpassed["failure_kind"])
        self.assertIsNone(xpassed["failure_reason"])
        strict_xpassed = nodes["test_strict_xpassed"]["phases"]["call"]
        self.assertEqual(strict_xpassed["outcome"], "failed")
        self.assertEqual(strict_xpassed["failure_kind"], "failure")
        self.assertIn("strict unexpected pass", strict_xpassed["failure_reason"])
        for name in ("test_pass", "test_call_failure", "test_teardown_failure", "test_xfailed", "test_xpassed", "test_strict_xpassed"):
            self.assertEqual(set(nodes[name]["phases"]), {"setup", "call", "teardown"})
        self.assertEqual(payload["result_counts"], {
            "passed": 1, "failed": 4, "skipped": 1, "xfailed": 1, "xpassed": 1, "unknown": 0,
        })
        self.assertFalse(payload["measurements_complete"])
        self.assertTrue(payload["omitted"])
        self.assertTrue(payload["errors"])


if __name__ == "__main__":
    unittest.main()
