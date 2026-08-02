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
    'VIS': '독 표시·배치',
    'ITEM': '독 항목·작업',
    'PRE': '창 미리보기',
    'CMD': '메뉴·단축키',
    'SET': '설정',
    'NOTI': '알림·주의 표시',
    'KEY': '키보드·접근성',
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


def area_of(result: dict) -> str:
    for item in result['qa']:
        parts = item['id'].split('-')
        if len(parts) > 1 and parts[1] in AREA_NAMES:
            return parts[1]
    return 'ITEM'


def render(results: list[dict], gifs: dict[str, str], base_url: str,
           failures_only: bool) -> str:
    total = sum(len(r['qa']) for r in results)
    bad = sum(1 for r in results for i in r['qa'] if i['failures'])

    lines = ['<!-- krema-frame-tests -->', '## UI frame tests', '',
             f"{'❌' if bad else '✅'} **{total - bad}/{total}** QA items passed "
             f'across {len(results)} scenarios.', '']

    def block(result: dict) -> list[str]:
        failing = [(i['id'], f) for i in result['qa'] for f in i['failures']]
        out = [f"**{'❌' if failing else '✅'} {result['scenario']}** — "
               f"{result['description']}", '']
        for qa, failure in failing:
            out.append(f'- `{qa}` — {failure}')
        if failing:
            out.append('')
        name = gifs.get(result['scenario'])
        out.append(f"![{result['scenario']}]({base_url}/{name})" if name and base_url
                   else '_(no animation published)_')
        out.append('')
        return out

    failed = [r for r in results if any(i['failures'] for i in r['qa'])]
    if failed:
        lines.append('### Failing')
        lines.append('')
        for result in failed:
            lines += block(result)

    if not failures_only:
        by_area: dict[str, list[dict]] = {}
        for result in results:
            if result in failed:
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
