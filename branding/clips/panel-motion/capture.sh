#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Capture a genuine Plasma panel and this checkout's built Krema, in real time.
set -Eeuo pipefail
: "${KREMA_BINARY:?Set KREMA_BINARY to the built Krema executable}"
: "${OUT:?Set OUT to a new artifact directory outside the checkout}"
export DISPLAY="${DISPLAY:-:99}"
export PANEL_MOTION_SOURCE="${PANEL_MOTION_SOURCE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
export KREMA_BINARY OUT

# Never share a user's session bus, Plasma configuration, or running compositor.
exec dbus-run-session -- python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import time

source = Path(os.environ["PANEL_MOTION_SOURCE"]).resolve()
out = Path(os.environ["OUT"]).resolve()
binary = Path(os.environ["KREMA_BINARY"]).resolve()
display = os.environ["DISPLAY"]
fixture_name = os.environ.get("TEST_WINDOW_BINARY") or shutil.which("krema-test-window")
if not binary.is_file() or not os.access(binary, os.X_OK):
    raise SystemExit("KREMA_BINARY must name an executable built from PANEL_MOTION_SOURCE")
if not fixture_name or not os.access(fixture_name, os.X_OK):
    raise SystemExit("Build krema-test-window, then put it on PATH or set TEST_WINDOW_BINARY")
if source == out or source in out.parents:
    raise SystemExit("OUT must be outside PANEL_MOTION_SOURCE")
if not display.startswith(":") or not display[1:].isdigit():
    raise SystemExit("DISPLAY must be a local, unused display, such as :99 (no screen suffix)")
if Path(f"/tmp/.X11-unix/X{display[1:]}").exists() or Path(f"/tmp/.X{display[1:]}-lock").exists():
    raise SystemExit(f"Refusing to use an occupied X display: {display}")
required_tools = ("Xvfb", "xdpyinfo", "kwin_wayland", "plasmashell", "xdotool", "ffmpeg", "ffprobe")
if not os.environ.get("PANEL_MOTION_REVISION"):
    required_tools += ("git",)
for tool in required_tools:
    if not shutil.which(tool):
        raise SystemExit(f"Missing dependency: {tool}")
if not (source / "tests/appium/krema_e2e/kwin.py").is_file():
    raise SystemExit("PANEL_MOTION_SOURCE must contain tests/appium/krema_e2e/kwin.py")
out.mkdir(parents=True, exist_ok=False)
for name in ("home", "config", "data", "cache", "runtime", "tmp"):
    (out / name).mkdir(mode=0o700)
os.environ.update(HOME=str(out / "home"), XDG_CONFIG_HOME=str(out / "config"),
                  XDG_DATA_HOME=str(out / "data"), XDG_CACHE_HOME=str(out / "cache"),
                  XDG_RUNTIME_DIR=str(out / "runtime"), TMPDIR=str(out / "tmp"),
                  WAYLAND_DISPLAY="panel-motion-0", QT_QPA_PLATFORM="wayland",
                  QT_FORCE_STDERR_LOGGING="1", LIBGL_ALWAYS_SOFTWARE="1",
                  KWIN_WAYLAND_NO_PERMISSION_CHECKS="1", XDG_CURRENT_DESKTOP="KDE",
                  KDE_FULL_SESSION="true", KDE_SESSION_VERSION="6",
                  XCURSOR_THEME="breeze_cursors", XCURSOR_SIZE="24",
                  QT_LOGGING_RULES="krema.shell.visibility.debug=true")
# This capture deliberately does not inherit the old clips' slowed-clock setup.
for key in ("LD_PRELOAD", "FAKETIME", "FAKETIME_DONT_FAKE_MONOTONIC", "QT_WAYLAND_SHELL_INTEGRATION"):
    os.environ.pop(key, None)
sys.path.insert(0, str(source / "tests/appium"))
from krema_e2e import dbus, kwin

children = []
ledger = []
# Keep the ledger and artifacts; stopping a recording/session never removes files.
ledger.append({"kind": "directory", "identifier": str(out), "created_at": time.time(),
               "delete_command": "rm -r -- " + shlex.quote(str(out))})
stages = []
recording_start = None
recorder = None
h264_encoder = None
h264_options = None
plasma_pid = None
krema = None
fixture = None

