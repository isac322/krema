# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Environment of the E2E session (set up by entrypoint.sh / run.rb)."""

from __future__ import annotations

import os
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
