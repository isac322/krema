#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Produce the animations embedded in a pull request, and the comment body.

GitHub will not play a video inline: raw `.mp4` and release assets both come
back as `application/octet-stream` with `content-disposition: attachment`, so
`<video src=...>` never renders. Raw image URLs do return a real `image/*`
content type, so an animated GIF referenced with `![]()` is the only thing that
shows up without a download.

Every captured frame goes into the GIF — none are dropped. Decimating to a
lower frame rate would hide exactly what the suite exists to catch: a 150 ms
animation is nine frames, and throwing five of them away leaves a blur that
cannot be told apart from a snap.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys

# GitHub rejects a comment body over this; degrade rather than fail the step.
BODY_LIMIT = 60000

AREA_NAMES = {
    'VIS': 'Dock display and placement',
    'ITEM': 'Dock items and tasks',
    'PRE': 'Window preview',
    'CMD': 'Context menu and shortcuts',
    'SET': 'Settings',
    'NOTI': 'Notifications and attention',
    'KEY': 'Keyboard and accessibility',
}


def encode_gif(src: pathlib.Path, dst: pathlib.Path, width: int, height: int,
               colors: int) -> bool:
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        return False
    # Fit inside a box rather than forcing a width: a vertical dock is a 108 px
    # wide window, and upscaling that to 620 turned a 32 KiB animation into
    # 1.6 MB of interpolated noise.
    chain = (f'scale=w={width}:h={height}:force_original_aspect_ratio=decrease:'
             f'flags=lanczos,split[a][b];'
             f'[a]palettegen=max_colors={colors}[p];'
             '[b][p]paletteuse=dither=bayer:bayer_scale=3')
    result = subprocess.run(
        [ffmpeg, '-y', '-loglevel', 'error', '-i', str(src), '-vf', chain,
         '-loop', '0', str(dst)],
        capture_output=True, text=True)
    if result.returncode:
        print(f'  gif failed for {src.name}: {result.stderr.strip()[:160]}', file=sys.stderr)
        return False
    return True


def result_failed(result: dict) -> bool:
    return (bool(result.get('scenario_failures'))
            or result.get('repro_ok') is False
            or any(item.get('failures') for item in result.get('qa', [])))


def area_of(result: dict) -> str:
    rows = [*result.get('qa', []), *result.get('not_verified', [])]
    for item in rows:
        parts = item['id'].split('-')
        if len(parts) > 1 and parts[1] in AREA_NAMES:
            return parts[1]
    return 'ITEM'


def render(results: list[dict], gifs: dict[str, str], base_url: str,
           failures_only: bool) -> str:
    qa_groups = [(result, item) for result in results for item in result.get('qa', [])]
    bad_groups = sum(1 for _, item in qa_groups if item.get('failures'))
    assertions = sum(result.get('assertions', len(result.get('qa', [])))
                     for result in results)
    unique_qa = {item['id'] for _, item in qa_groups}
    not_verified_count = sum(len(result.get('not_verified', [])) for result in results)
    failed = [result for result in results if result_failed(result)]
    not_verified = [result for result in results
                    if not result_failed(result) and result.get('not_verified')]

    lines = ['<!-- krema-frame-tests -->', '## UI frame tests', '',
             f"{'❌' if failed else '✅'} "
             f"**{len(qa_groups) - bad_groups}/{len(qa_groups)}** QA result groups "
             f'passed from {assertions} assertions ({len(unique_qa)} unique QA IDs) '
             f'across {len(results)} scenarios; {not_verified_count} QA rows are not '
             f'verified.', '']

    def block(result: dict) -> list[str]:
        failing = [(item['id'], failure) for item in result.get('qa', [])
                   for failure in item.get('failures', [])]
        harness = [*result.get('scenario_failures', []),
                   *result.get('repro_failures', [])]
        neutral = result.get('not_verified', [])
        if failing or harness:
            icon = '❌'
        elif neutral and not result.get('qa'):
            icon = '⚪'
        else:
            icon = '✅'
        out = [f"**{icon} {result['scenario']}** — {result['description']}", '']
        for qa, failure in failing:
            out.append(f'- `{qa}` — {failure}')
        for failure in harness:
            out.append(f'- Harness — {failure}')
        for item in neutral:
            out.append(f"- `{item['id']}` not verified — {item['reason']}")
        if failing or harness or neutral:
            out.append('')
        name = gifs.get(result['scenario'])
        out.append(f"![{result['scenario']}]({base_url}/{name})" if name and base_url
                   else '_(no animation published)_')
        out.append('')
        return out

    if failed:
        lines += ['### Failing', '']
        for result in failed:
            lines += block(result)

    if not_verified:
        lines += ['### Not verified', '']
        for result in not_verified:
            lines += block(result)

    if not failures_only:
        by_area: dict[str, list[dict]] = {}
        for result in results:
            if result in failed or result in not_verified:
                continue
            by_area.setdefault(area_of(result), []).append(result)
        for area, group in sorted(by_area.items()):
            lines.append(f'<details><summary>{AREA_NAMES.get(area, area)} '
                         f'({len(group)})</summary>')
            lines.append('')
            for result in group:
                lines += block(result)
            lines.append('</details>')
            lines.append('')
    else:
        lines.append('_Passing scenarios omitted: the full gallery exceeded the '
                     'comment size limit. Their MP4s are in the artifact._')
        lines.append('')

    lines.append('<sub>Every frame is stamped with its number and virtual time, so a '
                 'failing frame number above can be found in the animation. MP4s for '
                 'every scenario, the capture streams and the review keyframes are in '
                 'the <code>frame-captures</code> artifact.</sub>')
    return '\n'.join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-dir', required=True, type=pathlib.Path)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    parser.add_argument('--base-url', default='')
    parser.add_argument('--width', type=int, default=620)
    parser.add_argument('--height', type=int, default=380)
    parser.add_argument('--colors', type=int, default=48)
    args = parser.parse_args()

    results = [json.loads(p.read_text()) for p in sorted(args.run_dir.glob('*/result.json'))]
    if not results:
        print('No scenario results found.')
        return 0

    args.out.mkdir(parents=True, exist_ok=True)
    gifs: dict[str, str] = {}
    for result in results:
        name = result['scenario']
        mp4 = args.run_dir / name / f'{name}.mp4'
        if not mp4.exists():
            continue
        gif = args.out / f'{name}.gif'
        if encode_gif(mp4, gif, args.width, args.height, args.colors):
            gifs[name] = gif.name

    body = render(results, gifs, args.base_url, failures_only=False)
    if len(body) > BODY_LIMIT:
        body = render(results, gifs, args.base_url, failures_only=True)
    (args.out / 'comment.md').write_text(body)

    total = sum(p.stat().st_size for p in args.out.glob('*.gif'))
    print(f'  {len(gifs)} preview animation(s), {total // 1024} KiB, '
          f'comment {len(body)} chars')
    return 0


if __name__ == '__main__':
    sys.exit(main())
