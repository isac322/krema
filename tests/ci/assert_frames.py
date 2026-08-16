#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Assert a frame-probe capture against a scenario, and optionally emit a
screenshot review manifest.

The capture is NDJSON, one row per rendered frame, produced by
tests/ci/frameprobe. Animation time is virtual and advances a fixed step per
frame, so assertions are expressed in frame numbers: a 150 ms animation becomes
~9 inspectable samples rather than a race.

Every assertion carries a `qa` field naming the QA checklist item it covers
(tests/ci/qa-checklist.md), and results are reported per QA id.

Assertion kinds
---------------
animates   a property leaves `from`, reaches `to`, and no single frame covers
           more than `max_step_ratio` of the range (the snap detector)
settles    a numeric property equals `value` at `frame`
equals     a string/bool property equals `value` at `frame`
ordered    several items' values are strictly ascending/descending at `frame`
stable     a property is present in every frame and never changes
absent     no item with this name/path exists at `frame`
count      the number of items matching `type` (and optional `name_prefix`)
menu       the active popup menu's entries at `frame`
window     a window with this key exists, and its visibility/size
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

BASE_KEYS = ('x', 'y', 'w', 'h', 'scale', 'opacity', 'rotation', 'visible')


def load(path: pathlib.Path) -> list[dict]:
    with path.open() as handle:
        return [json.loads(line) for line in handle if line.strip()]


def find_item(row: dict, ident: str) -> dict | None:
    """Resolve an item by objectName first, then by child-index path.

    objectName is the stable handle; a path survives only until the QML tree is
    restructured, at which point it would silently point at a different item.
    """
    for window in row['windows']:
        for item in window['items']:
            if item.get('name') == ident:
                return item
    for window in row['windows']:
        for item in window['items']:
            if item['path'] == ident:
                return item
    return None


def value_of(row: dict, ident: str, prop: str):
    item = find_item(row, ident)
    if item is None:
        return None
    if prop in item:
        return item[prop]
    return item.get('props', {}).get(prop)


def series(rows: list[dict], ident: str, prop: str) -> list[tuple[int, object]]:
    out = []
    for row in rows:
        value = value_of(row, ident, prop)
        if value is not None:
            out.append((row['frame'], value))
    return out


def row_at(rows: list[dict], frame: int) -> dict:
    for row in rows:
        if row['frame'] == frame:
            return row
    raise IndexError(f'frame {frame} not captured (run has {len(rows)} frames)')


# --- assertion kinds -------------------------------------------------------

def check_animates(rows, spec, fail):
    data = series(rows, spec['item'], spec['prop'])
    if not data:
        fail(f"{spec['item']}.{spec['prop']}: not present in capture")
        return
    tol = spec.get('tolerance', 0.01)
    window = [(f, v) for f, v in data if spec['start_after'] <= f <= spec['settled_by']]
    if not window:
        fail(f"{spec['item']}.{spec['prop']}: no frames in "
             f"[{spec['start_after']},{spec['settled_by']}]")
        return

    begin = window[0][1]
    if abs(begin - spec['from']) > tol:
        fail(f"{spec['item']}.{spec['prop']}: starts at {begin:.4f}, expected {spec['from']}")
    end = window[-1][1]
    if abs(end - spec['to']) > tol:
        fail(f"{spec['item']}.{spec['prop']}: at frame {spec['settled_by']} is "
             f"{end:.4f}, expected {spec['to']}")

    lo, hi = sorted((spec['from'], spec['to']))
    span = hi - lo

    # Primary predicate: no single frame may cover more than max_step_ratio of
    # the range. A one-frame snap covers ~100%; an eased curve covers ~29%, or
    # ~51% when the +/-1 onset phase shift costs a sample. Unlike counting
    # intermediate samples this does not flake, and it needs no retuning when
    # stepMs or the animation duration changes.
    max_ratio = spec.get('max_step_ratio', 0.75)
    # The first delta after an animation starts is not reproducible: measured
    # 39-100% of range across runs of the same scenario, because QUnifiedTimer
    # registers a newly started animation through a deferred 0 ms timer and the
    # registration lands on either side of the next clock advance. Judging
    # "snapped or eased" on that frame is a coin flip, so skip the onset and
    # judge the body of the animation, which is stable. A true one-frame snap
    # still fails, via the intermediate-value floor below.
    skip = spec.get('skip_onset_frames', 2)
    body = window[skip:] if len(window) > skip + 1 else window
    steps = [(f, abs(b - a)) for (_, a), (f, b) in zip(body, body[1:])]
    worst = max(steps, key=lambda item: item[1], default=(None, 0.0))
    if span > tol and worst[1] > max_ratio * span:
        fail(f"{spec['item']}.{spec['prop']}: frame {worst[0]} jumps {worst[1]:.4f} of a "
             f"{span:.4f} range ({worst[1] / span:.0%} > {max_ratio:.0%}) — "
             f"the value snapped instead of animating")

    # Cheap sanity floor; deliberately low so it never decides pass/fail alone.
    intermediates = {round(v, 4) for _, v in window if lo + tol < v < hi - tol}
    needed = 0 if spec.get('expect_snap') else spec.get('min_intermediate_frames', 2)
    if len(intermediates) < needed:
        fail(f"{spec['item']}.{spec['prop']}: only {len(intermediates)} intermediate "
             f"value(s) between {spec['from']} and {spec['to']}, expected >= {needed}")


