# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Environment of the E2E session (set up by entrypoint.sh / run.rb)."""

from __future__ import annotations

import ctypes
import os
import re
import subprocess
from pathlib import Path

#: Size of one compositor output.
SCREEN_WIDTH: int = int(os.environ.get("KREMA_E2E_SCREEN_WIDTH", "1024"))
SCREEN_HEIGHT: int = int(os.environ.get("KREMA_E2E_SCREEN_HEIGHT", "768"))

#: Number of virtual outputs (KREMA_E2E_OUTPUT_COUNT, default 1). With more
#: than one, kwin_wayland --virtual lays them out left to right.
OUTPUT_COUNT: int = int(os.environ.get("KREMA_E2E_OUTPUT_COUNT", "1"))

#: Absolute path of the krema binary under test.
KREMA_BINARY: str = os.environ.get("KREMA_E2E_BINARY", "krema")

#: Where screenshots/logs of the run go (host: tests/appium/artifacts/).
ARTIFACTS_DIR: Path = Path(os.environ.get("APPIUM_ARTIFACT_OUTPUT_PATH", "artifacts")).resolve()

#: selenium-webdriver-at-spi endpoint started by selenium-webdriver-at-spi-run.
WEBDRIVER_URL: str = f"http://127.0.0.1:{os.environ.get('FLASK_PORT', '4723')}"

#: Test-window fixture binary and its two app ids (both have .desktop files).
TEST_WINDOW_BINARY: str = "krema-test-window"
TEST_APP_ID: str = "org.kde.krema.testwindow"
TEST_APP_NAME: str = "Krema Test Window"
TEST_APP2_ID: str = "org.kde.krema.testwindow2"
TEST_APP2_NAME: str = "Krema Second Test Window"


def artifact_path(name: str) -> Path:
    """Return ``ARTIFACTS_DIR / name`` with parent directories created."""
    path = ARTIFACTS_DIR / name
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def _qt_version() -> tuple[int, ...]:
    """qVersion() of the container's libQt6Core, i.e. the Qt krema runs on."""
    qt_core = ctypes.CDLL("libQt6Core.so.6")
    qt_core.qVersion.restype = ctypes.c_char_p
    return tuple(int(part) for part in qt_core.qVersion().decode().split("."))


#: Runtime Qt version, e.g. (6, 10, 3). The accessibility tree differs across
#: Qt releases (tests/distro runs the suite on each distro's Qt).
QT_VERSION: tuple[int, ...] = _qt_version()


def _library_version(soname: str) -> tuple[int, ...]:
    """Version of an installed shared library, from its real file name
    (libKGlobalAccelD.so.0 -> libKGlobalAccelD.so.6.3.6)."""
    out = subprocess.run(["ldconfig", "-p"], check=True, capture_output=True, text=True).stdout
    lib = next(line.split(" => ")[1] for line in out.splitlines() if f"{soname} " in line)
    match = re.search(r"\.so\.(\d+(?:\.\d+)*)$", os.path.realpath(lib))
    assert match, f"no version in {os.path.realpath(lib)}"
    return tuple(int(part) for part in match[1].split("."))


#: LayerShellQt version, e.g. (6, 3, 6). krema built against < 6.4 (no
#: Window::setDesiredSize) sizes its layer surfaces through QWindow::resize
#: (KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE).
LAYERSHELLQT_VERSION: tuple[int, ...] = _library_version("libLayerShellQtInterface.so.6")