def save_ledger():
    (out / "cleanup-ledger.json").write_text(json.dumps(ledger, indent=2) + "\n")

save_ledger()
print(f"Cleanup ledger: {out / 'cleanup-ledger.json'}", flush=True)

def spawn(args, name):
    with (out / f"{name}.log").open("ab") as log:
        proc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    children.append(proc)
    ledger.append({"kind": "process-group", "identifier": proc.pid, "command": args,
                   "created_at": time.time(), "delete_command": f"kill -TERM -- -{proc.pid}"})
    save_ledger()
    return proc

def stop(proc, sig=signal.SIGTERM):
    if proc is None or proc.poll() is not None:
        return
    os.killpg(proc.pid, sig)
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)

def wait_for(probe, label, timeout=30):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        dead = [p.pid for p in children if p.poll() is not None and p not in retired]
        if dead:
            raise RuntimeError(f"Child exited while waiting for {label}: {dead}; see {out}/*.log")
        try:
            last = probe()
            if last:
                return last
        except Exception as error:
            last = str(error)
        time.sleep(0.025)
    raise TimeoutError(f"Timed out waiting for {label}: {last!r}")

retired = set()

def plasma(js):
    return dbus.call("org.kde.plasmashell", "/PlasmaShell", "org.kde.PlasmaShell", "evaluateScript", "s", js)

def panel_state():
    return json.loads(plasma("print(JSON.stringify(panels().map(p=>({id:p.id,height:p.height,location:p.location,"
                           "floating:p.floating,hiding:p.hiding}))))"))

def dock_windows_state():
    if not krema:
        return []
    return kwin.evaluate(
        f"report(workspace.windowList().filter(w => (w.pid === {plasma_pid} || w.pid === {krema.pid}) && w.dock)"
        ".map(w => ({pid:w.pid, title:w.caption, dock:!!w.dock, hidden:"
        "(typeof w.hidden === 'boolean' ? w.hidden : null), hidden_type:typeof w.hidden,"
        "x:Math.round(w.frameGeometry.x), y:Math.round(w.frameGeometry.y),"
        "width:Math.round(w.frameGeometry.width), height:Math.round(w.frameGeometry.height)})));"
    )

def plasma_panel_oracle():
    return next((w for w in dock_windows_state() if w["pid"] == plasma_pid), None)

def wait_plasma_panel_hidden(hidden):
    def observed():
        state = plasma_panel_oracle()
        return state if state and state["hidden"] is hidden else False
    return wait_for(observed, f"Plasma panel hidden={hidden}")

def panel_dodge_state():
    state = plasma_panel_oracle()
    return state if state else {}

def panel_height(height):
    plasma(f"panels()[0].height={height};")
    wait_for(lambda: panel_state()[0]["height"] == height, f"panel height {height}")
    return wait_for(lambda: next((w for w in kwin.windows() if w.pid == plasma_pid and w.dock and w.height == height), None),
                    f"mapped Plasma panel at height {height}")

def mark(stage, **extra):
    entry = {"stage": stage, "unix_time": time.time(), "monotonic_time": time.monotonic(),
             "clip": clip, "seconds_after_recorder_launch": time.monotonic() - recording_start,
             "panel": panel_state(), "dock_windows": dock_windows_state(), "cursor": kwin.cursor_pos(),
             "windows": [{"pid": w.pid, "title": w.title, "dock": w.dock, "geometry": w.geometry}
                         for w in kwin.windows() if w.pid in (plasma_pid, krema.pid, fixture.pid)], **extra}
    stages.append(entry)
    (out / "stages.json").write_text(json.dumps(stages, indent=2) + "\n")
    print(json.dumps(entry), flush=True)

def glide(x0, y0, x1, y1, seconds, drag=False):
    # One X connection owns press, motion and release, matching clips.sh's rule.
    args = ["xdotool", "mousemove", str(round(x0)), str(round(y0))]
    if drag:
        args += ["mousedown", "1"]
    steps = max(1, round(seconds * 120))
    for step in range(1, steps + 1):
        u = step / steps
        t = u * u * (3 - 2 * u)
        args += ["mousemove", str(round(x0 + (x1 - x0) * t)), str(round(y0 + (y1 - y0) * t)),
                 "sleep", str(seconds / steps)]
    if drag:
        args += ["mouseup", "1"]
    subprocess.run(args, check=True, timeout=seconds * 3 + 10)

