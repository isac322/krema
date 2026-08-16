#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Shrink a finished run to what a reviewer actually needs.

A scenario writes ~140 PNGs (~15 MB) against ~90 KiB of encoded video, and its
NDJSON capture is another ~6 MB per pass, so an untrimmed run is ~600 MB for
4 MB of video. Kept: the video, the keyframes review.md links, the results, and
the first pass's capture stream gzipped for offline debugging. Dropped: every
other frame and the second pass's stream, which exists only to compare against
the first and has already been compared by then.
"""
from __future__ import annotations

import argparse
import gzip
import json
import pathlib
import shutil
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-dir', required=True, type=pathlib.Path)
    args = parser.parse_args()

    keep: set[pathlib.Path] = set()
    review = args.run_dir / 'review.json'
    if review.exists():
        manifest = json.loads(review.read_text())
        for entry in manifest.get('entries', []):
            for rel in entry.get('frames', []):
                keep.add((args.run_dir / rel).resolve())

    # The second pass exists to be compared with the first; assert_frames has
    # already done that and recorded the verdict in result.json.
    for extra in args.run_dir.glob('pass[2-9]/frames.ndjson'):
        extra.unlink()

    for stream in args.run_dir.glob('pass1/frames.ndjson'):
        with stream.open('rb') as src, gzip.open(f'{stream}.gz', 'wb', compresslevel=6) as dst:
            shutil.copyfileobj(src, dst)
        stream.unlink()

    removed = kept = 0
    for png in args.run_dir.glob('pass*/frames/*.png'):
        if png.resolve() in keep:
            kept += 1
        else:
            png.unlink()
            removed += 1
    for frames in args.run_dir.glob('pass*/frames'):
        if not any(frames.iterdir()):
            frames.rmdir()
    print(f'  pruned {removed} frames, kept {kept} for review')
    return 0


if __name__ == '__main__':
    sys.exit(main())
