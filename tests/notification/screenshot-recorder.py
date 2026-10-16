#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Capture a live KWin output through ScreenShot2 and encode it as MP4."""

import base64
import json
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


CAPTURE_INTERVAL = 0.1


def _quoted(path: Path) -> str:
    return "'" + str(path).replace("'", "'\\''") + "'"


def _remove_tree(path: Path) -> None:
    for child in path.iterdir():
        child.unlink()
    path.rmdir()


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} OUTPUT STOP_FILE", file=sys.stderr)
        return 2

    output = Path(sys.argv[1]).expanduser()
    stop_file = Path(sys.argv[2]).expanduser()
    output.parent.mkdir(parents=True, exist_ok=True)
    frames_dir = Path(tempfile.mkdtemp(prefix=f".{output.stem}.frames-", dir=output.parent))
    frames_jsonl = output.with_name(f"{output.stem}.frames.jsonl")
    ffconcat = output.with_name(f"{output.stem}.ffconcat")
    stopping = False
    frame_rows = []
    success = False

    def request_stop(_signum, _frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    try:
        with frames_jsonl.open("w", encoding="utf-8") as metadata:
            next_capture = time.monotonic()
            while not stopping and stop_file.exists():
                delay = next_capture - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                if stopping or not stop_file.exists():
                    break
                next_capture += CAPTURE_INTERVAL

                try:
                    proc = subprocess.run(
                        ["selenium-webdriver-at-spi-screenshotter", "0", "0", "0", "0"],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        check=False,
                        timeout=30,
                    )
                except (OSError, subprocess.TimeoutExpired) as error:
                    print(f"[screenshot-recorder] capture failed: {error}", file=sys.stderr, flush=True)
                    next_capture = max(next_capture, time.monotonic())
                    continue

                timestamp_ns = time.monotonic_ns()
                if proc.returncode != 0 or not proc.stdout:
                    detail = proc.stderr.decode(errors="replace").strip()[-2000:]
                    print(
                        f"[screenshot-recorder] screenshotter exited {proc.returncode}: {detail}",
                        file=sys.stderr,
                        flush=True,
                    )
                    next_capture = max(next_capture, time.monotonic())
                    continue

                try:
                    png = base64.b64decode(proc.stdout.strip(), validate=True)
                except ValueError as error:
                    print(f"[screenshot-recorder] invalid PNG response: {error}", file=sys.stderr, flush=True)
                    next_capture = max(next_capture, time.monotonic())
                    continue

                frame = frames_dir / f"frame-{len(frame_rows):06d}.png"
                frame.write_bytes(png)
                row = {
                    "frame": str(frame),
                    "timestamp_ns": timestamp_ns,
                    "pts_ns": timestamp_ns - (frame_rows[0]["timestamp_ns"] if frame_rows else timestamp_ns),
                }
                frame_rows.append(row)
                metadata.write(json.dumps(row, sort_keys=True) + "\n")
                metadata.flush()
                next_capture = max(next_capture, time.monotonic())

        if not frame_rows:
            print("[screenshot-recorder] no ScreenShot2 frames were captured", file=sys.stderr)
            return 1

        manifest = ["ffconcat version 1.0"]
        for index, row in enumerate(frame_rows):
            manifest.extend([f"file {_quoted(Path(row['frame']))}", "option framerate 1000"])
            if index + 1 < len(frame_rows):
                duration = max(0.001, (frame_rows[index + 1]["timestamp_ns"] - row["timestamp_ns"]) / 1_000_000_000)
                manifest.append(f"duration {duration:.9f}")
        ffconcat.write_text("\n".join(manifest) + "\n", encoding="utf-8")

        result = subprocess.run(
            [
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-f",
                "concat",
                "-safe",
                "0",
                "-i",
                str(ffconcat),
                "-fps_mode",
                "vfr",
                "-an",
                "-c:v",
                "libx264",
                "-pix_fmt",
                "yuv420p",
                "-movflags",
                "+faststart",
                str(output),
            ],
            check=False,
        )
        if result.returncode != 0:
            print(f"[screenshot-recorder] ffmpeg exited {result.returncode}", file=sys.stderr)
            return result.returncode
        if not output.is_file() or output.stat().st_size == 0:
            print(f"[screenshot-recorder] output is missing or empty: {output}", file=sys.stderr)
            return 1

        success = True
        return 0
    finally:
        if success:
            _remove_tree(frames_dir)
        else:
            print(f"[screenshot-recorder] retained failed capture frames at {frames_dir}", file=sys.stderr)
        stop_file.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
