# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Waiting contracts without an AT-SPI session or wall-clock delays."""

import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "e2e_waits", Path(__file__).parents[1] / "appium/krema_e2e/waits.py"
)
waits = importlib.util.module_from_spec(spec)
spec.loader.exec_module(waits)


class Clock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class StableWaitTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.monotonic = patch.object(waits.time, "monotonic", self.clock.monotonic)
        self.sleep = patch.object(waits.time, "sleep", self.clock.sleep)
        self.monotonic.start()
        self.sleep.start()
        self.addCleanup(self.monotonic.stop)
        self.addCleanup(self.sleep.stop)

    def test_stable_invalid_observation_is_not_ready(self):
        result = waits.wait_stable(
            lambda: self.clock.now >= 1.0, duration=0.5, timeout=2, accept=bool
        )
        self.assertTrue(result)
        self.assertGreaterEqual(self.clock.now, 1.5)

    def test_rejection_restarts_stability_even_when_value_is_unchanged(self):
        result = waits.wait_stable(
            lambda: "same geometry",
            duration=0.5,
            timeout=2,
            accept=lambda _: self.clock.now < 0.2 or self.clock.now >= 1.0,
        )
        self.assertEqual(result, "same geometry")
        self.assertGreaterEqual(self.clock.now, 1.5)

    def test_invalid_observation_times_out_with_last_value(self):
        with self.assertRaisesRegex(waits.WaitTimeout, "last: False"):
            waits.wait_stable(lambda: False, duration=0.5, timeout=1, accept=bool)
        self.assertAlmostEqual(self.clock.now, 1.0)

    def test_changed_valid_observation_restarts_stability(self):
        result = waits.wait_stable(
            lambda: 1 if self.clock.now < 0.3 else 2,
            duration=0.5,
            timeout=2,
            accept=lambda value: value > 0,
        )
        self.assertEqual(result, 2)
        self.assertGreaterEqual(self.clock.now, 0.8)

    def test_default_still_returns_a_stable_false_value(self):
        self.assertFalse(waits.wait_stable(lambda: False, duration=0.5, timeout=1))
        self.assertAlmostEqual(self.clock.now, 0.5)

    def test_acceptance_errors_are_not_hidden(self):
        def broken(_):
            raise ValueError("invalid observation")

        with self.assertRaisesRegex(ValueError, "invalid observation"):
            waits.wait_stable(lambda: 1, accept=broken)


if __name__ == "__main__":
    unittest.main()
