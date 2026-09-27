#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Find assertions that pass without their stimulus.

An assertion meant to prove a caused change is only worth having if removing the
cause makes it fail. This builds a negative control for every scenario -- same
config, no actions -- runs it, and reports which QA items still pass.

Not every survivor is a defect. Some assertions are invariants by design: the
icon resolves and the placeholder stays hidden, an always-visible dock stays
visible, no badge appears without a notification daemon. Those legitimately pass
with no stimulus. Every assertion is treated as claiming causation unless it sets
`"invariant": true`, so the burden is on the author to say "this is meant to
hold with nothing happening".

It also reports how much of the frame ever changes. A capture of 140 identical
frames demonstrates nothing to a reviewer no matter what its assertions say, and
the animation embedded in the pull request is then a still image.

Usage, inside the test image:
  python3 tests/ci/vacuity_check.py --src /src --out /out
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys


def frame_churn(run_dir: pathlib.Path) -> tuple[int, float]:
    """Distinct frames, and the largest changed area as a share of the frame."""
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return -1, -1.0
    frames = sorted((run_dir / 'pass1' / 'frames').glob('f*.png'))
    if not frames:
        return 0, 0.0
    distinct = len({hashlib.md5(p.read_bytes()).hexdigest() for p in frames})
    first = Image.open(frames[0]).convert('RGB')
    worst = 0
    for path in frames[1:]:
        image = Image.open(path).convert('RGB')
        if image.size != first.size:
            return distinct, 100.0
        box = ImageChops.difference(first, image).getbbox()
        if box:
            worst = max(worst, (box[2] - box[0]) * (box[3] - box[1]))
    return distinct, 100.0 * worst / (first.size[0] * first.size[1])


def passing_ids(run_dir: pathlib.Path) -> set[str]:
    result = run_dir / 'result.json'
    if not result.exists():
        return set()
    return {item['id'] for item in json.loads(result.read_text())['qa']
            if not item['failures']}


def run_suite(src: pathlib.Path, out: pathlib.Path, scenario_dir: pathlib.Path) -> None:
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    subprocess.run(
        ['bash', str(src / 'tests/ci/run-frame-tests.sh')],
        env={**os.environ,
             'KREMA_SRC': str(src),
             'KREMA_OUT': str(out),
             'KREMA_PASSES': '1',
             'KREMA_VIDEO': '0',
             'KREMA_SCREENSHOTS': '1',
             'KREMA_KEEP_FRAMES': '1',
             'KREMA_SCENARIO_DIR': str(scenario_dir)},
        check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--src', required=True, type=pathlib.Path)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    args = parser.parse_args()

    scenarios = sorted((args.src / 'tests/ci/scenarios').glob('*.json'))

    # An assertion claims causation unless it says otherwise.
    invariant: dict[str, set[str]] = {}
    for path in scenarios:
        data = json.loads(path.read_text())
        for spec in data.get('assertions', []):
            if spec.get('invariant'):
                invariant.setdefault(path.stem, set()).add(spec['qa'])

    # Two controls. Dropping the actions catches assertions that do not depend
    # on input. Dropping the config as well catches the ones that only read back
    # a value the runner itself wrote at startup -- an icon-size scenario whose
    # capture is 140 identical frames passes the first control and fails only
    # this one.
    controls = {}
    for label, drop_config in (('no-actions', False), ('defaults-only', True)):
        directory = args.out / f'control-{label}'
        shutil.rmtree(directory, ignore_errors=True)
        directory.mkdir(parents=True)
        for path in scenarios:
            data = dict(json.loads(path.read_text()))
            data['actions'] = []
            if drop_config:
                data.pop('config', None)
            (directory / path.name).write_text(
                json.dumps(data, indent=2, ensure_ascii=False))
        controls[label] = directory

    print('== baseline ==', flush=True)
    baseline = args.out / 'baseline'
    run_suite(args.src, baseline, args.src / 'tests/ci/scenarios')

    runs = {}
    for label, directory in controls.items():
        print(f'== control: {label} ==', flush=True)
        runs[label] = args.out / f'run-{label}'
        run_suite(args.src, runs[label], directory)

    vacuous: list[tuple[str, str]] = []
    proven = 0
    unmarked: list[tuple[str, str]] = []
    churn: list[tuple[str, int, float]] = []

    for path in scenarios:
        name = path.stem
        base_pass = passing_ids(baseline / name)
        churn.append((name, *frame_churn(baseline / name)))
        for qa in sorted(base_pass):
            if qa in invariant.get(name, set()):
                continue
            survives = [label for label, run in runs.items()
                        if qa in passing_ids(run / name)]
            if len(survives) == len(runs):
                vacuous.append((qa, f'{name} (survives {", ".join(survives)})'))
            elif survives:
                unmarked.append((qa, f'{name} (survives {survives[0]})'))
            else:
                proven += 1

    print()
    print(f'caused-change assertions proven by the control: {proven}')
    print(f'caused-change assertions that pass with NO stimulus (vacuous): {len(vacuous)}')
    for qa, name in vacuous:
        print(f'  {qa:16} {name}')
    if unmarked:
        print()
        print('partly vacuous: survives one control but not the other')
        for qa, name in unmarked:
            print(f'  {qa:16} {name}')

    flat = [(n, d) for n, d, _ in churn if d <= 1]
    print()
    print('captures that never change on screen:')
    for name, distinct in flat:
        print(f'  {name:28} {distinct} distinct frame(s)')
    if not flat:
        print('  none')

    print()
    print('least motion:')
    for name, distinct, pct in sorted(churn, key=lambda r: r[1])[:10]:
        print(f'  {name:28} {distinct:4} distinct  {pct:5.1f}% of frame changed')
    return 1 if vacuous else 0


if __name__ == '__main__':
    sys.exit(main())
