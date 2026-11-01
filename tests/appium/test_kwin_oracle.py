# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Public KWin oracle contracts exercised against the private compositor."""

from __future__ import annotations

import json
import re
from pathlib import Path
from uuid import uuid4

import pytest

from krema_e2e import dbus, kwin
from krema_e2e.waits import wait_until
from krema_e2e.windows import TestWindows


@pytest.mark.parametrize("value", [None, False, True, 42, 1.25, "KWin \"query\"\nλ", [1, None, False], {"nested": [True, {"value": 3}]}])
def test_evaluate_returns_json_values(value: object) -> None:
    assert kwin.evaluate(f"report({json.dumps(value)});") == value


def test_evaluate_reports_after_lower_script_id_is_unloaded(tmp_path: Path) -> None:
    """A live higher ID must not swallow a subsequent query's callback."""
    nonce = uuid4().hex
    before = {"query": nonce, "phase": "before"}
    after = {"query": nonce, "phase": "after"}
    # Initialize any long-lived oracle before creating the allocator gap.
    assert kwin.evaluate(f"report({json.dumps(before)});") == before
    plugin_a, plugin_b = (f"krema_e2e_oracle_{nonce}_{name}" for name in ("a", "b"))
    paths = [tmp_path / "a.js", tmp_path / "b.js"]
    for path in paths:
        path.write_text("void 0;\n", encoding="utf-8")

    try:
        script_ids = []
        for path, plugin in zip(paths, (plugin_a, plugin_b)):
            script_id = dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "loadScript", "ss", str(path), plugin)
            assert script_id >= 0, f"KWin did not load owned script {plugin}"
            script_ids.append(script_id)
            dbus.call("org.kde.KWin", f"/Scripting/Script{script_id}", "org.kde.kwin.Script", "run")
        assert script_ids[1] > script_ids[0], f"expected increasing script IDs, got {script_ids}"
        assert dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "isScriptLoaded", "s", plugin_b)

        assert dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", "s", plugin_a)
        wait_until(
            lambda: not dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "isScriptLoaded", "s", plugin_a),
            timeout=5,
            message="owned lower-ID KWin script to be unloaded",
        )
        assert dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "isScriptLoaded", "s", plugin_b)

        assert kwin.evaluate(f"report({json.dumps(after)});", timeout=5) == after
        assert dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "isScriptLoaded", "s", plugin_b)
    finally:
        # Both names are ours, even if loading or running either script failed.
        try:
            dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", "s", plugin_b)
        finally:
            dbus.call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", "s", plugin_a)


def test_evaluate_isolates_query_const_declarations() -> None:
    first = {"query": uuid4().hex, "value": 1}
    second = {"query": uuid4().hex, "value": 2}
    assert kwin.evaluate(f"const queryValue = {json.dumps(first)}; report(queryValue);") == first
    assert kwin.evaluate(f"const queryValue = {json.dumps(second)}; report(queryValue);") == second


def test_evaluate_recovers_after_javascript_exception() -> None:
    message = f"oracle query failed {uuid4().hex}"
    with pytest.raises(RuntimeError, match=re.escape(message)):
        kwin.evaluate(f"throw new Error({json.dumps(message)});")

    expected = {"recovered": uuid4().hex, "after": "exception"}
    assert kwin.evaluate(f"report({json.dumps(expected)});") == expected


def test_evaluate_recovers_after_body_does_not_report() -> None:
    with pytest.raises(TimeoutError):
        kwin.evaluate("const queryValue = 'no callback';", timeout=0.25)

    expected = {"recovered": uuid4().hex, "after": "timeout"}
    assert kwin.evaluate(f"const queryValue = {json.dumps(expected)}; report(queryValue);") == expected


def test_window_consumers_observe_fixture_creation_and_removal(apps: TestWindows) -> None:
    title = f"Oracle lifecycle {uuid4().hex}"
    assert all(w.title != title for w in kwin.windows())
    assert all(w.title != title for w in kwin.app_windows())

    fixture = apps.open(title)
    internal_id = fixture.internal_id
    try:
        for snapshot in (kwin.windows(), kwin.app_windows()):
            matches = [w for w in snapshot if w.internal_id == internal_id]
            assert len(matches) == 1
            observed = matches[0]
            assert (observed.pid, observed.title, observed.app_id) == (fixture.pid, title, fixture.app_id)
    finally:
        apps.close(fixture)

    assert all(w.internal_id != internal_id for w in kwin.windows())
    assert all(w.internal_id != internal_id for w in kwin.app_windows())
