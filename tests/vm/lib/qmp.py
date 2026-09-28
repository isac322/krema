#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Minimal QMP client: `qmp.py <unix-socket> <json-command>`.

Performs the QMP handshake (qmp_capabilities) and sends exactly one command,
printing the raw response object to stdout. Exit status is 0 when the command
returns a "return" object, 2 on a QMP "error" object, 1 on transport errors.
"""

from __future__ import annotations

import json
import socket
import sys


def _recv_obj(sock_file) -> dict:
    while True:
        line = sock_file.readline()
        if not line:
            raise ConnectionError("QMP socket closed")
        obj = json.loads(line)
        # Skip async events; only return greetings and command responses.
        if "event" in obj:
            continue
        return obj


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 64
    sock_path, command = sys.argv[1], sys.argv[2]
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(30)
    try:
        s.connect(sock_path)
        f = s.makefile("rw", encoding="utf-8", newline="\n")
        _recv_obj(f)  # greeting
        for cmd in ('{"execute":"qmp_capabilities"}', command):
            f.write(cmd + "\n")
            f.flush()
            resp = _recv_obj(f)
        print(json.dumps(resp))
        if "return" in resp:
            return 0
        return 2
    except (OSError, ConnectionError, json.JSONDecodeError) as exc:
        print(f"qmp: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
