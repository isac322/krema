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
    steps = [(f, abs(b - a)) for (_, a), (f, b) in zip(window, window[1:])]
    worst = max(steps, key=lambda item: item[1], default=(None, 0.0))
    if span > tol and worst[1] > max_ratio * span:
        fail(f"{spec['item']}.{spec['prop']}: frame {worst[0]} jumps {worst[1]:.4f} of a "
             f"{span:.4f} range ({worst[1] / span:.0%} > {max_ratio:.0%}) — "
             f"the value snapped instead of animating")

    # Cheap sanity floor; deliberately low so it never decides pass/fail alone.
    intermediates = {round(v, 4) for _, v in window if lo + tol < v < hi - tol}
    needed = spec.get('min_intermediate_frames', 2)
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
    if menu is None:
        fail(f"no popup menu open at frame {frame}")
        return
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


# --- run-to-run divergence -------------------------------------------------

def signature(row: dict) -> tuple:
    out = []
    for window in row['windows']:
        for item in window['items']:
            out.append(tuple(item[k] for k in BASE_KEYS))
            props = item.get('props', {})
            out.append(tuple(sorted((k, v) for k, v in props.items())))
    return tuple(out)


def divergence(a: list[dict], b: list[dict]) -> tuple[int, list[str]]:
    """Compare two passes, allowing a small whole-frame shift.

    Animations start one frame apart between runs: whether the frame that
    applies an action also sees its first animation step depends on sub-frame
    timing. That is a phase difference, not a different result, so a run is
    reproducible if the second pass matches the first after shifting by a few
    frames. A mismatch that no shift can align is a real divergence.
    """
    sa = [signature(r) for r in a]
    sb = [signature(r) for r in b]
    best_shift, best_bad = 0, None
    for shift in (0, 1, -1, 2, -2, 3, -3):
        bad = 0
        for i, x in enumerate(sa):
            j = i + shift
            if 0 <= j < len(sb) and sb[j] != x:
                bad += 1
            elif not 0 <= j < len(sb):
                continue
        if best_bad is None or bad < best_bad:
            best_bad, best_shift = bad, shift

    # Report magnitude, not just "differs": a 0.0005 wobble on currentScale and
    # a 222 px panel offset are not the same finding.
    notes = []
    worst = (0.0, '')
    if best_bad:
        for i, row in enumerate(a):
            j = i + best_shift
            if not 0 <= j < len(b):
                continue
            for wa, wb in zip(row['windows'], b[j]['windows']):
                for ia, ib in zip(wa['items'], wb['items']):
                    for key in BASE_KEYS:
                        if isinstance(ia[key], (int, float)) and ia[key] != ib[key]:
                            delta = abs(ia[key] - ib[key])
                            if delta > worst[0]:
                                worst = (delta, f"{ia['path']}.{key} frame {row['frame']}: "
                                                f"{ia[key]} vs {ib[key]}")
                    pa, pb = ia.get('props', {}), ib.get('props', {})
                    for key in set(pa) & set(pb):
                        if isinstance(pa[key], (int, float)) and not isinstance(pa[key], bool) \
                                and pa[key] != pb[key]:
                            delta = abs(pa[key] - pb[key])
                            if delta > worst[0]:
                                worst = (delta, f"{ia['path']}.props.{key} frame {row['frame']}: "
                                                f"{pa[key]} vs {pb[key]}")
        notes.append(f'{best_bad}/{len(a)} frames differ; largest gap {worst[0]:.4g} at {worst[1]}')
    return best_shift, notes


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

    lines = [f"# 스크린샷 리뷰: {manifest['scenario']}", '',
             f'총 {total} 프레임. 각 항목의 기대 결과와 해당 프레임 이미지를 비교하라.', '']
    for entry in entries:
        lines.append(f"## {entry['qa']} — {entry['result']}")
        lines.append(f"- 검사 종류: `{entry['kind']}`")
        lines.append(f"- 기대: {entry['expectation']}")
        for shot in entry['frames']:
            lines.append(f'- ![{shot}]({shot})')
        for failure in entry['failures']:
            lines.append(f'- 실패: {failure}')
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

    passes = sorted(args.run_dir.glob('pass*/frames.ndjson'))
    if not passes:
        print(f'  FAIL no capture under {args.run_dir}')
        return 1
    rows = load(passes[0])

    results: dict[str, list[str]] = {}
    covered: list[str] = []
    for spec in scenario.get('assertions', []):
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

    if len(passes) > 1:
        shift, notes = divergence(rows, load(passes[1]))
        if notes:
            print(f'  REPRO differs beyond a {shift:+d} frame shift:')
            for note in notes:
                print(f'    {note}')
        elif shift:
            print(f'  repro: identical after a {shift:+d} frame shift (animation onset phase)')
        else:
            print('  repro: byte-identical')

    if args.review:
        write_review(scenario, rows, args.run_dir, results)
        print(f'  review manifest: {args.run_dir / "review.md"}')

    for qa in covered:
        if qa in results:
            print(f'  FAIL {qa}')
            for failure in results[qa]:
                print(f'    {failure}')
        else:
            print(f'  pass {qa}')

    if results:
        print(f'  {len(results)}/{len(covered)} QA item(s) failed over {len(rows)} frames')
        return 1
    print(f'  {len(covered)} QA item(s) passed over {len(rows)} frames')
    return 0


if __name__ == '__main__':
    sys.exit(main())
