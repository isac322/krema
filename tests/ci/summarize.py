#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Render a run into markdown for the CI job summary.

GitHub Actions artifacts cannot be played in the browser — they download as a
zip — so the numbers a reviewer needs have to be on the job page itself. This
emits those: which QA items failed, what was expected, what was measured. The
videos are for when the numbers are not enough.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def result_failed(result: dict) -> bool:
    return (bool(result.get('scenario_failures'))
            or result.get('repro_ok') is False
            or any(item.get('failures') for item in result.get('qa', [])))


def scenario_status(result: dict) -> str:
    if result_failed(result):
        return '❌ Failed'
    if result.get('not_verified') and not result.get('qa'):
        return '⚪ Not verified'
    if result.get('not_verified'):
        return f"✅ Passed; ⚪ {len(result['not_verified'])} not verified"
    return '✅ Passed'


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-dir', required=True, type=pathlib.Path)
    parser.add_argument('--src', type=pathlib.Path, default=pathlib.Path('.'))
    args = parser.parse_args()

    results = [json.loads(p.read_text()) for p in sorted(args.run_dir.glob('*/result.json'))]
    if not results:
        print('No scenario results found.')
        return 0

    qa_groups = [(result, item) for result in results for item in result.get('qa', [])]
    failed_qa = [(result, item) for result, item in qa_groups if item.get('failures')]
    assertions = sum(result.get('assertions', len(result.get('qa', [])))
                     for result in results)
    unique_qa = {item['id'] for _, item in qa_groups}
    not_verified = [(result, item) for result in results
                    for item in result.get('not_verified', [])]
    broken = [result for result in results if result_failed(result)]
    videos = sorted(args.run_dir.glob('*/*.mp4'))

    out: list[str] = ['## UI frame tests', '']
    icon = '❌' if broken else '✅'
    out.append(f'{icon} **{len(qa_groups) - len(failed_qa)}/{len(qa_groups)}** '
               f'QA result groups passed from {assertions} assertions '
               f'({len(unique_qa)} unique QA IDs) across {len(results)} scenarios; '
               f'{len(not_verified)} QA rows are not verified.')
    out.append('')

    if failed_qa:
        out += ['### Assertion failures', '',
                '| QA | Scenario | Expected | Measured |',
                '|---|---|---|---|']
        for result, item in failed_qa:
            expectation = (item.get('expectation') or result.get('description', ''))[:110]
            for failure in item['failures']:
                out.append(f"| `{item['id']}` | {result['scenario']} | {expectation} | "
                           f"{failure[:160]} |")
        out.append('')

    harness_failures = []
    for result in results:
        for failure in result.get('scenario_failures', []):
            harness_failures.append((result['scenario'], 'Scenario', failure))
        for failure in result.get('repro_failures', []):
            harness_failures.append((result['scenario'], 'Reproducibility', failure))
    if harness_failures:
        out += ['### Harness failures', '',
                '| Scenario | Gate | Failure |', '|---|---|---|']
        for scenario, gate, failure in harness_failures:
            out.append(f'| {scenario} | {gate} | {failure[:200]} |')
        out.append('')

    if not_verified:
        out += [f'### Not verified here ({len(not_verified)})', '',
                '| QA | Scenario | Why |', '|---|---|---|']
        for result, item in not_verified:
            out.append(f"| `{item['id']}` | {result['scenario']} | {item['reason']} |")
        out.append('')

    compared = [result for result in results if result.get('repro_ok') is not None]
    if compared:
        reproducible = sum(1 for result in compared if result.get('repro_ok'))
        out.append(f'Reproducibility: {reproducible}/{len(compared)} scenarios have '
                   f'exact pre-action and settled states, matching transition '
                   f'envelopes, and no more than the allowed transient-frame budget. '
                   f'Frame-count, identity, endpoint, menu-state, or out-of-range '
                   f'property differences fail the scenario.')
    else:
        out.append('Reproducibility: not measured — this run captured a single pass per '
                   'scenario, so no two captures were compared.')
    out.append('')

    if videos:
        out.append(f'{len(videos)} annotated videos in the `frame-captures` artifact — '
                   f'download to play. Every frame is stamped with its number, virtual '
                   f'time, the action firing on it and any assertion anchored to it, so a '
                   f'failing frame number above can be scrubbed to directly.')
        out.append('')

    out += ['<details><summary>All scenarios</summary>', '',
            '| Scenario | Status | QA groups | Assertions | Frames | Repro |',
            '|---|---|---:|---:|---:|---|']
    for result in results:
        bad = sum(1 for item in result.get('qa', []) if item.get('failures'))
        qa = result.get('qa', [])
        out.append(f"| {result['scenario']} | {scenario_status(result)} | "
                   f"{len(qa) - bad}/{len(qa)} | "
                   f"{result.get('assertions', len(qa))} | {result['frames']} | "
                   f"{result.get('repro', 'unknown')[:80]} |")
    out += ['', '</details>']

    print('\n'.join(out))
    return 0

if __name__ == '__main__':
    sys.exit(main())
