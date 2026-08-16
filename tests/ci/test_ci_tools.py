#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

CI_DIR = pathlib.Path(__file__).resolve().parent
ROOT = CI_DIR.parent.parent
sys.path.insert(0, str(CI_DIR))

import assert_frames  # noqa: E402
import make_previews  # noqa: E402
import qa_coverage  # noqa: E402


def frame(number: int, *, x: float = 0.0, name: str = 'dockItem0', vt: int | None = None) -> dict:
    return {
        'frame': number,
        'vt': number * 16 if vt is None else vt,
        'windows': [{
            'key': 'krema::DockView',
            'target': True,
            'ox': 0,
            'oy': 0,
            'visible': True,
            'w': 100,
            'h': 80,
            'items': [{
                'path': '0/0',
                'type': 'DockItem',
                'name': name,
                'x': x,
                'y': 0,
                'w': 48,
                'h': 48,
                'scale': 1.0,
                'opacity': 1.0,
                'rotation': 0.0,
                'visible': True,
                'props': {'currentScale': 1.0},
            }],
        }],
    }


class ReproducibilityTests(unittest.TestCase):
    def test_identical_payload_ignores_virtual_time(self):
        reference = [frame(1, vt=16), frame(2, x=1, vt=32)]
        candidate = [frame(1, vt=999), frame(2, x=1, vt=1000)]
        self.assertEqual([], assert_frames.reproducibility_failures(reference, candidate))

    def test_shifted_payload_fails(self):
        reference = [frame(1, x=0), frame(2, x=1), frame(3, x=2)]
        candidate = [frame(1, x=1), frame(2, x=2), frame(3, x=2)]
        failures = assert_frames.reproducibility_failures(reference, candidate)
        self.assertTrue(failures)
        self.assertIn('capture.windows[0].items[0].x', failures[0])

    def test_frame_count_fails(self):
        failures = assert_frames.reproducibility_failures(
            [frame(1), frame(2)], [frame(1)])
        self.assertIn('frame count 1, expected 2', failures[0])

    def test_item_identity_fails(self):
        failures = assert_frames.reproducibility_failures(
            [frame(1, name='dockItem0')], [frame(1, name='dockItem1')])
        self.assertTrue(any('.name' in failure for failure in failures))

    def test_large_numeric_divergence_fails(self):
        failures = assert_frames.reproducibility_failures(
            [frame(1, x=0)], [frame(1, x=1000)])
        self.assertTrue(any('1000' in failure for failure in failures))

    def test_cli_returns_nonzero_for_reproducibility_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            scenario = root / 'scenario.json'
            scenario.write_text(json.dumps({
                'description': 'synthetic reproducibility gate',
                'actions': [],
                'assertions': [{
                    'qa': 'QA-TEST-001',
                    'kind': 'equals',
                    'item': 'dockItem0',
                    'prop': 'x',
                    'frame': 1,
                    'value': 0,
                }],
            }))
            run = root / 'synthetic'
            for pass_name, row in (('pass1', frame(1, x=0)),
                                   ('pass2', frame(1, x=1000))):
                pass_dir = run / pass_name
                pass_dir.mkdir(parents=True)
                (pass_dir / 'frames.ndjson').write_text(json.dumps(row) + '\n')

            completed = subprocess.run(
                [sys.executable, str(CI_DIR / 'assert_frames.py'),
                 '--scenario', str(scenario), '--run-dir', str(run)],
                capture_output=True, text=True, check=False)
            self.assertEqual(1, completed.returncode, completed.stdout + completed.stderr)
            result = json.loads((run / 'result.json').read_text())
            self.assertFalse(result['repro_ok'])
            self.assertTrue(result['repro_failures'])

    def test_empty_scenario_requires_not_verified_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            scenario = root / 'scenario.json'
            scenario.write_text(json.dumps({'actions': [], 'assertions': []}))
            run = root / 'empty'
            for pass_name in ('pass1', 'pass2'):
                pass_dir = run / pass_name
                pass_dir.mkdir(parents=True)
                (pass_dir / 'frames.ndjson').write_text(json.dumps(frame(1)) + '\n')
            completed = subprocess.run(
                [sys.executable, str(CI_DIR / 'assert_frames.py'),
                 '--scenario', str(scenario), '--run-dir', str(run)],
                capture_output=True, text=True, check=False)
            self.assertEqual(1, completed.returncode)
            result = json.loads((run / 'result.json').read_text())
            self.assertTrue(result['scenario_failures'])


class MenuAndReportingTests(unittest.TestCase):
    def test_menu_requires_mapped_surface(self):
        row = frame(1)
        row['menu'] = {'visible': True, 'mapped': False, 'entries': []}
        failures = []
        assert_frames.check_menu(
            [row], {'kind': 'menu', 'frame': 1, 'mapped': True}, failures.append)
        self.assertEqual(['menu mapped at frame 1: False, expected True'], failures)

    def test_menu_absence_is_assertable(self):
        failures = []
        assert_frames.check_menu(
            [frame(1)], {'kind': 'menu', 'frame': 1, 'exists': False},
            failures.append)
        self.assertEqual([], failures)

    def test_native_context_menu_clicks_wait_for_mapping(self):
        for path in (ROOT / 'tests/ci/scenarios').glob('*.json'):
            scenario = json.loads(path.read_text())
            for action in scenario.get('actions', []):
                opens_menu = (action.get('type') == 'click'
                              and action.get('button') == 'right'
                              and action.get('native')
                              and action.get('item'))
                if opens_menu:
                    self.assertTrue(
                        action.get('wait_for_menu'),
                        f'{path.name} frame {action["frame"]} has no menu barrier')

    def test_not_verified_scenario_is_neutral_in_preview(self):
        result = {
            'scenario': 'neutral',
            'description': 'not observable in this fixture',
            'assertions': 0,
            'qa': [],
            'not_verified': [{'id': 'QA-ITEM-001', 'reason': 'fixture missing'}],
            'scenario_failures': [],
            'repro_ok': True,
            'repro_failures': [],
        }
        body = make_previews.render([result], {}, '', failures_only=False)
        self.assertIn('**⚪ neutral**', body)
        self.assertNotIn('**✅ neutral**', body)
        self.assertIn('1 QA rows are not verified', body)


class CoverageTests(unittest.TestCase):
    def test_checklist_table_is_generated_from_all_items(self):
        path = ROOT / 'tests/ci/qa-checklist.md'
        text = path.read_text()
        items = qa_coverage.parse_items(text)
        self.assertEqual(140, len(items))
        counts = qa_coverage.coverage(items)
        totals = {tag: sum(row[tag] for row in counts.values())
                  for tag in qa_coverage.AUTOMATION}
        self.assertEqual({'AUTO': 39, 'AUTO-REQ': 8,
                          'FIXTURE': 79, 'MANUAL': 14}, totals)
        self.assertEqual(text, qa_coverage.replace_table(
            text, qa_coverage.render_table(items)))


if __name__ == '__main__':
    unittest.main()
