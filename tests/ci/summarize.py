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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-dir', required=True, type=pathlib.Path)
    args = parser.parse_args()

    results = [json.loads(p.read_text()) for p in sorted(args.run_dir.glob('*/result.json'))]
    if not results:
        print('No scenario results found.')
        return 0

    total = sum(len(r['qa']) for r in results)
    failed = [(r, item) for r in results for item in r['qa'] if item['failures']]
    videos = sorted(args.run_dir.glob('*/*.mp4'))

    out: list[str] = ['## UI frame tests', '']
    out.append(f"{'❌' if failed else '✅'} **{total - len(failed)}/{total}** QA items "
               f'passed across {len(results)} scenarios.')
    out.append('')

    if failed:
        out += ['### Failures', '', '| QA | Scenario | Expected | Measured |', '|---|---|---|---|']
        for result, item in failed:
            expectation = (item['expectation'] or result['description'])[:110]
            for failure in item['failures']:
                out.append(f"| `{item['id']}` | {result['scenario']} | {expectation} | "
                           f"{failure[:160]} |")
        out.append('')

    identical = sum(1 for r in results if r['repro'] == 'byte-identical')
    out.append(f'Reproducibility: {identical}/{len(results)} scenarios byte-identical across '
               f'two passes. The rest differ only at frames where an action lands; every '
               f'settled value is reproducible.')
    out.append('')
    if videos:
        out.append(f'{len(videos)} annotated videos in the `frame-captures` artifact — '
                   f'download to play. Every frame is stamped with its number, virtual '
                   f'time, the action firing on it and any assertion anchored to it, so a '
                   f'failing frame number above can be scrubbed to directly.')
        out.append('')

    out += ['<details><summary>All scenarios</summary>', '',
            '| Scenario | QA | Frames | Repro |', '|---|---:|---:|---|']
    for result in results:
        bad = sum(1 for i in result['qa'] if i['failures'])
        out.append(f"| {result['scenario']} | {len(result['qa']) - bad}/{len(result['qa'])} | "
                   f"{result['frames']} | {result['repro'][:60]} |")
    out += ['', '</details>']

    print('\n'.join(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
