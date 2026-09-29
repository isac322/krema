# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""pytest fixtures: fresh isolated krema + fixture windows per test, failure
artifacts (screenshot, AT-SPI tree, kremarc, KWin window list)."""

from __future__ import annotations

import json
import os
import re
from dataclasses import asdict
from typing import Any, Iterator

import pytest

from . import env, kwin
from . import input as inp
from .krema import Krema
from .windows import TestWindows


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line(
        "markers",
        "kremarc(settings): kremarc written before krema starts "
        "(same format as krema_e2e.config.write_kremarc; {} = krema defaults)",
    )
    config.addinivalue_line("markers", "no_krema_autostart: the krema fixture does not start krema")
    config.addinivalue_line(
        "markers",
        "outputs(n): needs a session with n virtual outputs (KREMA_E2E_OUTPUT_COUNT=n); "
        "unmarked tests need exactly one",
    )


def _shard() -> tuple[int, int] | None:
    """Parse ``KREMA_E2E_SHARD=<index>/<count>``; ``None`` when unset or 1."""
    spec = os.environ.get("KREMA_E2E_SHARD")
    if spec is None:
        return None
    match = re.fullmatch(r"(\d+)/(\d+)", spec.strip())
    if match is None:
        raise pytest.UsageError(
            f"KREMA_E2E_SHARD must be <index>/<count>, e.g. 1/3 (got {spec!r})"
        )
    index, count = int(match[1]), int(match[2])
    if count <= 1:
        return None
    if index >= count:
        raise pytest.UsageError(
            f"KREMA_E2E_SHARD index {index} out of range for {count} shard(s) "
            f"(valid: 0..{count - 1})"
        )
    return index, count


@pytest.hookimpl(trylast=True)
def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    """Select this shard's tests, then skip tests whose output count differs
    from this session's: a kwin session has a fixed number of outputs, so
    multi-output tests run in their own ``KREMA_E2E_OUTPUT_COUNT=n
    run-e2e.sh ...`` invocation."""
    shard = _shard()
    if shard is not None:
        index, count = shard
        # trylast makes this run after pytest's own -k/-m/--deselect filters
        # (_pytest.mark's impl): items is the post-filter collection, identical
        # across shards. Positions `pos % count == index` partition it into
        # disjoint sets — every index belongs to exactly one shard and no
        # shard sees the same items twice, so the union over i = 0..count-1
        # is the input list: no test is lost or duplicated. Round-robin
        # (not contiguous ranges) spreads a long file like test_06_settings
        # across shards.
        keep: list[pytest.Item] = []
        deselected: list[pytest.Item] = []
        for pos, item in enumerate(items):
            (keep if pos % count == index else deselected).append(item)
        if deselected:
            config.hook.pytest_deselected(items=deselected)
            items[:] = keep

    for item in items:
        marker = item.get_closest_marker("outputs")
        wanted = int(marker.args[0]) if marker is not None else 1
        if wanted != env.OUTPUT_COUNT:
            item.add_marker(
                pytest.mark.skip(
                    reason=f"needs {wanted} output(s), session has {env.OUTPUT_COUNT}; "
                    f"run with KREMA_E2E_OUTPUT_COUNT={wanted}"
                )
            )


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item: pytest.Item, call: pytest.CallInfo) -> Iterator[None]:
    outcome = yield
    rep = outcome.get_result()
    setattr(item, f"rep_{rep.when}", rep)


def _artifact_name(nodeid: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", nodeid).strip("_")


#: Pointer rest position between tests: mid-screen, away from the dock.
POINTER_PARK = (env.SCREEN_WIDTH // 2, env.SCREEN_HEIGHT // 2)


@pytest.fixture(scope="session", autouse=True)
def _pointer_capability() -> Iterator[None]:
    """Keep KWin's seat pointer capability for the whole session (see
    krema_e2e.input.hold_pointer_capability)."""
    proc = inp.hold_pointer_capability(POINTER_PARK)
    yield
    proc.terminate()
    proc.wait(5)


@pytest.fixture
def apps() -> Iterator[TestWindows]:
    """Opens krema-test-window fixture windows; closes leftovers afterwards."""
    manager = TestWindows()
    yield manager
    manager.close_all()


@pytest.fixture
def krema(request: pytest.FixtureRequest, tmp_path_factory: pytest.TempPathFactory, apps: TestWindows) -> Iterator[Krema]:
    """A started krema with private XDG dirs.

    Config precedence: ``@pytest.mark.kremarc({...})`` > class attribute
    ``kremarc`` > krema_e2e.krema.DEFAULT_CONFIG. Mark the test
    ``no_krema_autostart`` to call ``krema.start()`` yourself.

    The pointer is parked at :data:`POINTER_PARK` first, so no test starts
    with it resting where the previous one left it (e.g. on the spot where
    this test's dock item will appear, which would start the test hovered).
    """
    inp.move(*POINTER_PARK)
    marker = request.node.get_closest_marker("kremarc")
    settings: Any = None
    if marker is not None:
        settings = marker.args[0]
    elif request.cls is not None and getattr(request.cls, "kremarc", None) is not None:
        settings = request.cls.kremarc
    name = _artifact_name(request.node.nodeid)
    instance = Krema(tmp_path_factory.mktemp("krema-home"), settings, name=name)
    if request.node.get_closest_marker("no_krema_autostart") is None:
        instance.start()
    yield instance
    rep = getattr(request.node, "rep_call", None)
    if rep is not None and rep.failed:
        _collect_failure_artifacts(instance, name)
    crashed = instance.process is not None and instance.process.poll() is not None
    code = instance.process.returncode if crashed else None
    instance.stop()
    if crashed:
        pytest.fail(f"krema exited during the test (code {code}); log: {instance.log_path}")


def _collect_failure_artifacts(instance: Krema, name: str) -> None:
    try:
        kwin.screenshot(env.artifact_path(f"{name}/failure.png"))
    except Exception as e:  # noqa: BLE001 - best effort
        env.artifact_path(f"{name}/failure-screenshot-error.txt").write_text(repr(e))
    try:
        if instance.driver is not None:
            env.artifact_path(f"{name}/atspi-tree.xml").write_text(instance.page_source())
    except Exception as e:  # noqa: BLE001
        env.artifact_path(f"{name}/atspi-tree-error.txt").write_text(repr(e))
    try:
        env.artifact_path(f"{name}/kwin-windows.json").write_text(json.dumps([asdict(w) for w in kwin.windows()], indent=2))
    except Exception as e:  # noqa: BLE001
        env.artifact_path(f"{name}/kwin-windows-error.txt").write_text(repr(e))
    if instance.config_path.exists():
        env.artifact_path(f"{name}/kremarc").write_text(instance.config_path.read_text())


class KremaTest:
    """Optional base class: ``self.krema`` and ``self.apps`` are set for every
    test method; set the class attribute ``kremarc`` for a shared config."""

    kremarc: dict | None = None
    krema: Krema
    apps: TestWindows

    @pytest.fixture(autouse=True)
    def _krema_e2e_inject(self, krema: Krema, apps: TestWindows) -> None:
        self.krema = krema
        self.apps = apps
