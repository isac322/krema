# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Dependency-free document receipt helper for external-drop tests."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("mode", choices=["receive"])
parser.add_argument("--events", required=True)
parser.add_argument("--app-id", default="org.krema.pr137.source")
parser.add_argument("paths", nargs="*")
args = parser.parse_intermixed_args()


def record(**event):
    with open(args.events, "a", encoding="utf-8") as stream:
        stream.write(json.dumps({"pid": os.getpid(), **event}) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


record(event="document-received", app_id=args.app_id, argv=args.paths,
       files=[{"path": str(Path(path).resolve()),
               "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest()}
              for path in args.paths])
