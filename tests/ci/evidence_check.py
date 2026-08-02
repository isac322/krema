#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Require every assertion to point at something that visibly happened.

An assertion can survive the vacuity control -- it fails when the stimulus is
removed -- and still verify nothing a user could see: flipping a setting changes
a QML property while nothing renders, and the scenario looks alive because some
unrelated hover in the same capture supplies the motion. Scenario-wide pixel
churn cannot tell those apart.

So this checks each assertion on its own terms: crop the pixels of the item the
assertion names, at the frame it asserts, and compare them with the same crop
just before the stimulus that is supposed to have caused the change. No
difference means the assertion is not backed by anything on screen.

The crop comes from the item's scene rect as the probe recorded it, with every
transform applied, plus the window's draw offset on the composited frame.
Assertions marked `"invariant": true` are exempt: they assert that something
does not change. An assertion that cannot be pixel-checked at all must say so
with `"pixel_exempt": "<reason>"`; anything else that cannot be checked is
reported as unbacked rather than quietly waived.

Usage, inside the test image, after a run with KREMA_SCREENSHOTS=1:
  python3 tests/ci/evidence_check.py --src /src --run-dir /out
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

PAD = 6  # zoom and shadows spill past the item's own rect


def load_rows(run_dir: pathlib.Path) -> list[dict]:
    stream = run_dir / 'pass1' / 'frames.ndjson'
    if not stream.exists():
        return []
    return [json.loads(line) for line in stream.open() if line.strip()]


def item_box(row: dict, ident: str) -> tuple[int, int, int, int] | None:
    for window in row['windows']:
        for item in window['items']:
            if item.get('name') == ident or item['path'] == ident:
                if 'sx' not in item:
                    return None
                ox, oy = window.get('ox', 0), window.get('oy', 0)
                return (int(ox + item['sx']) - PAD, int(oy + item['sy']) - PAD,
                        int(ox + item['sx'] + item['sw']) + PAD,
                        int(oy + item['sy'] + item['sh']) + PAD)
    return None


def window_box(row: dict, key: str) -> tuple[int, int, int, int] | None:
    for window in row['windows']:
        if window['key'] == key:
            ox, oy = window.get('ox', 0), window.get('oy', 0)
            return (ox, oy, ox + window['w'], oy + window['h'])
    return None


def union(a, b):
    """Cover the item in both frames.

    A dock that slides away is in a different place before and after, and
    cropping only its final rect can land on background in both frames and
    report "unchanged" for a change that filled the screen.
    """
    if a is None:
        return b
    if b is None:
        return a
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


def stimulus_before(actions: list[dict], frame: int) -> int:
    earlier = [a['frame'] for a in actions if a['frame'] < frame]
    return max(earlier) if earlier else 0


def assertion_frames(spec: dict, total: int) -> tuple[int, int] | None:
    """The frame the assertion is about, and the frame to compare it against."""
    if spec['kind'] == 'animates':
        return spec['settled_by'], spec['start_after']
    frame = spec.get('frame', total)
    return frame, None