def check_settles(rows, spec, fail):
    frame = spec.get('frame', len(rows))
    value = value_of(row_at(rows, frame), spec['item'], spec['prop'])
    if value is None:
        fail(f"{spec['item']}.{spec['prop']}: not present at frame {frame}")
        return
    if abs(value - spec['value']) > spec.get('tolerance', 0.01):
        fail(f"{spec['item']}.{spec['prop']} at frame {frame}: {value:.4f}, "
             f"expected {spec['value']}")


def check_equals(rows, spec, fail):
    frame = spec.get('frame', 1)
    value = value_of(row_at(rows, frame), spec['item'], spec['prop'])
    if value is None:
        fail(f"{spec['item']}.{spec['prop']}: not present at frame {frame} "
             f"(wrong objectName, or the item never appeared)")
        return
    if value != spec['value']:
        fail(f"{spec['item']}.{spec['prop']} at frame {frame}: {value!r}, "
             f"expected {spec['value']!r}")


def check_ordered(rows, spec, fail):
    frame = spec.get('frame', 1)
    row = row_at(rows, frame)
    values = [value_of(row, item, spec['prop']) for item in spec['items']]
    if any(v is None for v in values):
        missing = [i for i, v in zip(spec['items'], values) if v is None]
        fail(f"ordered check at frame {frame}: missing {missing}")
        return
    pairs = list(zip(values, values[1:]))
    ok = all(a > b for a, b in pairs) if spec['order'] == 'descending' \
        else all(a < b for a, b in pairs)
    if not ok:
        fail(f"ordered check at frame {frame}: {spec['prop']} = "
             f"{[round(v, 4) for v in values]} is not {spec['order']}")


def check_stable(rows, spec, fail):
    data = series(rows, spec['item'], spec['prop'])
    if len(data) != len(rows):
        fail(f"{spec['item']}.{spec['prop']}: present in {len(data)}/{len(rows)} frames, "
             f"expected all (wrong objectName, or the item never appeared)")
        return
    values = {round(v, 4) if isinstance(v, float) else v for _, v in data}
    if len(values) > 1:
        fail(f"{spec['item']}.{spec['prop']}: expected stable, saw {sorted(values)[:6]}")


def check_absent(rows, spec, fail):
    frame = spec.get('frame', 1)
    if find_item(row_at(rows, frame), spec['item']) is not None:
        fail(f"{spec['item']}: expected absent at frame {frame}, but it exists")


