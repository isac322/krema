# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""DND-007 external desktop-entry and document drops through real Qt input."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from xml.etree import ElementTree

import pytest

from krema_e2e import config, env, kwin
from krema_e2e import input as inp
from krema_e2e.krema import context_menu_entries
from krema_e2e.waits import wait_stable, wait_until

BOUNDARY = Path(__file__).parent / "tools" / "desktop_drop_source.py"
SETTINGS = {"PinnedLaunchers": [], "VisibilityMode": config.ALWAYS_VISIBLE,
            "MaxZoomFactor": 1.0, "PreviewEnabled": False}


def pins(krema):
    return config.as_list(krema.read_config().get("General", {}).get("PinnedLaunchers", ""))


def description(krema, title):
    root = ElementTree.fromstring(krema.page_source())
    bar = next(e for e in root.iter("tool_bar") if e.get("name") == "Krema Dock")
    return next((e.get("description", "") for e in bar.findall("button") if e.get("name") == title), "")


def records(path):
    return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []


def unchanged_for(krema, expected, seconds=1.5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        assert krema.is_running(), str(krema.log_path)
        assert pins(krema) == expected
        time.sleep(0.05)


@pytest.mark.kremarc(SETTINGS)
def test_dnd007_unknown_local_task_pin_is_rejected(krema, apps, tmp_path):
    unknown = tmp_path / "pr137-unknown-task.desktop"
    unknown.write_text("[Desktop Entry]\nType=Application\nName=PR137 unknown task\nExec=/bin/true\n")
    title = "PR137 unknown local task"
    task = apps.open(title, app_id=str(unknown))
    window = task.refresh()
    assert window is not None
    # Qt/KWin may strip the trailing desktop suffix; require the actual local
    # path to reach the compositor rather than assuming the stimulus succeeded.
    sent_ids = {str(unknown), str(unknown).removesuffix(".desktop")}
    observed_ids = {window.app_id, window.resource_class}
    evidence = {"requested_app_id": str(unknown), "kwin_app_id": window.app_id,
                "resource_class": window.resource_class, "window_pid": task.pid}
    env.artifact_path("pr137/unknown-task-identity.json").write_text(json.dumps(evidence, indent=2))
    if not sent_ids.intersection(observed_ids):
        pytest.fail(f"CAPABILITY GAP: KWin did not preserve the local-path app_id: {evidence}")
    krema.wait_for_item(title)
    before = pins(krema)
    assert before == []
    assert "Pinned" not in description(krema, title)
    menu = krema.open_context_menu(title)
    krema.choose_context_menu_entry("Pin to Dock", context_menu_entries(pinned=False, is_window=True))
    wait_until(lambda: all(w.internal_id != menu.internal_id for w in kwin.windows()), message="pin menu closes")
    unchanged_for(krema, before)
    assert "Pinned" not in description(krema, title)
    assert task.process.poll() is None
    assert task.refresh() is not None
    apps.close(task)
    krema.wait_for_no_item(title)
    krema.restart()
    assert pins(krema) == before
    assert krema.item(title) is None
    assert krema.is_running()


def external_drop(krema, path, target, label):
    events = env.artifact_path(f"pr137/{label}-drag.jsonl")
    output = env.artifact_path(f"pr137/{label}-source.log")
    child_env = krema.environment()
    with output.open("wb") as stream:
        process = subprocess.Popen(
            [
                env.TEST_WINDOW_BINARY,
                "--drag-source",
                "--events", str(events),
                "--app-id", "org.krema.pr137.source",
                str(path),
            ],
            env=child_env, stdout=stream, stderr=subprocess.STDOUT)
    try:
        window = wait_until(lambda: next((w for w in kwin.app_windows() if w.pid == process.pid), None),
                            message=f"external source {label} mapped")
        # KWin client geometry is the Qt widget's actual screen coordinate space.
        start = (window.client_x + window.client_width // 2,
                 window.client_y + window.client_height // 2)
        end = krema.settled_item_center(target)
        inp.drag(start, end, steps=24, step_ms=60, hold_ms=400)
        events_seen = wait_until(lambda: records(events) if any(r.get("event") == "drag-finished" for r in records(events)) else None,
                                 message=f"real QDrag {label} finished")
        started = next(r for r in events_seen if r.get("event") == "drag-start")
        assert "text/uri-list" in started["formats"]
        assert len(started["urls"]) == 1
        assert krema.is_running()
    finally:
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=10)
        wait_until(lambda: not any(w.pid == process.pid for w in kwin.app_windows()), message="external source closed")
    krema.move_away()


@pytest.mark.no_krema_autostart
@pytest.mark.kremarc({**SETTINGS, "PinnedLaunchers": [config.launcher(env.TEST_APP_ID)]})
def test_dnd007_external_launcher_and_document_drops(krema, tmp_path):
    # Install deterministic desktop entries into the real private XDG applications
    # directory before Krema starts. KService, TaskManager and Krema are unmodified.
    app_dir = krema.home / "data" / "applications"
    app_dir.mkdir(parents=True, exist_ok=True)
    receipt = env.artifact_path("pr137/document-receipt.jsonl")
    desktop_paths = []
    expected = [config.launcher(env.TEST_APP_ID)]
    for app_id, name in [("org.krema.pr137.a", "PR137 installed A"), ("org.krema.pr137.b", "PR137 installed B")]:
        path = app_dir / f"{app_id}.desktop"
        command = f'{sys.executable} "{BOUNDARY}" receive --events "{receipt}" --app-id {app_id} %F'
        path.write_text(f"[Desktop Entry]\nType=Application\nName={name}\nExec={command}\nIcon=text-x-generic\nMimeType=text/plain;\nStartupWMClass={app_id}\n")
        desktop_paths.append(path)
    subprocess.run(["kbuildsycoca6"], env=krema.environment(), check=True, capture_output=True, text=True, timeout=30)
    unknown = tmp_path / "pr137-unknown.desktop"
    unknown.write_text("[Desktop Entry]\nType=Application\nName=PR137 unknown\nExec=/bin/true\n")
    repeated = tmp_path / f"{desktop_paths[0].name}.desktop"
    repeated.write_bytes(desktop_paths[0].read_bytes())
    symlink = tmp_path / "pr137-link.desktop"
    symlink.symlink_to(desktop_paths[1])
    document = tmp_path / "pr137-document.txt"
    document.write_text("PR137 real external document payload\n")
    env.artifact_path("pr137/installed-identities.json").write_text(json.dumps([
        {"path": str(path), "menu_id": path.name, "launcher_url": config.launcher(path.name),
         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        for path in desktop_paths], indent=2))
    krema.start()
    krema.wait_for_item(env.TEST_APP_NAME)
    for label, path in [("unknown", unknown), ("repeated-suffix", repeated)]:
        before_names = wait_stable(krema.item_names)
        external_drop(krema, path, env.TEST_APP_NAME, label)
        unchanged_for(krema, expected)
        assert wait_stable(krema.item_names) == before_names
        assert not receipt.exists(), "rejected desktop unexpectedly launched the document receiver"
    for label, path, desktop, name in [
            ("registered", desktop_paths[0], desktop_paths[0], "PR137 installed A"),
            ("symlink", symlink, desktop_paths[1], "PR137 installed B")]:
        external_drop(krema, path, env.TEST_APP_NAME, label)
        expected.append(config.launcher(desktop.name))
        wait_until(lambda: pins(krema) == expected, message=f"{label} installed menuId saved")
        krema.wait_for_item(name)
        assert krema.item_names().count(name) == 1
        assert "Pinned" in description(krema, name)
    # Same installed file a second time must not add a duplicate.
    external_drop(krema, desktop_paths[0], env.TEST_APP_NAME, "registered-again")
    unchanged_for(krema, expected)
    assert krema.item_names().count("PR137 installed A") == 1
    external_drop(krema, document, "PR137 installed A", "document")
    received = wait_until(lambda: records(receipt), timeout=15, message="installed target receives real document argv")
    assert len(received) == 1
    assert received[0]["event"] == "document-received"
    assert received[0]["app_id"] == "org.krema.pr137.a"
    assert received[0]["argv"] == [str(document)]
    assert received[0]["files"] == [{"path": str(document.resolve()), "sha256": hashlib.sha256(document.read_bytes()).hexdigest()}]
    unchanged_for(krema, expected)
    krema.restart()
    assert pins(krema) == expected
    assert wait_stable(krema.item_names) == [env.TEST_APP_NAME, "PR137 installed A", "PR137 installed B"]
    assert krema.is_running()