def window():
    return next((w for w in kwin.windows() if w.pid == fixture.pid and w.normal_window and not w.skip_taskbar), None)

def place_fixture():
    kwin.evaluate(f"const w=workspace.windowList().find(w=>w.pid==={fixture.pid} && w.normalWindow);"
                  "w.setMaximize(false,false); w.noBorder=false; w.frameGeometry={x:390,y:140,width:500,height:320};"
                  "workspace.activeWindow=w; report(true);")
    return wait_for(lambda: (w := window()) and abs(w.y - 140) < 3 and w, "fixture placement")

def start_krema(mode, reserve_screen_space=True):
    global krema
    if krema:
        stop(krema)
        retired.add(krema)
    (out / "config/kremarc").write_text("[General]\nEdge=1\nFloating=false\nFloatingMarginReference=1\n"
        "FloatingMargin=8\nIconSize=48\nMaxZoomFactor=1\nPreviewEnabled=false\nShadowEnabled=false\n"
        f"ReserveScreenSpace={'true' if reserve_screen_space else 'false'}\n"
        "PinnedLaunchers=applications:org.kde.dolphin.desktop,applications:org.kde.kate.desktop,applications:org.kde.konsole.desktop\n"
        f"VisibilityMode={mode}\n")
    krema = spawn([str(binary)], f"krema-{mode}")
    wait_for(lambda: any(w.dock and w.pid == krema.pid for w in kwin.windows()), "Krema mapped dock")

def visibility_offset():
    return (out / "krema-2.log").stat().st_size

def wait_visibility(visible, offset):
    def observed():
        with (out / "krema-2.log").open() as log:
            log.seek(offset)
            return f"Dock visible: {str(visible).lower()}" in log.read()
    wait_for(observed, f"Dodge target visible={visible}")

def begin(name):
    global recorder, recording_start, clip
    clip = name
    progress = out / f"{clip}.progress"
    recording_start = time.monotonic()
    recorder = spawn(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "warning", "-n",
        "-f", "x11grab", "-framerate", "120", "-video_size", "1280x720", "-draw_mouse", "1",
        "-i", f"{display}+0,0", "-c:v", "utvideo", "-pix_fmt", "gbrp", "-progress", str(progress),
        str(out / f"{clip}.mkv")], f"{clip}-ffmpeg")
    def first_frame():
        if not progress.exists():
            return False
        return any(line.startswith("frame=") and int(line[6:]) > 0 for line in progress.read_text().splitlines())
    wait_for(first_frame, "ffmpeg encoded first frame")
    mark("recording-ready")

def finish():
    mark("recording-end")
    stop(recorder, signal.SIGINT)
    retired.add(recorder)
    if recorder.returncode not in (0, 255):
        raise RuntimeError(f"ffmpeg exited with {recorder.returncode}")
    subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "warning", "-n", "-i", str(out / f"{clip}.mkv"),
                    "-an", "-vf", "fps=60", *h264_options, "-pix_fmt", "yuv420p",
                    "-movflags", "+faststart", str(out / f"{clip}.mp4")], check=True)
    subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "warning", "-n", "-i", str(out / f"{clip}.mkv"),
                    "-frames:v", "1", str(out / f"{clip}.png")], check=True)
    with (out / f"{clip}.ffprobe.json").open("w") as result:
        subprocess.run(["ffprobe", "-v", "error", "-show_format", "-show_streams", "-of", "json", str(out / f"{clip}.mp4")], stdout=result, check=True)

