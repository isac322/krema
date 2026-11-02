#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Collect opt-in host and Docker-container resource diagnostics.

The default cidfile mode watches one running container and samples Docker
resource metrics. Snapshot mode performs one metadata-only ``docker run
--rm`` against an existing image and exits.

The caller supplies a cidfile created by ``docker run --cidfile``. This
process watches only that container, writes one JSON object per stats sample,
and exits when the container disappears or when it receives TERM/INT.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import signal
import socket
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from threading import Event
from typing import Any


_STOP = Event()
_SIZE_RE = re.compile(r"^\s*([0-9]+(?:\.[0-9]+)?)\s*([A-Za-z]+)\s*$")


def _stop(_signum: int, _frame: Any) -> None:
    _STOP.set()


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _run(command: list[str], timeout: float = 10.0) -> tuple[int, str, str]:
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 127, "", str(exc)
    return completed.returncode, completed.stdout, completed.stderr.strip()


def _read_meminfo() -> int | None:
    try:
        for line in Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
            if line.startswith("MemTotal:"):
                fields = line.split()
                if len(fields) >= 2:
                    return int(fields[1]) * 1024
    except (OSError, ValueError):
        pass
    return None


def _host_metadata() -> dict[str, Any]:
    return {
        "hostname": socket.gethostname(),
        "platform": platform.platform(),
        "kernel": platform.release(),
        "machine": platform.machine(),
        "cpu_count": os.cpu_count(),
        "memory_total_bytes": _read_meminfo(),
    }


def _parse_size(value: str) -> int | float | None:
    match = _SIZE_RE.match(value)
    if not match:
        return None
    amount = float(match.group(1))
    unit = match.group(2).lower()
    multipliers = {
        "b": 1,
        "kb": 1000,
        "kib": 1024,
        "mb": 1000**2,
        "mib": 1024**2,
        "gb": 1000**3,
        "gib": 1024**3,
        "tb": 1000**4,
        "tib": 1024**4,
    }
    multiplier = multipliers.get(unit)
    if multiplier is None:
        return None
    result = amount * multiplier
    return int(result) if result.is_integer() else result


def _parse_memory(value: str) -> tuple[int | float | None, int | float | None]:
    parts = [part.strip() for part in value.split("/", 1)]
    if len(parts) != 2:
        return None, None
    return _parse_size(parts[0]), _parse_size(parts[1])


def _parse_block_io(value: str) -> tuple[int | float | None, int | float | None]:
    parts = [part.strip() for part in value.split("/", 1)]
    if len(parts) != 2:
        return None, None
    return _parse_size(parts[0]), _parse_size(parts[1])


def _parse_stats(raw: str) -> dict[str, Any]:
    decoded = json.loads(raw)
    cpu = decoded.get("CPUPerc")
    cpu_percent = None
    if isinstance(cpu, str) and cpu.rstrip().endswith("%"):
        try:
            cpu_percent = float(cpu.rstrip()[:-1])
        except ValueError:
            pass
    memory_used, memory_limit = _parse_memory(str(decoded.get("MemUsage", "")))
    block_read, block_write = _parse_block_io(str(decoded.get("BlockIO", "")))
    pids = None
    try:
        pids = int(str(decoded.get("PIDs", "")).strip())
    except ValueError:
        pass
    return {
        "cpu_percent": cpu_percent,
        "memory_usage_bytes": memory_used,
        "memory_limit_bytes": memory_limit,
        "block_read_bytes": block_read,
        "block_write_bytes": block_write,
        "pids": pids,
        "raw": decoded,
    }


def _inspect_container(container_id: str) -> dict[str, Any] | None:
    rc, stdout, _stderr = _run(["docker", "inspect", container_id])
    if rc != 0:
        return None
    try:
        value = json.loads(stdout)
        return value[0] if value else None
    except (json.JSONDecodeError, IndexError, TypeError):
        return None


