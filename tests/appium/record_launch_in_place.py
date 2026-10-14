# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Record native pinned launch/group ordering; explicitly selected, never sharded.

Regenerate with a dedicated, sanitized, single-output package-image session::

    KREMA_E2E_SKIP_BUILD=1 KREMA_E2E_IMAGE=<package-image> \
    KREMA_E2E_BINARY=/usr/bin/krema KREMA_E2E_ARTIFACTS=<unused-output-dir> \
    KREMA_E2E_DOCKER_ARGS='-e KREMA_CAPTURE_ORIGINAL=false' \
    tests/appium/run-e2e.sh record_launch_in_place.py -x

For the original package use KREMA_CAPTURE_ORIGINAL=true. Accepted booleans are
1/0, true/false, yes/no, on/off (case-insensitive); other values fail. Optionally
forward KREMA_CAPTURE_SOURCE_SHA=<actual package source SHA> with another docker
``-e`` argument; absent provenance is recorded as null, never inferred. Unset
KREMA_E2E_SHARD. The existing runner deletes its artifact destination: use a new
one, never a concurrent QA run's directory/session. This script does not encode.

Artifacts under visual-evidence/launch-in-place: full-screen frames/*.png,
frames.jsonl (monotonic capture bounds/PTS), phases.jsonl (native windows and
physical dock order), metadata.json, capture.ffconcat (measured VFR durations).
Encode the manifest with ffmpeg -f concat -safe 0 -i capture.ffconcat
-fps_mode vfr; crop only the recorded default dock area from metadata.json.
Inspect decoded frame PTS and the separate 8.00 s pointer calibration before
using the launch segment. No promised frame rate, time warping, theme changes,
or synthetic product frames. Original mode records observed order, including
an honest non-reproduction; it does not waive counts, identity or capture errors.

A/B are installed krema-test-window launchers; C is the installed ICON-009
fixture. Only B's desktop entry is copied to Krema's private XDG home, adding
its installed Name as --title: its unmodified Exec otherwise uses A's title.
Group 2/3 use the existing TestWindows.open API, not a fake launch consumer.
"""

from __future__ import annotations

import configparser
import json
import os
import re
import signal
import statistics
import subprocess
import threading
import time
from pathlib import Path
from typing import Any
from xml.etree import ElementTree

import pytest

from krema_e2e import config, env, input as inp, kwin
from krema_e2e.krema import Krema
from krema_e2e.waits import wait_stable, wait_until
from krema_e2e.windows import TestWindows


CAPTURE_INTERVAL = 0.075  # Scheduling interval, not a claimed achieved frame rate.
CALIBRATION_SECONDS = 8.00
HOLD_SECONDS = 1.0
APP_IDS = (env.TEST_APP_ID, env.TEST_APP2_ID, "org.krema.icon009.drag-raw")
pytestmark = pytest.mark.no_krema_autostart


def _original_mode() -> bool:
    value = os.environ.get("KREMA_CAPTURE_ORIGINAL", "false").strip().lower()
    if value in {"1", "true", "yes", "on"}:
        return True
    if value in {"0", "false", "no", "off"}:
        return False
    raise pytest.UsageError("KREMA_CAPTURE_ORIGINAL must be an explicit boolean")


def _native_windows() -> list[kwin.Window]:
    return [window for window in kwin.app_windows() if window.app_id in APP_IDS]


def _prepare_launchers(krema: Krema) -> list[dict[str, str]]:
    """Read installed fixture metadata; isolate B's ambiguous launch title."""
    entries = []
    for app_id in APP_IDS:
        path = Path("/usr/share/applications") / f"{app_id}.desktop"
        parser = configparser.ConfigParser(interpolation=None)
        parser.optionxform = str  # Preserve desktop-entry key spelling in the private copy.
        with path.open(encoding="utf-8") as stream:
            parser.read_file(stream)
        entry = parser["Desktop Entry"]
        name = entry["Name"]
        installed_exec = entry["Exec"]
        entries.append({
            "desktop_file": path.name, "name": name,
            "icon": entry["Icon"], "startup_wm_class": entry["StartupWMClass"],
            "installed_exec": entry["Exec"], "capture_exec": entry["Exec"],
        })
        if app_id == env.TEST_APP2_ID:
            capture_exec = f'{installed_exec} --title "{name}"'
            private = krema.home / "data" / "applications" / path.name
            private.parent.mkdir(parents=True, exist_ok=True)
            parser.set("Desktop Entry", "Exec", capture_exec)
            with private.open("w", encoding="utf-8") as stream:
                parser.write(stream)
            entries[-1]["capture_exec"] = capture_exec
    subprocess.run(["kbuildsycoca6"], env=krema.environment(), capture_output=True, timeout=30, check=True)
    return entries


def _item_descriptions(krema: Krema) -> dict[str, str]:
    """Read the complete native tree, as the existing context-menu tests do."""
    root = ElementTree.fromstring(krema.page_source())
    toolbar = next(node for node in root.iter("tool_bar") if node.get("name") == "Krema Dock")
    return {item.get("name", ""): item.get("description", "") for item in toolbar.findall("button")}


def _dock_snapshot(krema: Krema) -> list[dict[str, Any]]:
    descriptions = _item_descriptions(krema)
    return [
        {"name": item.get_attribute("name"), "description": descriptions.get(item.get_attribute("name"), ""), "rect": krema.screen_rect(item)._asdict()}
        for item in krema.items()
    ]


def test_record_launch_in_place(krema: Krema, apps: TestWindows, request: pytest.FixtureRequest) -> None:
    original = _original_mode()
    source_sha = os.environ.get("KREMA_CAPTURE_SOURCE_SHA")
    if source_sha is not None and re.fullmatch(r"[0-9a-fA-F]{40}", source_sha) is None:
        raise pytest.UsageError("KREMA_CAPTURE_SOURCE_SHA must be the actual 40-hex package source SHA")
    if not kwin.can_capture():
        pytest.fail("Native ScreenShot2 capture requires the existing OpenGL/DRM runner")
    assert not kwin.app_windows(), "Use a dedicated empty, sanitized fixture session, not a concurrent QA session"
    evidence = env.artifact_path("visual-evidence/launch-in-place")
    evidence.mkdir(parents=True, exist_ok=False)
    frames = evidence / "frames"
    frames.mkdir()
    metadata: dict[str, Any] = {
        "scenario": "record_launch_in_place", "mode": "original" if original else "fixed",
        "outcome": "running", "source_sha": source_sha,
        "capture_interval_seconds": CAPTURE_INTERVAL, "clock": "time.monotonic_ns",
        "pts_origin": "first screenshot completion", "screen": {"width": env.SCREEN_WIDTH, "height": env.SCREEN_HEIGHT},
        "calibration_requested_seconds": CALIBRATION_SECONDS,
    }
    rows: list[dict[str, Any]] = []
    phases: list[dict[str, Any]] = []
    errors: list[str] = []
    stop = threading.Event()
    ready = threading.Event()
    phase = "baseline"
    launched: dict[str, kwin.Window] = {}

    def marker(name: str, **values: Any) -> None:
        nonlocal phase
        phase = name
        phases.append({
            "phase": name, "timestamp_ns": time.monotonic_ns(),
            "completed_frame_count": len(rows), **values,
        })

    def record_frames() -> None:
        next_capture = time.monotonic()
        try:
            while not stop.is_set():
                if stop.wait(max(0.0, next_capture - time.monotonic())):
                    break
                capture_phase = phase
                started = time.monotonic_ns()
                index = len(rows)
                kwin.screenshot(frames / f"{index:06d}.png")
                timestamp = time.monotonic_ns()
                rows.append({"frame": f"frames/{index:06d}.png", "phase": capture_phase, "capture_started_ns": started, "timestamp_ns": timestamp, "pts_ns": timestamp - (rows[0]["timestamp_ns"] if rows else timestamp)})
                ready.set()
                next_capture = max(next_capture + CAPTURE_INTERVAL, time.monotonic())
        except BaseException as error:  # propagate recorder failures to the test thread
            errors.append(repr(error))
            ready.set()

    def hold(seconds: float = HOLD_SECONDS) -> None:
        # Presentation/calibration interval, never used as a readiness assertion.
        stop.wait(seconds)
        assert not errors, f"native capture failed: {errors}"

    def park() -> None:
        point = (env.SCREEN_WIDTH // 2, 20)
        inp.move(*point)
        wait_until(lambda: kwin.cursor_pos() == point, message="native pointer parked")
        wait_until(lambda: not krema.preview_visible(), message="native preview hidden")

    def proof(name: str, expected_counts: tuple[int, int, int]) -> None:
        park()
        snapshot = wait_stable(lambda: _dock_snapshot(krema))
        native = _native_windows()
        counts = tuple(sum(window.app_id == app_id for window in native) for app_id in APP_IDS)
        ordered = [item["name"] for item in snapshot]
        physical = [item["name"] for item in sorted(snapshot, key=lambda item: item["rect"]["x"] + item["rect"]["width"] / 2)]
        group = next((item for item in snapshot if item["name"] == names[0]), None)
        group_matches = expected_counts[0] < 2 or (group is not None and f"{expected_counts[0]} windows" in (group["description"] or ""))
        marker(name, dock=snapshot, accessible_order=ordered, physical_order=physical, counts=dict(zip(APP_IDS, counts, strict=True)), windows=[{"pid": window.pid, "internal_id": window.internal_id, "app_id": window.app_id, "title": window.title} for window in native], order_matches=ordered == list(names) and physical == list(names), group_matches=group_matches)
        assert counts == expected_counts, f"{name}: actual native counts {counts}, expected {expected_counts}"
        assert len({window.internal_id for window in native}) == sum(expected_counts)
        assert len(snapshot) == len(names) and set(ordered) == set(names), f"{name}: missing or duplicate native dock items: {snapshot}"
        assert all(item["rect"]["width"] > 0 and item["rect"]["height"] > 0 for item in snapshot), snapshot
        assert group_matches, f"{name}: native windows did not become one accessible group: {snapshot}"
        hold()

    def click_launch(index: int, stage: str) -> kwin.Window:
        app_id = APP_IDS[index]
        before = {window.internal_id for window in kwin.app_windows()}
        marker(f"{stage}-trigger", action="native pointer click", desktop_file=f"{app_id}.desktop")
        try:
            krema.click_item(names[index])
            mapped = wait_until(lambda: [window for window in _native_windows() if window.app_id == app_id and window.internal_id not in before], timeout=15, message=f"real pinned {names[index]} launch mapped")
            assert len(mapped) == 1, mapped
            assert mapped[0].title == names[index], mapped[0]
            marker(f"{stage}-mapped", pid=mapped[0].pid, internal_id=mapped[0].internal_id, title=mapped[0].title)
            return mapped[0]
        finally:
            for window in _native_windows():
                if window.app_id == app_id and window.internal_id not in before:
                    launched[window.internal_id] = window

    def close_launched() -> None:
        # Signal only an exact native identity this recorder launched, never a name/PID search.
        for window in kwin.app_windows():
            owned = launched.get(window.internal_id)
            if owned is not None and (window.pid, window.app_id) == (owned.pid, owned.app_id):
                try:
                    os.kill(window.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
        wait_until(lambda: not any(window.internal_id in launched for window in kwin.app_windows()), message="recorder-launched native windows closed")

    request.addfinalizer(close_launched)
    recorder = threading.Thread(target=record_frames, name="launch-in-place-recorder", daemon=True)
    started = False
    try:
        metadata["desktop_entries"] = _prepare_launchers(krema)
        names = tuple(entry["name"] for entry in metadata["desktop_entries"])
        metadata["expected_order"] = list(names)
        krema.write_config({"PinnedLaunchers": [config.launcher(app_id) for app_id in APP_IDS], "SeparateLaunchers": True, "Edge": config.EDGE_BOTTOM, "VisibilityMode": config.ALWAYS_VISIBLE})
        krema.start()
        wait_until(lambda: krema.item_names() == list(names), message="installed cold A/B/C pins")
        dock = wait_stable(lambda: krema.surface_rect("dock"))
        assert dock is not None
        metadata["dock_rect"] = dock._asdict()
        park()
        recorder.start()
        started = True
        assert ready.wait(35), "first native frame did not complete"
        assert not errors, errors
        proof("cold-pins", (0, 0, 0))
        krema.hover_item(names[0])
        hover = kwin.cursor_pos()
        marker("calibration-start", pointer=list(hover), action="native dock hover hold")
        hold(CALIBRATION_SECONDS)
        park()
        marker("calibration-end", pointer=[env.SCREEN_WIDTH // 2, 20], action="native pointer park")
        hold()
        first = click_launch(0, "pin-a-first-launch")
        proof("pin-a-one-window", (1, 0, 0))
        extra = []
        for count in (2, 3):
            marker(f"pin-a-group-{count}-trigger", action="TestWindows.open", title=f"Group {count}", app_id=APP_IDS[0])
            extra.append(apps.open(f"Group {count}", app_id=APP_IDS[0], accessible=True))
            wait_until(lambda: f"{count} windows" in _item_descriptions(krema).get(names[0], ""), message=f"native {count}-window group")
            proof(f"pin-a-group-{count}", (count, 0, 0))
        second = click_launch(1, "pin-b-first-launch")
        proof("pin-b-running-final-order", (3, 1, 0))
        native = _native_windows()
        expected_ids = {first.internal_id, second.internal_id, *(window.internal_id for window in extra)}
        assert {window.internal_id for window in native} == expected_ids, "native window identity changed during grouping/next-pin launch"
        observed = [entry for entry in phases if "order_matches" in entry]
        mismatches = [entry["phase"] for entry in observed if not entry["order_matches"]]
        metadata["order_mismatch_phases"] = mismatches
        if not original:
            assert not mismatches, f"pinned physical A/B/C order changed in {mismatches}"
        metadata["outcome"] = ("original-order-change-observed" if mismatches else "original-order-change-not-reproduced") if original else "fixed-contract-passed"
    except BaseException as error:
        metadata["outcome"] = "failed"
        metadata["error"] = str(error)
        raise
    finally:
        stop.set()
        if started:
            recorder.join(timeout=35)
            if recorder.is_alive():
                errors.append("capture thread did not stop within 35 seconds")
        metadata["recorder_errors"] = errors
        metadata["frame_count"] = len(rows)
        if len(rows) >= 2:
            deltas = [b["timestamp_ns"] - a["timestamp_ns"] for a, b in zip(rows, rows[1:])]
            metadata["capture_duration_ns"] = rows[-1]["pts_ns"]
            metadata["frame_interval_ns"] = {"median": statistics.median(deltas), "p95": sorted(deltas)[max(0, (95 * len(deltas) + 99) // 100 - 1)], "max": max(deltas)}
        calibration = {entry["phase"]: entry["timestamp_ns"] for entry in phases if entry["phase"] in {"calibration-start", "calibration-end"}}
        if len(calibration) == 2:
            metadata["calibration_actual_ns"] = calibration["calibration-end"] - calibration["calibration-start"]
        manifest = ["ffconcat version 1.0"]
        for a, b in zip(rows, rows[1:]):
            manifest.extend([f"file '{a['frame']}'", "option framerate 1000", f"duration {(b['timestamp_ns'] - a['timestamp_ns']) / 1_000_000_000:.9f}"])
        if rows:
            manifest.extend([f"file '{rows[-1]['frame']}'", "option framerate 1000"])
        (evidence / "capture.ffconcat").write_text("\n".join(manifest) + "\n", encoding="utf-8")
        (evidence / "frames.jsonl").write_text("".join(json.dumps(row, sort_keys=True) + "\n" for row in rows), encoding="utf-8")
        (evidence / "phases.jsonl").write_text("".join(json.dumps(entry, sort_keys=True) + "\n" for entry in phases), encoding="utf-8")
        if errors:
            metadata["outcome"] = "failed"
        (evidence / "metadata.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        assert not errors, f"native capture failed: {errors}"
        assert len(rows) >= 2, "no usable native frame sequence was captured"