try:
    revision_override = os.environ.get("PANEL_MOTION_REVISION")
    provenance_error = None
    if revision_override:
        source_sha = revision_override
        source_dirty = "unknown"
        source_revision_source = "PANEL_MOTION_REVISION"
    else:
        try:
            source_sha = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
            source_dirty = bool(subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True))
            source_revision_source = "git"
        except (OSError, subprocess.CalledProcessError) as error:
            source_sha = "unknown"
            source_dirty = "unknown"
            source_revision_source = "unavailable"
            provenance_error = str(error)
    encoder_probe = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"], stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, check=False)
    if encoder_probe.returncode:
        raise RuntimeError(f"ffmpeg encoder probe failed with {encoder_probe.returncode}")
    encoder_names = set()
    for line in encoder_probe.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].startswith("V"):
            encoder_names.add(fields[1])
    if "libx264" in encoder_names:
        h264_encoder = "libx264"
        h264_options = ["-c:v", h264_encoder, "-preset", "medium", "-crf", "18"]
    elif "libopenh264" in encoder_names:
        h264_encoder = "libopenh264"
        h264_options = ["-c:v", h264_encoder, "-b:v", "4M"]
    else:
        raise RuntimeError("FFmpeg provides neither libx264 nor libopenh264")
    metadata = {"source_sha": source_sha, "source_revision_source": source_revision_source,
                "source_dirty": source_dirty, "source_provenance_error": provenance_error,
                "h264_encoder": h264_encoder,
                "source": str(source), "binary": str(binary), "binary_sha256": hashlib.file_digest(binary.open("rb"), "sha256").hexdigest(),
                "display": display, "resolution": "1280x720", "capture_fps_requested": 120, "output_fps": 60,
                "clock": "real time; no libfaketime", "verification": "capture only; inspect frames before publishing"}
    (out / "capture.json").write_text(json.dumps(metadata, indent=2) + "\n")
    with (out / "versions.txt").open("w") as versions:
        versions.write(sys.version + "\n")
        versions.write(f"selected_h264_encoder={h264_encoder}\n")
        version_commands = [["kwin_wayland", "--version"], ["plasmashell", "--version"], ["xdotool", "--version"],
                            ["ffmpeg", "-version"], ["ffprobe", "-version"]]
        for requested_args in version_commands:
            args = (["rpm", "-q", "plasma-workspace"]
                    if requested_args == ["plasmashell", "--version"] and shutil.which("rpm")
                    else requested_args)
            versions.write("\n$ " + shlex.join(args) + "\n")
            versions.flush()
            try:
                result = subprocess.run(args, stdout=versions, stderr=subprocess.STDOUT, check=False,
                                        env={**os.environ, "QT_QPA_PLATFORM": "offscreen"})
                if result.returncode:
                    versions.write(f"[nonfatal version-command exit {result.returncode}]\n")
            except OSError as error:
                versions.write(f"[nonfatal version-command failure: {error}]\n")
    spawn(["Xvfb", display, "-screen", "0", "1280x720x24", "-nolisten", "tcp"], "xvfb")
    wait_for(lambda: subprocess.run(["xdpyinfo", "-display", display], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0,
             "Xvfb accepting clients")
    spawn(["kwin_wayland", "--x11-display", display, "--no-lockscreen", "--socket", "panel-motion-0", "--width", "1280", "--height", "720"], "kwin")
    wait_for(lambda: (out / "runtime/panel-motion-0").is_socket() and dbus.has_name("org.kde.KWin"), "KWin socket and D-Bus service")
    wait_for(lambda: kwin.evaluate("report(workspace.windowList().length>=0);"), "KWin scripting")
    activity = shutil.which("kactivitymanagerd") or next((str(p) for p in (Path("/usr/libexec/kactivitymanagerd"), Path("/usr/lib/x86_64-linux-gnu/libexec/kactivitymanagerd")) if p.is_file()), None)
    if not activity:
        raise RuntimeError("kactivitymanagerd is required; install Plasma's activity manager")
    spawn([activity], "activity-manager")
    wait_for(lambda: dbus.has_name("org.kde.ActivityManager"), "activity manager")
    plasma_proc = spawn(["plasmashell", "--no-respawn"], "plasmashell")
    plasma_pid = plasma_proc.pid
    wait_for(lambda: dbus.has_name("org.kde.plasmashell") and plasma("print(desktops().length)").strip() != "0", "Plasma desktop script API")
    plasma("panels().forEach(p=>p.remove());")
    wait_for(lambda: panel_state() == [], "default panel removal in private session")
    plasma("var p=new Panel; p.location='bottom'; p.height=48; p.floating=false; p.hiding='none'; p.addWidget('org.kde.plasma.kickoff'); p.addWidget('org.kde.plasma.icontasks'); p.addWidget('org.kde.plasma.digitalclock');")
    wait_for(lambda: len(panel_state()) == 1, "genuine Plasma panel creation")
    panel_height(48)
    fixture = spawn([fixture_name, "--title", "Panel motion QA", "--app-id", "org.kde.krema.testwindow", "--color", "#264653"], "fixture")
    wait_for(window, "test-window fixture")
    place_fixture()
    start_krema(0)
    subprocess.run(["xdotool", "mousemove", "160", "160"], check=True)
    begin("panel-height")
    mark("floating-off-panel-48", krema_floating=False)
    time.sleep(1)  # Deliberate viewing holds, never startup readiness waits.
    mark("panel-72-request")
    panel_height(72)
    mark("panel-72-mapped")
    time.sleep(1)
    mark("panel-48-request")
    panel_height(48)
    mark("panel-48-mapped")
    time.sleep(1)
    # Request a reversal 60 ms after the taller panel maps; inspect the frames.
    mark("panel-motion-interruption-request", requested_delay_after_mapping_seconds=0.06)
    interruption_started = time.monotonic()
    panel_height(72)
    taller_panel_mapped = time.monotonic()
    time.sleep(0.06)
    reversal_requested = time.monotonic()
    plasma("panels()[0].height=48;")
    panel_height(48)
    mark("panel-motion-interruption-returned", first_request_monotonic=interruption_started,
         taller_panel_mapped_monotonic=taller_panel_mapped, reversal_request_monotonic=reversal_requested,
         verification="inspect frames; request timing alone does not prove interruption")
    time.sleep(1)
    finish()
    start_krema(0, reserve_screen_space=False)
    plasma("panels()[0].hiding='dodgewindows'; panels()[0].floating=false;")
    wait_for(lambda: (state := panel_state()) and state[0]["hiding"] == "dodgewindows"
             and state[0]["floating"] is False, "Plasma panel dodge configuration")
    w = place_fixture()
    subprocess.run(["xdotool", "mousemove", "160", "160"], check=True)
    wait_plasma_panel_hidden(False)
    begin("dodge")
    mark("plasma-dodge-visible", verification="Plasma panel hiding=dodgewindows and KWin hidden=false",
         panel_oracle=panel_dodge_state())
    time.sleep(1)
    x, y = w.x + w.width // 2, (w.y + w.client_y) // 2
    if w.client_y <= w.y:
        raise RuntimeError("Fixture has no server-side titlebar to drag")
    cx, cy = kwin.cursor_pos()
    glide(cx, cy, x, y, 0.8)
    mark("plasma-dodge-overlap-drag-begin", panel_oracle=panel_dodge_state())
    glide(x, y, x, y + 330, 1.5, drag=True)
    wait_plasma_panel_hidden(True)
    mark("plasma-dodge-hidden", verification="KWin window.hidden=true for the genuine Plasma panel",
         panel_oracle=panel_dodge_state())
    time.sleep(1)
    moved = window()
    if moved.y < w.y + 250:
        raise RuntimeError("Titlebar drag did not move the fixture over the dock")
    x, y = moved.x + moved.width // 2, (moved.y + moved.client_y) // 2
    cx, cy = kwin.cursor_pos()
    glide(cx, cy, x, y, 0.3)
    mark("plasma-dodge-clear-drag-begin", panel_oracle=panel_dodge_state())
    glide(x, y, x, y - 330, 1.5, drag=True)
    wait_plasma_panel_hidden(False)
    mark("plasma-dodge-revealed", verification="KWin window.hidden=false for the genuine Plasma panel",
         panel_oracle=panel_dodge_state())
    time.sleep(1)
    mark("plasma-dodge-height-follow-72-request")
    panel_height(72)
    mark("plasma-dodge-height-follow-72-mapped")
    time.sleep(1)
    mark("plasma-dodge-height-follow-48-request")
    panel_height(48)
    mark("plasma-dodge-height-follow-48-mapped")
    time.sleep(1)
    finish()
finally:
    # Close only process groups started here. Preserve artifacts and ledger.
    for proc in reversed(children):
        stop(proc, signal.SIGINT if proc is recorder else signal.SIGTERM)
    for entry in ledger:
        if entry["kind"] == "process-group":
            entry["stopped_at"] = time.time()
    save_ledger()
PY