def _image_metadata(image: str | None) -> dict[str, Any]:
    if not image:
        return {"reference": None, "id": None, "digests": [], "tags": []}
    rc, stdout, stderr = _run(["docker", "image", "inspect", image])
    if rc != 0:
        return {"reference": image, "error": stderr or "docker image inspect failed"}
    try:
        inspected = json.loads(stdout)[0]
    except (json.JSONDecodeError, IndexError, TypeError):
        return {"reference": image, "error": "invalid docker image inspect output"}
    return {
        "reference": image,
        "id": inspected.get("Id"),
        "digests": inspected.get("RepoDigests") or [],
        "tags": inspected.get("RepoTags") or [],
    }


def _package_inventory_from_lines(
    lines: list[str],
    *,
    returncode: int,
    error: str | None = None,
) -> dict[str, Any]:
    manager = lines[0] if lines else "unavailable"
    inventory_hash = hashlib.sha256(
        "\n".join(sorted(lines[1:])).encode("utf-8")
    ).hexdigest()
    if returncode != 0:
        return {
            "status": "incomplete",
            "manager": manager,
            "packages": lines[1:],
            "inventory_hash": inventory_hash,
            "error": error or "package inventory command failed",
        }
    if manager == "unavailable":
        return {
            "status": "incomplete",
            "manager": manager,
            "packages": [],
            "inventory_hash": inventory_hash,
            "error": error or "no supported package manager found",
        }
    return {
        "status": "complete",
        "manager": manager,
        "packages": lines[1:],
        "inventory_hash": inventory_hash,
    }


_PACKAGE_INVENTORY_COMMAND = (
    "if command -v rpm >/dev/null 2>&1; then "
    "printf '%s\\n' rpm; rpm -qa; "
    "elif command -v dpkg-query >/dev/null 2>&1; then "
    "printf '%s\\n' dpkg; dpkg-query -W -f='${binary:Package}\\t${Version}\\n'; "
    "elif command -v pacman >/dev/null 2>&1; then "
    "printf '%s\\n' pacman; pacman -Q; "
    "else printf '%s\\n' unavailable; fi"
)


def _package_inventory(container_id: str) -> dict[str, Any]:
    rc, stdout, stderr = _run(
        ["docker", "exec", container_id, "sh", "-c", _PACKAGE_INVENTORY_COMMAND],
        timeout=30.0,
    )
    return _package_inventory_from_lines(
        stdout.splitlines(), returncode=rc, error=stderr or None
    )


