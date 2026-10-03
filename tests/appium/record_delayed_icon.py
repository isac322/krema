# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Record a real-time delayed task-icon update for merge evidence.

This file is intentionally not named ``test_*.py``.  The evidence workflow
selects it explicitly so ordinary Appium shards do not spend time recording
video.  The recorder captures KWin ScreenShot2 frames while a running task's
private desktop entry changes from a generic icon to the installed raw SVG.
"""

from __future__ import annotations

import json
import os
import subprocess
import threading
import time
import uuid
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from krema_e2e import env, input as inp, kwin
from krema_e2e.krema import Krema, Rect
from krema_e2e.windows import TestWindows
from krema_e2e.waits import wait_stable, wait_until


RAW_ICON = "/usr/share/krema-test-window/icon-raw-quadrants.svg"
GENERIC_ICON = "application-x-executable"
CAPTURE_INTERVAL = 0.075  # 13.3 Hz target; ScreenShot2 is the limiting cost.
BASELINE_SECONDS = 2.5
POST_HOLD_SECONDS = 2.5
BASELINE_WAIT_SECONDS = 8.0
POST_WAIT_SECONDS = 12.0
CACHE_TIMEOUT_SECONDS = 30.0
SIGNATURE_TOLERANCE = 14
MIN_SIGNATURE_PIXELS = 12
RAW_COLORS = np.asarray(
    [
        (240, 24, 240),
        (24, 240, 240),
        (240, 240, 24),
        (24, 240, 24),
    ],
    dtype=np.int16,
)


def _write_desktop_entry(path: Path, app_id: str, title: str, icon: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "[Desktop Entry]\n"
        "Type=Application\n"
        f"Name={title}\n"
        f"Exec=krema-test-window --app-id {app_id}\n"
        f"Icon={icon}\n"
        "Categories=Utility;\n"
        f"StartupWMClass={app_id}\n",
        encoding="utf-8",
    )


def _refresh_service_cache(krema: Krema) -> dict[str, Any]:
    """Refresh KService in the same private XDG environment as Krema."""
    result = subprocess.run(
        ["kbuildsycoca6"],
        env=krema.environment(),
        capture_output=True,
        text=True,
        timeout=CACHE_TIMEOUT_SECONDS,
        check=False,
    )
    details = {
        "returncode": result.returncode,
        "stdout": result.stdout[-2000:],
        "stderr": result.stderr[-2000:],
        "xdg_data_home": krema.environment()["XDG_DATA_HOME"],
        "xdg_cache_home": krema.environment()["XDG_CACHE_HOME"],
    }
    if result.returncode != 0:
        raise RuntimeError(f"kbuildsycoca6 failed: {json.dumps(details, sort_keys=True)}")
    return details


def _signature_counts(image: Any, area: tuple[int, int, int, int]) -> list[int]:
    """Count each raw-artwork colour in the captured dock-item crop."""
    x, y, width, height = area
    crop = image.crop((x, y, x + width, y + height)).convert("RGB")
    pixels = np.asarray(crop, dtype=np.int16)
    distances = np.max(np.abs(pixels[:, :, None, :] - RAW_COLORS[None, None, :, :]), axis=3)
    return [int(np.count_nonzero(distances[:, :, index] <= SIGNATURE_TOLERANCE)) for index in range(4)]




def _raw_oracle_with_layout(image: Any, area: tuple[int, int, int, int]) -> dict[str, Any]:
    x, y, width, height = area
    crop = image.crop((x, y, x + width, y + height)).convert("RGB")
    pixels = np.asarray(crop, dtype=np.int16)
    distances = np.max(np.abs(pixels[:, :, None, :] - RAW_COLORS[None, None, :, :]), axis=3)
    masks = [distances[:, :, index] <= SIGNATURE_TOLERANCE for index in range(4)]
    counts = [int(np.count_nonzero(mask)) for mask in masks]
    centers: list[tuple[float, float]] = []
    for mask in masks:
        rows, columns = np.nonzero(mask)
        if len(rows) == 0:
            centers.append((float("nan"), float("nan")))
        else:
            centers.append((float(np.mean(columns)), float(np.mean(rows))))
    finite_centers = [center for center in centers if np.isfinite(center).all()]
    if len(finite_centers) == 4:
        middle_x = float(np.median([center[0] for center in finite_centers]))
        middle_y = float(np.median([center[1] for center in finite_centers]))
        arrangement = (
            centers[0][0] < middle_x
            and centers[0][1] < middle_y
            and centers[1][0] > middle_x
            and centers[1][1] < middle_y
            and centers[2][0] < middle_x
            and centers[2][1] > middle_y
            and centers[3][0] > middle_x
            and centers[3][1] > middle_y
        )
    else:
        arrangement = False
    return {
        "counts": counts,
        "present": all(count >= MIN_SIGNATURE_PIXELS for count in counts),
        "arrangement": bool(arrangement),
        "centers": [[round(px, 2), round(py, 2)] for px, py in centers],
    }


def _bounded_area(rect: Rect, margin: int = 16) -> tuple[int, int, int, int]:
    left = max(0, rect.x - margin)
    top = max(0, rect.y - margin)
    right = min(env.SCREEN_WIDTH, rect.x + rect.width + margin)
    bottom = min(env.SCREEN_HEIGHT, rect.y + rect.height + margin)
    area = (left, top, right - left, bottom - top)
    if area[2] <= 0 or area[3] <= 0:
        raise AssertionError(f"dock item produced an empty capture area: rect={rect}, area={area}")
    return area


def test_record_delayed_icon(krema: Krema, apps: TestWindows, request: pytest.FixtureRequest) -> None:
    """Capture one existing item changing from generic to raw desktop artwork."""
    if not kwin.can_capture():
        pytest.fail("KWin cannot capture (QPainter compositing; the vgem/DRM runner is required)")
    icon_path = Path(RAW_ICON)
    if not icon_path.is_file():
        pytest.fail(f"raw artwork fixture is missing: expected installed file {RAW_ICON}")

    evidence_root = env.artifact_path("visual-evidence")
    frames_dir = evidence_root / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)
    frames_jsonl = evidence_root / "frames.jsonl"
    metadata_path = evidence_root / "metadata.json"

    suffix = uuid.uuid4().hex[:10]
    app_id = f"krema-delayed-icon-{suffix}"
    title = f"Delayed Icon {suffix}"
    desktop_path = krema.home / "data" / "applications" / f"{app_id}.desktop"
    if desktop_path.exists():
        pytest.fail(f"unique delayed-icon desktop entry already exists: {desktop_path}")
    previous_desktop = None
    request.addfinalizer(
        lambda: _restore_desktop_entry(krema, desktop_path, previous_desktop)
    )

    metadata: dict[str, Any] = {
        "scenario": "record_delayed_icon",
        "outcome": "running",
        "app_id": app_id,
        "title": title,
        "desktop_file": str(desktop_path),
        "icon_before": GENERIC_ICON,
        "icon_after": RAW_ICON,
        "capture_interval_seconds": CAPTURE_INTERVAL,
        "target_fps": round(1.0 / CAPTURE_INTERVAL, 3),
        "git_revision": os.environ.get("GITHUB_SHA"),
        "cache_refreshes": [],
    }
    rows: list[dict[str, Any]] = []
    recorder_error: list[str] = []
    baseline_max_counts = [0, 0, 0, 0]
    post_counts: list[int] | None = None
    baseline_ready = threading.Event()
    update_started = threading.Event()
    post_seen = threading.Event()
    stop_recording = threading.Event()
    transition_times: dict[str, int | None] = {
        "recording_started_ns": None,
        "baseline_ready_ns": None,
        "transition_started_ns": None,
        "cache_refreshed_ns": None,
        "transition_observed_ns": None,
    }
    area: tuple[int, int, int, int] | None = None
    recorder_thread: threading.Thread | None = None

    def record_frames() -> None:
        nonlocal post_counts
        assert area is not None
        start = time.monotonic()
        transition_times["recording_started_ns"] = time.monotonic_ns()
        baseline_deadline = start + BASELINE_SECONDS
        post_seen_at: float | None = None
        next_capture = start
        try:
            while not stop_recording.is_set():
                now = time.monotonic()
                if now < next_capture:
                    stop_recording.wait(next_capture - now)
                    if stop_recording.is_set():
                        break
                frame_index = len(rows)
                frame_path = frames_dir / f"{frame_index:06d}.png"
                image = kwin.screenshot(frame_path, area)
                timestamp_ns = time.monotonic_ns()
                rows.append({"frame": f"frames/{frame_index:06d}.png", "timestamp_ns": timestamp_ns})
                counts = _signature_counts(image, area)
                if not update_started.is_set():
                    for index, count in enumerate(counts):
                        baseline_max_counts[index] = max(baseline_max_counts[index], count)
                    if any(count >= MIN_SIGNATURE_PIXELS for count in counts):
                        recorder_error.append(
                            f"raw artwork appeared before metadata update: counts={counts}, frame={frame_index}"
                        )
                        baseline_ready.set()
                        break
                    if now >= baseline_deadline:
                        transition_times["baseline_ready_ns"] = timestamp_ns
                        baseline_ready.set()
                else:
                    oracle = _raw_oracle_with_layout(image, area)
                    if oracle["present"] and oracle["arrangement"] and not post_seen.is_set():
                        post_counts = oracle["counts"]
                        transition_times["transition_observed_ns"] = timestamp_ns
                        post_seen_at = time.monotonic()
                        post_seen.set()
                if post_seen_at is not None and time.monotonic() - post_seen_at >= POST_HOLD_SECONDS:
                    break
                next_capture += CAPTURE_INTERVAL
                if next_capture < time.monotonic():
                    next_capture = time.monotonic()
        except BaseException as error:  # noqa: BLE001 - propagate through main thread
            recorder_error.append(f"capture thread failed: {error!r}")
            baseline_ready.set()
            post_seen.set()

    def stop_and_join() -> None:
        stop_recording.set()
        if recorder_thread is not None:
            recorder_thread.join(timeout=10)
            if recorder_thread.is_alive():
                recorder_error.append("capture thread did not stop within 10 seconds")

    try:
        _write_desktop_entry(desktop_path, app_id, title, GENERIC_ICON)
        metadata["cache_refreshes"].append(_refresh_service_cache(krema))

        window = apps.open(title, app_id=app_id, width=320, height=220)
        item = krema.wait_for_item(title, timeout=15)
        wait_until(
            lambda: krema.item_names().count(title) == 1,
            timeout=10,
            message=lambda: f"exactly one delayed-icon dock item (have {krema.item_names()})",
        )
        wait_stable(lambda: krema.screen_rect(krema.wait_for_item(title)), duration=0.5, timeout=10)
        krema.move_away()
        inp.move(env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)
        item_rect = krema.screen_rect(item)
        area = _bounded_area(item_rect)
        metadata["area"] = {"x": area[0], "y": area[1], "width": area[2], "height": area[3]}
        metadata["window_internal_id"] = window.internal_id

        recorder_thread = threading.Thread(target=record_frames, name="delayed-icon-recorder", daemon=True)
        recorder_thread.start()
        if not baseline_ready.wait(BASELINE_WAIT_SECONDS):
            raise AssertionError("baseline capture did not reach its bounded pre-update interval")
        if recorder_error:
            raise AssertionError("; ".join(recorder_error))

        transition_times["transition_started_ns"] = time.monotonic_ns()
        update_started.set()
        _write_desktop_entry(desktop_path, app_id, title, RAW_ICON)
        metadata["cache_refreshes"].append(_refresh_service_cache(krema))
        transition_times["cache_refreshed_ns"] = time.monotonic_ns()
        metadata["desktop_updated_ns"] = transition_times["transition_started_ns"]
        metadata["cache_refreshed_ns"] = transition_times["cache_refreshed_ns"]

        if not post_seen.wait(POST_WAIT_SECONDS):
            diagnostics = {
                "item_names": krema.item_names(),
                "window": repr(window.refresh()),
                "baseline_max_counts": baseline_max_counts,
                "frames_captured": len(rows),
                "recorder_error": recorder_error,
            }
            raise AssertionError(f"delayed raw icon was not observed on the existing item: {diagnostics}")
        if recorder_error:
            raise AssertionError("; ".join(recorder_error))
        current_window = window.refresh()
        if current_window is None or current_window.internal_id != window.internal_id:
            raise AssertionError(
                f"task window identity changed during icon transition: before={window.internal_id!r}, "
                f"after={current_window!r}"
            )
        if krema.item_names().count(title) != 1:
            raise AssertionError(f"existing dock item was not stable during transition: {krema.item_names()}")
        metadata["outcome"] = "passed"
    except BaseException as error:
        metadata["outcome"] = "failed"
        metadata["error"] = str(error)
        raise
    finally:
        stop_and_join()
        metadata.update(transition_times)
        metadata["frame_count"] = len(rows)
        if len(rows) >= 2:
            metadata["capture_duration_ns"] = rows[-1]["timestamp_ns"] - rows[0]["timestamp_ns"]
        metadata["baseline_max_counts"] = baseline_max_counts
        metadata["post_counts"] = post_counts
        metadata["recorder_errors"] = recorder_error
        frames_jsonl.write_text("".join(json.dumps(row, sort_keys=True) + "\n" for row in rows), encoding="utf-8")
        metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _restore_desktop_entry(krema: Krema, path: Path, previous: bytes | None) -> None:
    """Restore only this test's private unique entry, even on assertion failure."""
    try:
        if previous is None:
            path.unlink(missing_ok=True)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(previous)
        _refresh_service_cache(krema)
    except Exception as error:  # noqa: BLE001 - teardown must not hide the test result
        env.artifact_path("visual-evidence/cleanup-error.txt").write_text(repr(error), encoding="utf-8")
