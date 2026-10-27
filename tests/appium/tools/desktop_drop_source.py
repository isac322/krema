# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""External Qt boundary for real QDrag input and document receipt."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

try:
    from PySide6.QtCore import QMimeData, Qt, QUrl
    from PySide6.QtGui import QDrag, QGuiApplication
    from PySide6.QtWidgets import QApplication, QWidget
except ImportError:
    from PyQt6.QtCore import QMimeData, Qt, QUrl
    from PyQt6.QtGui import QDrag, QGuiApplication
    from PyQt6.QtWidgets import QApplication, QWidget


parser = argparse.ArgumentParser()
parser.add_argument("mode", choices=["source", "receive"])
parser.add_argument("--events", required=True)
parser.add_argument("--app-id", default="org.krema.pr137.source")
parser.add_argument("--title", default="PR137 external MIME source")
parser.add_argument("paths", nargs="*")
args = parser.parse_intermixed_args()


def record(**event):
    with open(args.events, "a", encoding="utf-8") as stream:
        stream.write(json.dumps({"pid": os.getpid(), **event}) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


if args.mode == "receive":
    record(event="document-received", app_id=args.app_id, argv=args.paths,
           files=[{"path": str(Path(p).resolve()),
                   "sha256": hashlib.sha256(Path(p).read_bytes()).hexdigest()}
                  for p in args.paths])
    sys.exit(0)

# Set before QApplication, as the existing native test-window fixture does.
QGuiApplication.setDesktopFileName(args.app_id)
app = QApplication([sys.argv[0]])


class DragSource(QWidget):
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.press = event.position().toPoint()

    def mouseMoveEvent(self, event):
        if not event.buttons() & Qt.MouseButton.LeftButton:
            return
        if not hasattr(self, "press") or (event.position().toPoint() - self.press).manhattanLength() < app.startDragDistance():
            return
        del self.press
        mime = QMimeData()
        mime.setUrls([QUrl.fromLocalFile(str(Path(p).absolute())) for p in args.paths])
        drag = QDrag(self)
        drag.setMimeData(mime)
        record(event="drag-start", urls=[u.toString() for u in mime.urls()], formats=mime.formats())
        result = drag.exec(Qt.DropAction.CopyAction)
        record(event="drag-finished", action=result.value)


widget = DragSource()
widget.setWindowTitle(args.title)
widget.resize(240, 120)
widget.show()
record(event="source-started")
sys.exit(app.exec())