def _snapshot_metadata(image: str) -> dict[str, Any]:
    image_info = _image_metadata(image)
    command = (
        "printf '%s\\n' __KREMA_CONTAINER__ \"$HOSTNAME\"; "
        "printf '%s\\n' __KREMA_BINARY__; "
        "if command -v sha256sum >/dev/null 2>&1; then "
        "sha256sum /usr/bin/krema; "
        "else printf '%s\\n' sha256sum-unavailable; fi; "
        "printf '%s\\n' __KREMA_PACKAGES__; "
        + _PACKAGE_INVENTORY_COMMAND
    )
    rc, stdout, stderr = _run(
        [
            "docker",
            "run",
            "--rm",
            "--pull=never",
            "--network=none",
            "--read-only",
            "--entrypoint",
            "sh",
            image,
            "-c",
            command,
        ],
        timeout=60.0,
    )
    lines = stdout.splitlines()
    probe_hostname = None
    if "__KREMA_CONTAINER__" in lines:
        container_marker = lines.index("__KREMA_CONTAINER__")
        if container_marker + 1 < len(lines):
            probe_hostname = lines[container_marker + 1] or None
    binary: dict[str, Any] = {"path": "/usr/bin/krema"}
    inventory_lines: list[str] = []
    errors: list[str] = []
    try:
        binary_marker = lines.index("__KREMA_BINARY__")
        package_marker = lines.index("__KREMA_PACKAGES__", binary_marker + 1)
        binary_output = lines[binary_marker + 1 : package_marker]
        inventory_lines = lines[package_marker + 1 :]
        if binary_output:
            fields = binary_output[0].split()
            if (
                len(fields) == 2
                and fields[1] == "/usr/bin/krema"
                and re.fullmatch(r"[0-9a-fA-F]{64}", fields[0])
            ):
                binary["sha256"] = fields[0].lower()
                binary["status"] = "complete"
            else:
                binary["status"] = "incomplete"
                binary["error"] = "sha256sum did not report /usr/bin/krema"
        else:
            binary["status"] = "incomplete"
            binary["error"] = "sha256sum returned no output"
    except ValueError:
        binary["status"] = "incomplete"
        binary["error"] = "snapshot output markers missing"
    if rc != 0:
        errors.append(stderr or "docker run failed")
    if binary.get("error"):
        errors.append(str(binary["error"]))
    inventory = _package_inventory_from_lines(
        inventory_lines,
        returncode=rc,
        error=stderr or None,
    )
    if inventory["status"] != "complete" and inventory.get("error"):
        errors.append(str(inventory["error"]))
    runtime_image = os.environ.get("KREMA_E2E_RUNTIME_IMAGE")
    builder_image = os.environ.get("KREMA_E2E_BUILDER_IMAGE")
    result: dict[str, Any] = {
        "schema_version": 2,
        "mode": "snapshot",
        "status": (
            "complete"
            if rc == 0
            and binary.get("status") == "complete"
            and inventory["status"] == "complete"
            else "incomplete"
        ),
        "collected_at": _utc_now(),
        "source_revision": os.environ.get("KREMA_E2E_SOURCE_REVISION")
        or os.environ.get("GITHUB_SHA"),
        "source_revision_env": (
            "KREMA_E2E_SOURCE_REVISION"
            if os.environ.get("KREMA_E2E_SOURCE_REVISION")
            else ("GITHUB_SHA" if os.environ.get("GITHUB_SHA") else None)
        ),
        "host": _host_metadata(),
        "probe_hostname": probe_hostname,
        "image": image_info,
        "binary": binary,
        "package_inventory": inventory,
        "runtime_image": _image_metadata(runtime_image) if runtime_image else None,
        "builder_image": _image_metadata(builder_image) if builder_image else None,
    }
    for key in ("image", "runtime_image", "builder_image"):
        value = result[key]
        if value and value.get("error"):
            errors.append(str(value["error"]))
    if errors:
        result["status"] = "incomplete"
        result["errors"] = sorted(set(errors))
    return result


def _container_status(container: dict[str, Any] | None) -> str | None:
    state = container.get("State") if container else None
    return state.get("Status") if isinstance(state, dict) else None


def _incomplete_sample(
    cid: str,
    *,
    reason: str,
    status: str | None = None,
    error: str | None = None,
) -> dict[str, Any]:
    record: dict[str, Any] = {
        "timestamp": _utc_now(),
        "container_id": cid,
        "status": "incomplete",
        "reason": reason,
    }
    if status is not None:
        record["container_status"] = status
    if error:
        record["error"] = error
    return record


def _write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _read_container_id(cidfile: Path) -> str | None:
    try:
        value = cidfile.read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return value or None


def _metadata(cid: str, container: dict[str, Any] | None) -> dict[str, Any]:
    config = container.get("Config") if container else {}
    image_ref = config.get("Image") if isinstance(config, dict) else None
    state = container.get("State") if container else None
    return {
        "schema_version": 1,
        "collected_at": _utc_now(),
        "source_revision": os.environ.get("KREMA_E2E_SOURCE_REVISION")
        or os.environ.get("GITHUB_SHA"),
        "source_revision_env": (
            "KREMA_E2E_SOURCE_REVISION"
            if os.environ.get("KREMA_E2E_SOURCE_REVISION")
            else ("GITHUB_SHA" if os.environ.get("GITHUB_SHA") else None)
        ),
        "container_id": cid,
        "host": _host_metadata(),
        "container": {
            "image_id": container.get("Image") if container else None,
            "image_reference": image_ref,
            "created": container.get("Created") if container else None,
            "state": state,
        },
        "image": _image_metadata(image_ref),
        "package_inventory": _package_inventory(cid),
    }