def check_count(rows, spec, fail):
    frame = spec.get('frame', 1)
    row = row_at(rows, frame)
    prefix = spec.get('name_prefix')
    found = 0
    for window in row['windows']:
        for item in window['items']:
            if spec.get('type') and item['type'] != spec['type']:
                continue
            if prefix and not item.get('name', '').startswith(prefix):
                continue
            if spec.get('visible_only') and not item['visible']:
                continue
            found += 1
    if found != spec['value']:
        what = spec.get('type') or f'name~{prefix}'
        fail(f"count of {what} at frame {frame}: {found}, expected {spec['value']}")


def check_menu(rows, spec, fail):
    frame = spec.get('frame', 1)
    menu = row_at(rows, frame).get('menu')
    if spec.get('exists') is False:
        if menu is not None:
            fail(f"popup menu exists at frame {frame}, expected none")
        return
    if menu is None:
        fail(f"no popup menu open at frame {frame}")
        return
    for key in ('visible', 'mapped'):
        if key in spec and menu.get(key) != spec[key]:
            fail(f"menu {key} at frame {frame}: {menu.get(key)}, expected {spec[key]}")
    entries = menu['entries']
    labels = ['---' if e.get('separator') else e['text'] for e in entries]
    if 'entries' in spec and labels != spec['entries']:
        fail(f"menu entries at frame {frame}:\n      got      {labels}\n"
             f"      expected {spec['entries']}")
    for want in spec.get('contains', []):
        if want not in labels:
            fail(f"menu at frame {frame} is missing {want!r}; has {labels}")
    for reject in spec.get('excludes', []):
        if reject in labels:
            fail(f"menu at frame {frame} should not offer {reject!r}")
    for label, expected in spec.get('enabled', {}).items():
        match = next((e for e in entries if e.get('text') == label), None)
        if match is None:
            fail(f"menu at frame {frame} has no entry {label!r}")
        elif match['enabled'] != expected:
            fail(f"menu entry {label!r} enabled={match['enabled']}, expected {expected}")


def check_window(rows, spec, fail):
    frame = spec.get('frame', 1)
    row = row_at(rows, frame)
    match = next((w for w in row['windows'] if w['key'] == spec['key']), None)
    if spec.get('exists') is False:
        if match is not None:
            fail(f"window {spec['key']!r} exists at frame {frame}, expected none")
        return
    if match is None:
        keys = [w['key'] for w in row['windows']]
        fail(f"no window {spec['key']!r} at frame {frame}; have {keys}")
        return
    for key in ('visible', 'w', 'h'):
        if key in spec and match[key] != spec[key]:
            fail(f"window {spec['key']!r} {key} at frame {frame}: {match[key]}, "
                 f"expected {spec[key]}")


CHECKS = {
    'animates': check_animates,
    'settles': check_settles,
    'equals': check_equals,
    'ordered': check_ordered,
    'stable': check_stable,
    'absent': check_absent,
    'count': check_count,
    'menu': check_menu,
    'window': check_window,
}


# --- run-to-run reproducibility --------------------------------------------

def canonical_payload(row: dict) -> dict:
    """Captured state whose equality is required across passes.

    `frame` is checked separately and `vt` is derived from it. Everything else
    is observable capture output and must be byte-for-byte reproducible.
    """
    return {key: value for key, value in row.items() if key not in ('frame', 'vt')}


def first_difference(left, right, path: str = 'capture') -> str | None:
    """Describe the first structural difference between two JSON values."""
    if type(left) is not type(right):
        return f'{path}: type {type(left).__name__} != {type(right).__name__}'
    if isinstance(left, dict):
        left_keys, right_keys = set(left), set(right)
        if left_keys != right_keys:
            missing = sorted(left_keys - right_keys)
            extra = sorted(right_keys - left_keys)
            return f'{path}: missing keys {missing}, extra keys {extra}'
        for key in sorted(left):
            difference = first_difference(left[key], right[key], f'{path}.{key}')
            if difference:
                return difference
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return f'{path}: length {len(left)} != {len(right)}'
        for index, (left_item, right_item) in enumerate(zip(left, right)):
            difference = first_difference(left_item, right_item, f'{path}[{index}]')
            if difference:
                return difference
        return None
    if left != right:
        return f'{path}: {left!r} != {right!r}'
    return None


