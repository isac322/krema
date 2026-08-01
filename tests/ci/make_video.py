#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Encode a scenario's captured frames into a reviewable video.

Each frame is annotated with its frame number, the virtual animation time, the
scenario action firing on it, and any assertion anchored to it. A failing
assertion names a frame, so the reviewer can scrub straight to it instead of
guessing which moment the number came from.

The compositor cannot produce a screencast in this container (no DRM render
node), so the source is the client-side per-frame PNGs the probe already writes.
That is also the better source: it is exactly what the dock rendered, one image
per captured frame, with no compositor pacing in between.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

BANNER_HEIGHT = 34
FONT_CANDIDATES = (
    '/usr/share/fonts/google-droid-sans-fonts/DroidSans.ttf',
    '/usr/share/fonts/abattis-cantarell-vf-fonts/Cantarell-VF.otf',
    '/usr/share/fonts/urw-base35/NimbusSans-Regular.t1',
)


def load_font(size: int):
    for path in FONT_CANDIDATES:
        if pathlib.Path(path).exists():
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
    return ImageFont.load_default()


def describe_action(action: dict) -> str:
    kind = action['type']
    if kind in ('move', 'click', 'press', 'release'):
        where = action.get('item') or f"({action.get('x', 0)},{action.get('y', 0)})"
        button = f" {action['button']}" if action.get('button') else ''
        return f'{kind}{button} -> {where}'
    if kind == 'key':
        return f"key {action.get('key')}"
    if kind in ('setting', 'shortcut', 'menuitem'):
        value = action.get('value')
        return f"{kind} {action.get('name')}" + (f' = {value}' if value is not None else '')
    return kind


def annotate(src: pathlib.Path, dst: pathlib.Path, frame: int, vt: int,
             action: str | None, assertion: str | None, font, small,
             canvas_size: tuple[int, int]) -> None:
    """Draw one frame onto a fixed canvas.

    The canvas is fixed for the whole scenario because the dock window resizes
    mid-capture: changing the edge swaps 1024x108 for 108x768, and changing the
    icon size changes the height. An image2 sequence with varying resolution
    either fails to encode or produces a corrupt video.
    """
    image = Image.open(src).convert('RGB')
    canvas = Image.new('RGB', canvas_size, (18, 18, 18))
    canvas.paste(image, (0, BANNER_HEIGHT))
    draw = ImageDraw.Draw(canvas)
    draw.text((8, 6), f'f{frame:04d}  vt {vt} ms', fill=(220, 220, 220), font=font)
    if action:
        draw.text((190, 8), f'\u25b6 {action}', fill=(120, 200, 255), font=small)
    if assertion:
        draw.text((190, 20), f'\u2713 {assertion}', fill=(180, 255, 160), font=small)
    canvas.save(dst)


def canvas_for(frames: list[pathlib.Path]) -> tuple[int, int]:
    width = height = 0
    for path in frames:
        with Image.open(path) as image:
            width = max(width, image.width)
            height = max(height, image.height)
    height += BANNER_HEIGHT
    # H.264 needs even dimensions.
    return width + (width % 2), height + (height % 2)


def assertion_labels(scenario: dict) -> dict[int, str]:
    labels: dict[int, list[str]] = {}
    for spec in scenario.get('assertions', []):
        qa = spec.get('qa', '?')
        frames = []
        if spec.get('frame'):
            frames.append(spec['frame'])
        if spec.get('settled_by'):
            frames.append(spec['settled_by'])
        for frame in frames:
            labels.setdefault(frame, []).append(f"{qa} {spec['kind']}")
    return {frame: ', '.join(items[:2]) for frame, items in labels.items()}


def encode(frames_dir: pathlib.Path, out_base: pathlib.Path, fps: float) -> pathlib.Path | None:
    """Encode with whatever this ffmpeg build actually has.

    Fedora's ffmpeg-free carries neither libx264 nor libvpx; it does carry
    libopenh264, so H.264 in MP4 is the portable choice here. The ladder is
    ordered by how widely the result plays, and each entry names its container
    because the codec and the extension have to agree.
    """
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        print('  ffmpeg not installed; skipping video', file=sys.stderr)
        return None
    ladder = (
        ('.mp4', ['-c:v', 'libopenh264', '-b:v', '2M']),
        ('.mp4', ['-c:v', 'libsvtav1', '-crf', '35', '-preset', '8']),
        ('.mp4', ['-c:v', 'mpeg4', '-qscale:v', '4']),
    )
    for suffix, args in ladder:
        out = out_base.with_suffix(suffix)
        result = subprocess.run(
            [ffmpeg, '-y', '-loglevel', 'error', '-framerate', str(fps),
             '-i', str(frames_dir / 'a%05d.png'), *args,
             '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(out)],
            capture_output=True, text=True)
        if result.returncode == 0 and out.exists() and out.stat().st_size > 0:
            return out
        print(f"  {args[1]} failed: {result.stderr.strip()[:160]}", file=sys.stderr)
        out.unlink(missing_ok=True)
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--scenario', required=True, type=pathlib.Path)
    parser.add_argument('--run-dir', required=True, type=pathlib.Path)
    parser.add_argument('--fps', type=float, default=30.0,
                        help='playback fps; the capture is 1000/step_ms, so the '
                             'default plays roughly half speed')
    args = parser.parse_args()

    scenario = json.loads(args.scenario.read_text())
    if isinstance(scenario, list):
        scenario = {'actions': scenario, 'assertions': []}

    frames_dir = args.run_dir / 'pass1' / 'frames'
    ndjson = args.run_dir / 'pass1' / 'frames.ndjson'
    if not ndjson.exists() or not any(frames_dir.glob('f*.png')):
        print('  no frames captured; run with KREMA_SCREENSHOTS=1', file=sys.stderr)
        return 1

    rows = {json.loads(line)['frame']: json.loads(line)['vt']
            for line in ndjson.open() if line.strip()}
    actions = {a['frame']: describe_action(a) for a in scenario.get('actions', [])}
    labels = assertion_labels(scenario)

    font, small = load_font(16), load_font(11)
    annotated = args.run_dir / 'annotated'
    annotated.mkdir(exist_ok=True)
    sources = sorted(frames_dir.glob('f*.png'))
    canvas_size = canvas_for(sources)
    sticky_action, sticky_left = None, 0
    count = 0
    for index, src in enumerate(sources, start=1):
        frame = int(src.stem[1:])
        if frame in actions:
            sticky_action, sticky_left = actions[frame], 8
        elif sticky_left:
            sticky_left -= 1
        else:
            sticky_action = None
        annotate(src, annotated / f'a{index:05d}.png', frame, rows.get(frame, 0),
                 sticky_action, labels.get(frame), font, small, canvas_size)
        count += 1

    out = encode(annotated, args.run_dir / args.run_dir.name, args.fps)
    shutil.rmtree(annotated, ignore_errors=True)
    if out:
        print(f'  video: {out.name} ({count} frames, '
              f'{out.stat().st_size // 1024} KiB, {args.fps:g} fps)')
        return 0
    return 1


if __name__ == '__main__':
    sys.exit(main())
