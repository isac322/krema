#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Produce the animations that get embedded in a pull request, and their markdown.

GitHub will not play a video inline: release assets and raw `.mp4` both come
back as `application/octet-stream` with `content-disposition: attachment`, so
`<video src=...>` never renders. Raw image URLs do return a real `image/*`
content type, so an animated GIF referenced with `![]()` is the only thing that
shows up without a download.

Those GIFs have to be pushed to a branch to get a raw URL, and force-pushing
does not reclaim the old blobs, so this deliberately animates only what someone
would actually open: the scenarios that failed, plus a couple of canonical ones
so a green run still shows the harness working. Everything else stays as MP4 in
the build artifact.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys

CANONICAL = ('hover-zoom', 'dock-visibility-autohide')


def encode_gif(src: pathlib.Path, dst: pathlib.Path, fps: int, width: int) -> bool:
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        return False
    chain = (f'fps={fps},scale={width}:-1:flags=lanczos,'
             'split[a][b];[a]palettegen=max_colors=64[p];[b][p]paletteuse')
    result = subprocess.run(
        [ffmpeg, '-y', '-loglevel', 'error', '-i', str(src), '-vf', chain,
         '-loop', '0', str(dst)],
        capture_output=True, text=True)
    if result.returncode:
        print(f'  gif failed for {src.name}: {result.stderr.strip()[:160]}', file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-dir', required=True, type=pathlib.Path)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    parser.add_argument('--base-url', default='',
                        help='raw URL prefix the GIFs will be reachable at')
    parser.add_argument('--fps', type=int, default=20)
    parser.add_argument('--width', type=int, default=760)
    args = parser.parse_args()

    results = [json.loads(p.read_text()) for p in sorted(args.run_dir.glob('*/result.json'))]
    if not results:
        print('No scenario results found.')
        return 0

    failing = [r for r in results if any(i['failures'] for i in r['qa'])]
    chosen = failing + [r for r in results
                        if r['scenario'] in CANONICAL and r not in failing]

    args.out.mkdir(parents=True, exist_ok=True)
    made: list[tuple[dict, str]] = []
    for result in chosen:
        mp4 = args.run_dir / result['scenario'] / f"{result['scenario']}.mp4"
        if not mp4.exists():
            continue
        gif = args.out / f"{result['scenario']}.gif"
        if encode_gif(mp4, gif, args.fps, args.width):
            made.append((result, gif.name))

    lines: list[str] = ['## UI frame tests', '']
    total = sum(len(r['qa']) for r in results)
    bad = sum(1 for r in results for i in r['qa'] if i['failures'])
    lines.append(f"{'❌' if bad else '✅'} **{total - bad}/{total}** QA items passed "
                 f'across {len(results)} scenarios.')
    lines.append('')

    for result, name in made:
        failures = [(i['id'], f) for i in result['qa'] for f in i['failures']]
        title = f"{'❌' if failures else '✅'} {result['scenario']}"
        lines.append(f'### {title}')
        lines.append('')
        lines.append(result['description'])
        lines.append('')
        for qa, failure in failures:
            lines.append(f'- `{qa}` — {failure}')
        if failures:
            lines.append('')
        if args.base_url:
            lines.append(f'![{result["scenario"]}]({args.base_url}/{name})')
        else:
            lines.append(f'`{name}`')
        lines.append('')
        lines.append('<sub>Every frame is stamped with its number and virtual time; '
                     'the failing frame numbers above appear in the banner.</sub>')
        lines.append('')

    if not made:
        lines.append('No animations were published for this run.')
        lines.append('')
    lines.append('MP4s for every scenario, the per-frame capture streams and the '
                 'review keyframes are in the `frame-captures` artifact.')

    (args.out / 'comment.md').write_text('\n'.join(lines))
    print(f'  {len(made)} preview animation(s) in {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