def reproducibility_failures(reference: list[dict], candidate: list[dict],
                             label: str = 'pass2') -> list[str]:
    """Return strict capture differences; no frame shifting or omission."""
    failures = []
    if len(reference) != len(candidate):
        failures.append(f'{label}: frame count {len(candidate)}, expected {len(reference)}')

    for index, (left, right) in enumerate(zip(reference, candidate)):
        left_frame = left.get('frame')
        right_frame = right.get('frame')
        if left_frame != right_frame:
            failures.append(
                f'{label}: row {index + 1} has frame {right_frame}, expected {left_frame}')
        difference = first_difference(canonical_payload(left), canonical_payload(right))
        if difference:
            failures.append(f'{label} frame {left_frame}: {difference}')
        if len(failures) >= 8:
            failures.append(f'{label}: additional differences omitted')
            break
    return failures


# --- screenshot review manifest --------------------------------------------

def keyframes(spec: dict, total: int) -> list[int]:
    """Frames worth showing a human (or a vision model) for this assertion."""
    if spec['kind'] == 'animates':
        start, end = spec['start_after'], spec['settled_by']
        mid = (start + end) // 2
        return sorted({max(1, start - 1), start, mid, min(total, end)})
    frame = spec.get('frame')
    if frame:
        return sorted({max(1, frame - 1), frame})
    return [total]