def crop_differs(frames_dir: pathlib.Path, box, before: int, after: int) -> bool | None:
    """True if the crop changed, False if not, None if it could not be compared."""
    from PIL import Image, ImageChops
    a = frames_dir / f'f{before:05d}.png'
    b = frames_dir / f'f{after:05d}.png'
    if not a.exists() or not b.exists():
        return None
    ia, ib = Image.open(a).convert('RGB'), Image.open(b).convert('RGB')
    clipped = (max(0, box[0]), max(0, box[1]),
               min(ia.width, box[2]), min(ia.height, box[3]))
    if clipped[2] <= clipped[0] or clipped[3] <= clipped[1]:
        return None
    return ImageChops.difference(ia.crop(clipped), ib.crop(clipped)).getbbox() is not None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--src', required=True, type=pathlib.Path)
    parser.add_argument('--run-dir', required=True, type=pathlib.Path)
    args = parser.parse_args()

    unbacked: list[str] = []
    backed = 0
    exempt = 0
    skipped: list[str] = []
    examined = 0
    missing_frames: list[str] = []
    baseline: dict[tuple[str, str], int] = {}
    control_failures: list[str] = []
    controls_run = 0

    for path in sorted((args.src / 'tests/ci/scenarios').glob('*.json')):
        scenario = json.loads(path.read_text())
        run = args.run_dir / path.stem
        rows = load_rows(run)
        frames_dir = run / 'pass1' / 'frames'
        if not rows or not frames_dir.is_dir():
            # A gate that quietly examines nothing is worse than no gate: run
            # the suite with KREMA_SCREENSHOTS=1 or this proves nothing.
            missing_frames.append(path.stem)
            continue
        examined += 1
        by_frame = {r['frame']: r for r in rows}
        total = max(by_frame)
        actions = scenario.get('actions', [])

        first_action = min((a['frame'] for a in actions), default=None)
        # Two settled frames before anything is injected. Startup animation is
        # done by frame 3 in this harness, so a pair at or after that is quiet.
        quiet = None
        if first_action is not None and first_action >= 6:
            candidate = (first_action - 3, first_action - 1)
            if all(f in by_frame for f in candidate):
                quiet = candidate

        for spec in scenario.get('assertions', []):
            qa, kind = spec.get('qa', '?'), spec['kind']
            if spec.get('invariant'):
                exempt += 1
                continue
            if spec.get('pixel_exempt'):
                skipped.append(f"{qa} {path.stem} ({kind}): {spec['pixel_exempt']}")
                continue

            target = assertion_frames(spec, total)
            if not target:
                unbacked.append(f'{qa:16} {path.stem:26} no frame to check')
                continue
            after, explicit_before = target

            # An assertion placed before the first action is the baseline half
            # of a pair: it records the state the stimulus then changes. It
            # cannot show a delta of its own, and requiring one would push
            # authors to delete exactly the assertion that makes the pair
            # meaningful. Its partner still has to show one.
            if first_action is not None and after < first_action:
                baseline.setdefault((path.stem, qa), 0)
                baseline[(path.stem, qa)] += 1
                continue

            before = explicit_before if explicit_before is not None \
                else stimulus_before(actions, after)
            if before <= 0 or before not in by_frame:
                unbacked.append(f'{qa:16} {path.stem:26} no pre-stimulus frame for f{after}')
                continue

            row_after, row_before = by_frame.get(after), by_frame[before]
            if not row_after:
                unbacked.append(f'{qa:16} {path.stem:26} frame {after} not captured')
                continue

            if kind == 'window':
                idents = [spec['key']]
                boxes = [union(window_box(row_before, spec['key']),
                               window_box(row_after, spec['key']))]
            else:
                idents = spec.get('items') or ([spec['item']] if spec.get('item')
                                               else [spec.get('container', '0/0/2')])
                boxes = [union(item_box(row_before, i), item_box(row_after, i))
                         for i in idents]

            verdicts = []
            for ident, box in zip(idents, boxes):
                if box is None:
                    unbacked.append(f'{qa:16} {path.stem:26} {ident} has no recorded rect')
                    verdicts.append(False)
                    continue
                result = crop_differs(frames_dir, box, before, after)
                if result is None:
                    unbacked.append(
                        f'{qa:16} {path.stem:26} {ident} rect falls outside the frame; '
                        f'cannot compare f{before} with f{after}')
                    verdicts.append(False)
                else:
                    verdicts.append(result)
            if any(verdicts):
                backed += 1
                # The gate widens each crop to cover the item in both frames.
                # A union wide enough to catch unrelated motion would report a
                # change for any pair, so every positive is re-checked over a
                # quiet stretch: the two frames just before the first action,
                # where the scene has settled and nothing has been injected. A
                # crop that also "changes" there is measuring noise and its
                # positive is withdrawn.
                if quiet is not None:
                    for ident, box in zip(idents, boxes):
                        controls_run += 1
                        if box is not None and \
                                crop_differs(frames_dir, box, quiet[0], quiet[1]):
                            control_failures.append(
                                f'{qa:16} {path.stem:26} {ident} also changes over the '
                                f'quiet pair f{quiet[0]}-f{quiet[1]}')
            elif verdicts:
                unbacked.append(
                    f'{qa:16} {path.stem:26} {", ".join(idents)} unchanged on screen '
                    f'between f{before} and f{after}')

    print(f'assertions backed by a visible change: {backed}')
    print(f'assertions exempt as declared invariants: {exempt}')
    print(f'baseline halves recorded before the first action: {sum(baseline.values())}')
    if skipped:
        print(f'assertions not pixel-checkable ({len(skipped)}), vacuity control only:')
        for line in skipped:
            print(f'  {line}')
    print(f'assertions with no visible change: {len(unbacked)}')
    for line in unbacked:
        print(f'  {line}')

    if missing_frames:
        print(f'scenarios with no captured frames ({len(missing_frames)}): '
              f'{", ".join(missing_frames)}')
        print('run the suite with KREMA_SCREENSHOTS=1 before checking evidence')
    # A control that never ran proves nothing about the crops it was meant to
    # police, so report its coverage rather than letting silence read as a pass.
    print(f'positives re-checked over a quiet pre-stimulus pair: {controls_run}')
    if backed and not controls_run:
        print('the crop control never ran; it cannot vouch for any positive')
        return 1
    if control_failures:
        print(f'crops that change against themselves ({len(control_failures)}): '
              'the comparison is measuring noise, not the assertion')
        for line in control_failures:
            print(f'  {line}')
        return 1
    if not examined:
        print('no scenario was examined; refusing to report a pass')
        return 1
    print(f'scenarios examined: {examined}')

    # A gate that only reports "0 unbacked" degrades quietly: stop emitting
    # frames, or move an assertion into pixel_exempt, and it stays green while
    # checking less. The budget is committed, so any erosion has to appear in
    # the diff with its reason.
    budget_path = args.src / 'tests/ci/evidence_baseline.json'
    failed = bool(unbacked or missing_frames)
    if budget_path.exists():
        budget = json.loads(budget_path.read_text())
        for label, actual, limit, worse in (
                ('assertions backed', backed, budget['backed_min'], 'fewer'),
                ('declared invariants', exempt, budget['invariant_max'], 'more'),
                ('pixel-exempt assertions', len(skipped),
                 budget['pixel_exempt_max'], 'more')):
            over = actual < limit if worse == 'fewer' else actual > limit
            if over:
                print(f'{label}: {actual}, budget says no {worse} than {limit} '
                      f'-- update {budget_path.name} with the reason if intended')
                failed = True
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