def _sample(cid: str) -> dict[str, Any]:
    inspected = _inspect_container(cid)
    status = _container_status(inspected)
    if inspected is None:
        return _incomplete_sample(cid, reason="container missing")
    if status != "running":
        reason = {
            "exited": "container exited",
            "dead": "container dead",
            "removing": "container removing",
        }.get(status, "container is not running")
        return _incomplete_sample(cid, reason=reason, status=status)

    rc, stdout, stderr = _run(
        ["docker", "stats", "--no-stream", "--format", "{{json .}}", cid],
        timeout=15.0,
    )
    record: dict[str, Any] | None = None
    if rc == 0:
        raw = stdout.strip().splitlines()
        if raw:
            try:
                record = {
                    "timestamp": _utc_now(),
                    "container_id": cid,
                    "stats": _parse_stats(raw[0]),
                }
            except (json.JSONDecodeError, TypeError, ValueError) as exc:
                return _incomplete_sample(
                    cid,
                    reason="invalid docker stats JSON",
                    status=status,
                    error=str(exc),
                )
    after_stats = _inspect_container(cid)
    after_status = _container_status(after_stats)
    if after_stats is None:
        return _incomplete_sample(cid, reason="container missing")
    if after_status != "running":
        reason = {
            "exited": "container exited",
            "dead": "container dead",
            "removing": "container removing",
        }.get(after_status, "container is not running")
        return _incomplete_sample(cid, reason=reason, status=after_status)
    if record is not None:
        return record
    return _incomplete_sample(
        cid,
        reason="docker stats unavailable",
        status=after_status,
        error=stderr or "docker stats failed",
    )


def _wait_for_running(cid: str, interval: float) -> dict[str, Any] | None:
    while not _STOP.is_set():
        inspected = _inspect_container(cid)
        if inspected is None:
            return None
        status = _container_status(inspected)
        if status == "running":
            return inspected
        if status not in {"created", "restarting"}:
            return inspected
        _STOP.wait(min(interval, 0.25))
    return None


def collect(cidfile: Path, output: Path, metadata_path: Path, interval: float) -> int:
    output.parent.mkdir(parents=True, exist_ok=True)
    output_handle = output.open("a", encoding="utf-8")
    try:
        cid = None
        while not _STOP.is_set() and cid is None:
            cid = _read_container_id(cidfile)
            if cid is None:
                _STOP.wait(min(interval, 0.25))
        if cid is None:
            return 0

        container = _wait_for_running(cid, interval)
        if container is None:
            if not _STOP.is_set():
                record = _incomplete_sample(cid, reason="container missing")
                output_handle.write(json.dumps(record, sort_keys=True) + "\n")
                output_handle.flush()
            return 0
        status = _container_status(container)
        if status != "running":
            reason = {
                "exited": "container exited",
                "dead": "container dead",
                "removing": "container removing",
            }.get(status, "container is not running")
            record = _incomplete_sample(cid, reason=reason, status=status)
            output_handle.write(json.dumps(record, sort_keys=True) + "\n")
            output_handle.flush()
            return 0

        _write_json(metadata_path, _metadata(cid, container))
        while not _STOP.is_set():
            record = _sample(cid)
            output_handle.write(json.dumps(record, sort_keys=True) + "\n")
            output_handle.flush()
            if record.get("status") == "incomplete":
                break
            _STOP.wait(interval)
        return 0
    finally:
        output_handle.close()


def collect_snapshot(image: str, metadata_path: Path) -> int:
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    _write_json(metadata_path, _snapshot_metadata(image))
    return 0


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--cidfile", type=Path)
    mode.add_argument("--snapshot-image")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--interval", type=float, default=1.0)
    args = parser.parse_args()
    if args.interval <= 0:
        parser.error("--interval must be positive")
    if args.cidfile is not None and args.output is None:
        parser.error("--output is required with --cidfile")
    if args.snapshot_image is not None and args.output is not None:
        parser.error("--output is only valid with --cidfile")
    return args


def main() -> int:
    args = _parse_args()
    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)
    try:
        if args.snapshot_image is not None:
            return collect_snapshot(args.snapshot_image, args.metadata)
        return collect(args.cidfile, args.output, args.metadata, args.interval)
    except Exception as exc:  # diagnostics must never affect the suite
        print(f"profile-resources: {exc}", file=sys.stderr)
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