def write_review(scenario: dict, rows: list[dict], run_dir: pathlib.Path,
                 results: dict[str, list[str]]) -> None:
    """Emit a manifest pairing rendered frames with the expectation in prose.

    Numeric assertions decide pass/fail. This exists for the part that is
    genuinely visual — whether the glow reads as a glow — and is the input a
    vision model reviews.
    """
    frames_dir = run_dir / 'pass1' / 'frames'
    total = len(rows)
    entries = []
    for spec in scenario.get('assertions', []):
        qa = spec.get('qa', '(no QA id)')
        shots = [f'pass1/frames/f{f:05d}.png' for f in keyframes(spec, total)
                 if (frames_dir / f'f{f:05d}.png').exists()]
        entries.append({
            'qa': qa,
            'kind': spec['kind'],
            'expectation': spec.get('comment') or scenario.get('description', ''),
            'frames': shots,
            'result': 'FAIL' if results.get(qa) else 'PASS',
            'failures': results.get(qa, []),
        })
    manifest = {
        'scenario': scenario.get('description', ''),
        'total_frames': total,
        'entries': entries,
    }
    (run_dir / 'review.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False))

    lines = [f"# Screenshot review: {manifest['scenario']}", '',
             f'{total} frames. Compare each expectation below against its frames.', '']
    for entry in entries:
        lines.append(f"## {entry['qa']} — {entry['result']}")
        lines.append(f"- Check: `{entry['kind']}`")
        lines.append(f"- Expected: {entry['expectation']}")
        for shot in entry['frames']:
            lines.append(f'- ![{shot}]({shot})')
        for failure in entry['failures']:
            lines.append(f'- Failure: {failure}')
        lines.append('')
    (run_dir / 'review.md').write_text('\n'.join(lines))


# --- main ------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--scenario', required=True, type=pathlib.Path)
    parser.add_argument('--run-dir', required=True, type=pathlib.Path)
    parser.add_argument('--review', action='store_true',
                        help='emit review.json/review.md for screenshot review')
    args = parser.parse_args()

    scenario = json.loads(args.scenario.read_text())
    if isinstance(scenario, list):
        scenario = {'actions': scenario, 'assertions': []}

    assertions = scenario.get('assertions', [])
    not_verified = scenario.get('not_verified', [])
    scenario_failures: list[str] = []
    for obsolete in ('blocked', 'not_verifiable'):
        if obsolete in scenario:
            scenario_failures.append(
                f"obsolete scenario metadata {obsolete!r}; use 'not_verified'")
    if not isinstance(assertions, list):
        scenario_failures.append("'assertions' must be a list")
        assertions = []
    if not isinstance(not_verified, list):
        scenario_failures.append("'not_verified' must be a list")
        not_verified = []
    for index, entry in enumerate(not_verified):
        if not isinstance(entry, dict) or not entry.get('qa') or not entry.get('reason'):
            scenario_failures.append(
                f'not_verified[{index}] must contain non-empty qa and reason')
    if not assertions and not not_verified:
        scenario_failures.append(
            "scenario has no assertions and declares no 'not_verified' QA rows")

    passes = sorted(args.run_dir.glob('pass*/frames.ndjson'))
    if not passes:
        print(f'  FAIL no capture under {args.run_dir}')
        return 1
    rows = load(passes[0])

    results: dict[str, list[str]] = {}
    covered: list[str] = []
    for spec in assertions:
        qa = spec.get('qa', '(no QA id)')
        if qa not in covered:
            covered.append(qa)
        check = CHECKS.get(spec['kind'])
        if check is None:
            results.setdefault(qa, []).append(f"unknown assertion kind {spec['kind']!r}")
            continue
        try:
            check(rows, spec, lambda msg, qa=qa: results.setdefault(qa, []).append(msg))
        except (IndexError, KeyError, TypeError) as exc:
            results.setdefault(qa, []).append(f'{type(exc).__name__}: {exc}')

    repro_failures: list[str] = []
    repro_ok: bool | None = None
    repro = 'single pass'
    if len(passes) > 1:
        for pass_index, path in enumerate(passes[1:], start=2):
            repro_failures.extend(
                reproducibility_failures(rows, load(path), f'pass{pass_index}'))
        repro_ok = not repro_failures
        repro = 'byte-identical' if repro_ok else repro_failures[0]
        if repro_ok:
            print(f'  repro: byte-identical across {len(passes)} passes')
        else:
            print('  REPRO capture differs:')
            for failure in repro_failures:
                print(f'    {failure}')

    if args.review:
        write_review(scenario, rows, args.run_dir, results)
        print(f'  review manifest: {args.run_dir / "review.md"}')

    detail = {}
    for spec in assertions:
        qa = spec.get('qa', '(no QA id)')
        detail.setdefault(qa, spec.get('comment', ''))
    (args.run_dir / 'result.json').write_text(json.dumps({
        'scenario': args.run_dir.name,
        'description': scenario.get('description', ''),
        'frames': len(rows),
        'assertions': len(assertions),
        'repro': repro,
        'repro_ok': repro_ok,
        'repro_failures': repro_failures,
        'scenario_failures': scenario_failures,
        'not_verified': [
            {'id': entry['qa'], 'reason': entry['reason']}
            for entry in not_verified
            if isinstance(entry, dict) and entry.get('qa') and entry.get('reason')
        ],
        'qa': [{'id': qa, 'expectation': detail.get(qa, ''),
                'failures': results.get(qa, [])} for qa in covered],
    }, indent=2, ensure_ascii=False))

    for failure in scenario_failures:
        print(f'  FAIL scenario: {failure}')
    for qa in covered:
        if qa in results:
            print(f'  FAIL {qa}')
            for failure in results[qa]:
                print(f'    {failure}')
        else:
            print(f'  pass {qa}')
    for entry in not_verified:
        if isinstance(entry, dict) and entry.get('qa') and entry.get('reason'):
            print(f"  not verified {entry['qa']}: {entry['reason']}")

    failed = bool(results or repro_failures or scenario_failures)
    if failed:
        print(f'  scenario failed over {len(rows)} frames')
        return 1
    print(f'  {len(covered)} QA result group(s) passed from '
          f'{len(assertions)} assertion(s) over {len(rows)} frames; '
          f'{len(not_verified)} not verified')
    return 0


if __name__ == '__main__':
    sys.exit(main())
