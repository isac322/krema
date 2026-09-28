#!/usr/bin/env python3
"""Record a KWin screencast (PipeWire node) as I420 Matroska, for the GPU session.

usage: record.py SERIAL OUTPUT [--gl] [--min-gap-ms MS] [--ready FILE] [--trim FILE]

OUTPUT is a file, a FIFO or - (stdout); clips.sh hands a FIFO to
jellyfin-ffmpeg, which encodes on the VPU. A pipe or FIFO is enlarged to
1 MiB (the unprivileged maximum): through the default 64 KiB the 6 MB frames
stall the pipeline.
SIGINT ends the stream cleanly (EOS, then exit).

KWin's own frame limiter (the stream's max-framerate, 60 fps) counts whole
milliseconds from the moment the previous frame was queued, so it drifts and
skips a frame every few (about 54 fps delivered). This asks for
max-framerate=0 (no KWin limiter); KWin then sends a frame for every repaint
and every cursor move, up to about 120 a second. A probe right after
pipewiresrc drops any frame less than --min-gap-ms (default 15) after the
last kept one, before any conversion work, and keeps the rest with KWin's
timestamps: about 60 fps at the real frame times.

--gl: KWin fills linear DMA-BUFs by a GPU blit; GStreamer's GL elements
convert them to I420 and read them back (needs EGL, see CAPTURE_GL in
session-gpu.sh). Without it the stream uses memfd buffers, which KWin reads
back inside its render loop (slower; on GLES the image is upside down).

--ready FILE is created when the first frame arrives, so the caller can start
the choreography without waiting for the encoder to write its header.
SIGUSR1 marks a point in time; with --trim FILE the recorder writes
"START DUR" there on exit: the first mark's offset from the first frame and
the time to the last mark, in seconds. clips.sh marks the start and end of
each clip, and encode.sh trims to them.
"""
import argparse
import fcntl
import os
import signal
import stat
import sys
import time

import gi

gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("serial")
ap.add_argument("output")
ap.add_argument("--gl", action="store_true")
ap.add_argument("--min-gap-ms", type=float, default=15.0)
ap.add_argument("--ready", help="file created when the first frame arrives")
ap.add_argument("--trim", help="file that gets 'START DUR' (seconds) from the SIGUSR1 marks")
args = ap.parse_args()

Gst.init(None)
src = f"pipewiresrc name=src target-object={args.serial} keepalive-time=100"
fd = 1 if args.output == "-" else os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
if stat.S_ISFIFO(os.fstat(fd).st_mode):
    fcntl.fcntl(fd, fcntl.F_SETPIPE_SZ, 1 << 20)
sink = f"fdsink fd={fd} blocksize=1048576"
tail = " ! queue max-size-buffers=8 ! matroskamux streamable=true ! " + sink
if args.gl:
    desc = (src + ' ! video/x-raw(memory:DMABuf),format=DMA_DRM,drm-format=AR24,max-framerate=0/1'
            ' ! glupload ! glcolorconvert ! video/x-raw(memory:GLMemory),format=I420,colorimetry=bt709'
            ' ! queue max-size-buffers=4 ! gldownload ! video/x-raw,format=I420' + tail)
else:
    desc = (src + ' ! video/x-raw,max-framerate=0/1 ! videoconvert n-threads=2'
            ' ! video/x-raw,format=I420,colorimetry=bt709' + tail)
pipeline = Gst.parse_launch(desc)

gap = int(args.min_gap_ms * Gst.MSECOND)
stats = {"dropped": 0, "kept": [], "first": None, "marks": []}


def limit(_pad, info):
    pts = info.get_buffer().pts
    kept = stats["kept"]
    if kept and pts != Gst.CLOCK_TIME_NONE and pts - kept[-1] < gap:
        stats["dropped"] += 1
        return Gst.PadProbeReturn.DROP
    if not kept:
        stats["first"] = time.monotonic()
        if args.ready:
            open(args.ready, "w").close()
    kept.append(pts)
    return Gst.PadProbeReturn.OK


pipeline.get_by_name("src").get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, limit)


def mark():
    stats["marks"].append(time.monotonic())
    return GLib.SOURCE_CONTINUE


loop = GLib.MainLoop()
status = {"code": 0}


def on_message(_bus, msg):
    if msg.type == Gst.MessageType.EOS:
        loop.quit()
    elif msg.type == Gst.MessageType.ERROR:
        err, dbg = msg.parse_error()
        print(f"record.py: {err.message} ({dbg})", file=sys.stderr)
        status["code"] = 1
        loop.quit()


def stop():
    pipeline.send_event(Gst.Event.new_eos())
    return GLib.SOURCE_REMOVE


bus = pipeline.get_bus()
bus.add_signal_watch()
bus.connect("message", on_message)
try:
    gi.require_version("GLibUnix", "2.0")
    from gi.repository import GLibUnix
    signal_add = GLibUnix.signal_add
except (ValueError, ImportError):
    signal_add = GLib.unix_signal_add
for sig in (signal.SIGINT, signal.SIGTERM):
    signal_add(GLib.PRIORITY_DEFAULT, sig, stop)
signal_add(GLib.PRIORITY_DEFAULT, signal.SIGUSR1, mark)
pipeline.set_state(Gst.State.PLAYING)
loop.run()
pipeline.set_state(Gst.State.NULL)
# Frame spacing while something moves (gaps under 90 ms; keepalive resends
# come every 100 ms): fps and the number of gaps over 25 ms (a missed frame).
kept = stats["kept"]
moving = [(b - a) / Gst.SECOND for a, b in zip(kept, kept[1:]) if b - a < 90 * Gst.MSECOND]
fps = len(moving) / sum(moving) if moving else 0.0
print(f"record.py: kept {len(kept)} frames, dropped {stats['dropped']} (< {args.min_gap_ms} ms apart); "
      f"while moving {fps:.1f} fps, {sum(d > 0.025 for d in moving)} gaps > 25 ms", file=sys.stderr)
# Trim points: file time 0 is the first frame (ffmpeg starts its output
# there), so the marks are taken relative to the first frame's arrival.
marks = stats["marks"]
if args.trim and stats["first"] is not None and len(marks) >= 2:
    with open(args.trim, "w") as f:
        f.write(f"{marks[0] - stats['first']:.3f} {marks[-1] - marks[0]:.3f}\n")
sys.exit(status["code"])
