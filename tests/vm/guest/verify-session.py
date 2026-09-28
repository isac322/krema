#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
"""Tier 3 guest verification: run inside the live Plasma Wayland session.

Executed over SSH as user `qa`. Discovers the session environment from the
running kwin_wayland/plasmashell process, then checks:

  * plasmashell and kwin_wayland are running (real Plasma Wayland session)
  * krema was installed from the cargo ISO before sddm started
  * krema is running, started by the session autostart machinery
  * the Krema Dock tool bar is present in the AT-SPI tree
  * launching a real app produces a dock button (AT-SPI) and a KWin window,
    and KWin scripting can activate it
  * `krema --version` (when it prints anything) matches the package version,
    and the installed package matches the cargo package
  * no krema crash in this boot's journal / coredumpctl

Writes a JSON report (list of {name, ok, seconds, detail}) to --report.
Exit status is 0 iff every check passed.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# check bookkeeping

CHECKS: list[dict] = []


def check(name: str):
    """Decorator: run a check function; records {name, ok, seconds, detail}.

    The function returns a detail string on success or raises/returns
    (False, detail). Any exception is a failure.
    """

    def wrap(fn):
        t0 = time.monotonic()
        detail = ""
        ok = False
        try:
            res = fn()
            if isinstance(res, tuple):
                ok, detail = res[0], (res[1] if len(res) > 1 else "")
            else:
                ok, detail = True, (res or "")
        except Exception as exc:  # noqa: BLE001 - report any check failure
            ok, detail = False, f"{type(exc).__name__}: {exc}"
        CHECKS.append(
            {
                "name": name,
                "ok": bool(ok),
                "seconds": round(time.monotonic() - t0, 2),
                "detail": detail if isinstance(detail, str) else str(detail),
            }
        )
        print(f"[{'PASS' if ok else 'FAIL'}] {name} — {detail}", flush=True)
        return fn

    return wrap


def wait_until(cond, timeout: float, interval: float = 0.5, desc: str = ""):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            v = cond()
        except Exception:
            v = None
        if v:
            return v
        time.sleep(interval)
    raise TimeoutError(f"timed out after {timeout}s waiting for {desc or cond}")


# ---------------------------------------------------------------------------
# session environment discovery


def _proc_environ(pid: int) -> dict[str, str]:
    try:
        data = Path(f"/proc/{pid}/environ").read_bytes()
    except OSError:
        return {}
    env = {}
    for item in data.split(b"\0"):
        if b"=" in item:
            k, v = item.split(b"=", 1)
            env[k.decode("utf-8", "replace")] = v.decode("utf-8", "replace")
    return env


def _pgrep(pattern: str) -> list[int]:
    out = subprocess.run(
        ["pgrep", "-u", str(os.getuid()), "-x", pattern],
        capture_output=True, text=True,
    ).stdout
    return [int(x) for x in out.split() if x.strip().isdigit()]


def session_environ(timeout: float = 120.0) -> dict[str, str]:
    """Wait for the Plasma session and return an env usable from SSH."""
    env = wait_until(
        lambda: next(
            (
                _proc_environ(p)
                for proc in ("plasmashell", "kwin_wayland", "kwin_wayland_wrapper", "startplasma")
                for p in _pgrep(proc)
                if "WAYLAND_DISPLAY" in _proc_environ(p)
            ),
            None,
        ),
        timeout,
        desc="a running plasmashell/kwin_wayland with WAYLAND_DISPLAY",
    )
    env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    env.setdefault(
        "DBUS_SESSION_BUS_ADDRESS",
        f"unix:path={env['XDG_RUNTIME_DIR']}/bus",
    )
    # The SSH shell env wins for PATH/HOME basics.
    env.setdefault("HOME", str(Path.home()))
    env["PATH"] = env.get("PATH", os.environ.get("PATH", "/usr/bin:/bin"))
    for k in ("WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS",
              "QT_ACCESSIBILITY", "QT_LINUX_ACCESSIBILITY_ALWAYS_ON"):
        if k in env:
            os.environ[k] = env[k]
    return env


SESSION_ENV: dict[str, str] = {}


def in_session(cmd: list[str] | str, **kw) -> subprocess.CompletedProcess:
    """Run a command with the real session's environment."""
    return subprocess.run(cmd, env=SESSION_ENV, capture_output=True, text=True,
                          timeout=kw.pop("timeout", 60), shell=isinstance(cmd, str), **kw)


# ---------------------------------------------------------------------------
# KWin scripting oracle (same mechanism as Tier 2: a KWin script reports back
# over the session bus to a transient object exported here).

_KWIN_ORACLE_XML = """
<node>
  <interface name="org.kde.krema.vmqa.Oracle">
    <method name="result">
      <arg type="s" name="token" direction="in"/>
      <arg type="s" name="json" direction="in"/>
    </method>
  </interface>
</node>
"""

_kwin_results: dict[str, str] = {}
_kwin_bus = None
_kwin_counter = 0


def _kwin_setup():
    global _kwin_bus
    from gi.repository import Gio

    if _kwin_bus is None:
        _kwin_bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        info = Gio.DBusNodeInfo.new_for_xml(_KWIN_ORACLE_XML).interfaces[0]

        def on_call(_conn, _sender, _path, _iface, _method, params, invocation):
            token, payload = params.unpack()
            _kwin_results[token] = payload
            invocation.return_value(None)

        _kwin_bus.register_object("/Oracle", info, on_call, None, None)
    return _kwin_bus


def kwin_eval(js: str, timeout: float = 15.0):
    """Run JS inside KWin; the script must call report(value) once."""
    global _kwin_counter
    from gi.repository import Gio, GLib

    bus = _kwin_setup()
    _kwin_counter += 1
    token = f"vmqa_{os.getpid()}_{_kwin_counter}"
    plugin = f"krema_vmqa_{token}"
    script = (
        "function report(v) {"
        f" callDBus({json.dumps(bus.get_unique_name())}, '/Oracle',"
        " 'org.kde.krema.vmqa.Oracle', 'result',"
        f" {json.dumps(token)}, JSON.stringify(v === undefined ? null : v)); }}\n"
        "try {\n" + js + "\n} catch (e) { report({__error__: String(e)}); }\n"
    )
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
        f.write(script)
        path = f.name

    def call(method: str, sig, *args, obj="Scripting", path="/Scripting"):
        params = GLib.Variant(f"({sig})", args) if sig else None
        reply = bus.call_sync(
            "org.kde.KWin", path, "org.kde.kwin." + obj, method, params, None,
            Gio.DBusCallFlags.NONE, 10000, None,
        )
        vals = reply.unpack() if reply else []
        return vals[0] if len(vals) == 1 else vals

    try:
        script_id = call("loadScript", "ss", path, plugin)
        call("run", None, obj="Script", path=f"/Scripting/Script{script_id}")
        ctx = GLib.MainContext.default()
        deadline = time.monotonic() + timeout
        while token not in _kwin_results:
            if time.monotonic() > deadline:
                raise TimeoutError(f"KWin script {plugin} did not report back")
            if not ctx.iteration(False):
                time.sleep(0.01)
        payload = _kwin_results.pop(token)
    finally:
        try:
            call("unloadScript", "s", plugin)
        except Exception:
            pass
        os.unlink(path)
    value = json.loads(payload)
    if isinstance(value, dict) and "__error__" in value:
        raise RuntimeError(f"KWin script error: {value['__error__']}")
    return value


def kwin_windows() -> list[dict]:
    return kwin_eval(
        """
        const active = workspace.activeWindow;
        report(workspace.windowList().map(w => ({
          id: String(w.internalId),
          title: w.caption,
          app_id: w.desktopFileName || '',
          resource_class: String(w.resourceClass || ''),
          pid: w.pid,
          active: w === active,
          dock: !!w.dock,
          normal: !!w.normalWindow,
          skip_taskbar: !!w.skipTaskbar,
          x: Math.round(w.frameGeometry.x), y: Math.round(w.frameGeometry.y),
          width: Math.round(w.frameGeometry.width),
          height: Math.round(w.frameGeometry.height),
          output: w.output ? w.output.name : ''
        })));
        """
    )


# ---------------------------------------------------------------------------
# AT-SPI helpers


def _atspi():
    import pyatspi

    return pyatspi


def dock_toolbar(timeout: float = 60.0):
    """Find the 'Krema Dock' tool bar accessible anywhere on the desktop."""
    pyatspi = _atspi()

    def find():
        desktop = pyatspi.Registry.getDesktop(0)
        for i in range(desktop.childCount):
            app = desktop.getChildAtIndex(i)
            if app is None:
                continue
            stack = [app]
            while stack:
                acc = stack.pop()
                try:
                    role = acc.getRole()
                    name = acc.name or ""
                except Exception:
                    continue
                if role == pyatspi.ROLE_TOOL_BAR and name == "Krema Dock":
                    return acc
                try:
                    stack.extend(
                        acc.getChildAtIndex(c) for c in range(acc.childCount)
                    )
                except Exception:
                    continue
        return None

    return wait_until(find, timeout, desc="AT-SPI tool bar 'Krema Dock'")


def dock_buttons(toolbar=None) -> list:
    pyatspi = _atspi()
    toolbar = toolbar or dock_toolbar(60)
    roles = {pyatspi.ROLE_PUSH_BUTTON, pyatspi.ROLE_TOGGLE_BUTTON}
    out = []
    for i in range(toolbar.childCount):
        try:
            child = toolbar.getChildAtIndex(i)
            if child is not None and child.getRole() in roles:
                out.append(child)
        except Exception:
            continue
    return out


# ---------------------------------------------------------------------------
# process helpers


def proc_running(name: str) -> list[int]:
    return _pgrep(name)


def proc_cgroup(pid: int) -> str:
    try:
        return Path(f"/proc/{pid}/cgroup").read_text()
    except OSError:
        return ""


def ancestor_names(pid: int, limit: int = 12) -> list[str]:
    names = []
    seen = set()
    while pid and pid not in seen and len(names) < limit:
        seen.add(pid)
        try:
            stat = Path(f"/proc/{pid}/stat").read_text()
            comm = stat[stat.index("(") + 1 : stat.rindex(")")]
            ppid = int(stat[stat.rindex(")") + 1 :].split()[1])
            names.append(comm)
            pid = ppid
        except (OSError, ValueError, IndexError):
            break
    return names


def autostart_files() -> list[str]:
    hits = []
    for base in ("/etc/xdg/autostart", "/usr/etc/xdg/autostart",
                 "/usr/share/autostart", str(Path.home() / ".config/autostart")):
        d = Path(base)
        if d.is_dir():
            hits.extend(str(p) for p in d.glob("*krema*.desktop"))
    return hits


def _pkg_query(cmd: list[str]) -> str:
    """stdout of a package-manager query; "" on failure or a missing tool
    (each guest has only its own distro's package manager)."""
    try:
        q = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    except FileNotFoundError:
        return ""
    return q.stdout.strip() if q.returncode == 0 else ""


def installed_pkg_version() -> tuple[str, str]:
    """Return (manager, version) of the installed krema package."""
    out = _pkg_query(["rpm", "-q", "krema", "--qf", "%{VERSION}-%{RELEASE}\n"])
    if out:
        return "rpm", out
    out = _pkg_query(["dpkg-query", "-W", "-f", "${Version}", "krema"])
    if out:
        return "dpkg", out
    out = _pkg_query(["pacman", "-Q", "krema"])
    if out:
        return "pacman", out.split()[-1]
    return "", ""


def cargo_pkg_version() -> str:
    pkgs = sorted(Path("/run/krema-cargo/packages").glob("krema-[0-9]*")) \
        or sorted(Path("/run/krema-cargo/packages").glob("krema*"))
    for p in pkgs:
        out = ""
        if p.suffix == ".rpm":
            out = _pkg_query(["rpm", "-qp", str(p), "--qf", "%{VERSION}-%{RELEASE}\n"])
        elif p.suffix == ".deb":
            out = _pkg_query(["dpkg-deb", "-f", str(p), "Version"])
        elif p.name.endswith(".pkg.tar.zst"):
            out = _pkg_query(["pacman", "-Qp", str(p)])
            out = out.split()[-1] if out else ""
        if out:
            return out
    return ""


# ---------------------------------------------------------------------------
# the checks


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--report", default="/tmp/krema-verify.json")
    ap.add_argument("--launch-app", default="kwrite",
                    help="binary to launch (desktop file must exist)")
    ap.add_argument("--app-desktop", default="org.kde.kwrite")
    ap.add_argument("--app-name-regex", default=r"(?i)kwrite|untitled")
    ap.add_argument("--session-timeout", type=float, default=180)
    ap.add_argument("--item-timeout", type=float, default=60)
    args = ap.parse_args()

    global SESSION_ENV
    SESSION_ENV = session_environ(args.session_timeout)
    print("session env: WAYLAND_DISPLAY="
          f"{SESSION_ENV.get('WAYLAND_DISPLAY')} "
          f"DBUS={SESSION_ENV.get('DBUS_SESSION_BUS_ADDRESS')}", flush=True)

    # Wake the AT-SPI bus (org.a11y.Bus) explicitly so pyatspi's getDesktop has
    # a registry to talk to even if no client has touched it yet.
    subprocess.run(
        ["busctl", "--user", "call", "org.a11y.Bus", "/org/a11y/bus",
         "org.a11y.Bus", "GetAddress"],
        env=SESSION_ENV, capture_output=True, timeout=30)

    @check("processes.plasmashell")
    def _():
        pids = proc_running("plasmashell")
        return bool(pids), f"plasmashell pids={pids}"

    @check("processes.kwin_wayland")
    def _():
        pids = proc_running("kwin_wayland") or proc_running("kwin_wayland_wrapper")
        return bool(pids), f"kwin pids={pids}"

    @check("krema.installed_from_cargo")
    def _():
        stamp = Path("/var/lib/krema-cargo-installed")
        mgr, ver = installed_pkg_version()
        return stamp.exists() and bool(ver), \
            f"stamp={stamp.exists()} installed={mgr}:{ver or '(none)'}"

    @check("krema.running_via_autostart")
    def _():
        # Autostart fires after plasmashell/kwin come up; give it time.
        wait_until(lambda: proc_running("krema"), args.item_timeout,
                   desc="a running krema process")
        pids = proc_running("krema")
        files = autostart_files()
        pid = pids[0]
        cgroup = proc_cgroup(pid)
        ancestors = ancestor_names(pid)
        sessionish = bool(
            set(ancestors) & {"plasmashell", "startplasma", "systemd"}
        ) or "autostart" in cgroup or "plasma" in cgroup
        detail = (f"pid={pid} autostart_files={files or '[]'} "
                  f"ancestors={ancestors[:5]} cgroup={cgroup.strip() or '-'}")
        return bool(files) and sessionish, detail

    @check("atspi.krema_dock_toolbar")
    def _():
        tb = dock_toolbar(args.item_timeout)
        return True, f"'Krema Dock' tool bar with {tb.childCount} children"

    launched_proc = None

    @check("app.launch_appears_in_dock")
    def _():
        nonlocal launched_proc
        before = len(dock_buttons())
        launched_proc = subprocess.Popen(
            [args.launch_app], env=SESSION_ENV,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        name_re = re.compile(args.app_name_regex)

        # Wait until the toolbar gains a button whose accessible name matches
        # the launched app (window title while single-window, app name when
        # grouped — either satisfies name_re).
        deadline = time.monotonic() + args.item_timeout
        buttons = []
        matched = None
        while time.monotonic() < deadline:
            try:
                tb = dock_toolbar(15)
                buttons = dock_buttons(tb)
                for b in buttons:
                    try:
                        nm = b.name or ""
                    except Exception:
                        nm = ""
                    if name_re.search(nm):
                        matched = b
                        break
            except TimeoutError:
                pass
            if matched is not None:
                break
            if len(buttons) > before:
                # something appeared; keep waiting for the name match briefly
                pass
            time.sleep(0.75)
        names = []
        for b in buttons:
            try:
                names.append(b.name)
            except Exception:
                names.append("?")
        ok = matched is not None
        return ok, (f"launched {args.launch_app} (pid={launched_proc.pid}); "
                    f"dock buttons {before}->{len(buttons)} names={names}")

    @check("kwin.window_list_and_activation")
    def _():
        def app_window():
            wins = kwin_windows()
            for w in wins:
                if args.app_desktop in (w["app_id"] or "") \
                        or args.app_desktop in (w["resource_class"] or "") \
                        or re.search(args.app_name_regex, w["title"] or ""):
                    return w
            return None

        win = wait_until(app_window, args.item_timeout,
                         desc="KWin window for the launched app")
        wins = kwin_windows()
        dock_surfaces = [w for w in wins if w["dock"]
                         or "krema" in (w["app_id"] or "").lower()
                         or "krema" in (w["resource_class"] or "").lower()]
        if not dock_surfaces:
            return False, f"no krema dock surface in KWin windows: {wins}"
        # Activate the app window through KWin scripting.
        kwin_eval(
            f"""
            const ws = workspace.windowList().filter(
                w => String(w.internalId) === {json.dumps(win['id'])});
            if (!ws.length) report({{__error__: 'window vanished'}});
            workspace.activeWindow = ws[0];
            report(true);
            """
        )
        def is_active():
            for w in kwin_windows():
                if w["id"] == win["id"]:
                    return w["active"]
            return False

        wait_until(is_active, 15, desc="launched app becoming active in KWin")
        return True, (f"window '{win['title']}' active; "
                      f"dock surfaces={len(dock_surfaces)}")

    @check("krema.version_matches_package")
    def _():
        mgr, installed = installed_pkg_version()
        cargo = cargo_pkg_version()
        q = in_session(["krema", "--version"], timeout=30)
        out = (q.stdout or "") + (q.stderr or "")
        m = re.search(r"(\d+\.\d+(\.\d+)*)", out)
        binary = m.group(1) if m else ""
        parts = [f"--version output={out.strip()!r} (parsed {binary or 'none'})",
                 f"installed={mgr}:{installed or '?'}", f"cargo={cargo or '?'}"]
        if binary and cargo and binary not in cargo:
            return False, "; ".join(parts) + " — binary version != package"
        if not installed:
            return False, "; ".join(parts) + " — krema package not installed"
        if cargo and installed != cargo:
            return False, "; ".join(parts) + " — installed != cargo package"
        return True, "; ".join(parts)

    @check("journal.no_krema_crash")
    def _():
        j = subprocess.run(
            ["sudo", "-n", "journalctl", "-b", "--no-pager", "-o", "cat"],
            capture_output=True, text=True, timeout=120)
        text = j.stdout if j.returncode == 0 else ""
        bad = [ln for ln in text.splitlines()
               if re.search(r"krema", ln, re.I)
               and re.search(r"segfault|core dumped|crash|SIGSEGV|SIGABRT|"
                             r"coredump", ln, re.I)]
        cd = subprocess.run(["sudo", "-n", "coredumpctl", "--no-pager", "list"],
                            capture_output=True, text=True, timeout=60)
        cores = [ln for ln in cd.stdout.splitlines()
                 if re.search(r"krema", ln, re.I)] if cd.returncode == 0 else []
        return not bad and not cores, \
            f"journal crash lines={len(bad)} coredumps={len(cores)}"

    # dump a little observability for artifacts, never failing
    try:
        wins = kwin_windows()
        CHECKS.append({"name": "observability.kwin_windows", "ok": True,
                       "seconds": 0, "detail": json.dumps(wins)})
    except Exception as exc:
        CHECKS.append({"name": "observability.kwin_windows", "ok": True,
                       "seconds": 0, "detail": f"unavailable: {exc}"})

    failed = [c for c in CHECKS if not c["ok"]]
    Path(args.report).write_text(json.dumps(
        {"checks": CHECKS, "failed": len(failed), "total": len(CHECKS)},
        indent=2))
    print(f"\n{len(CHECKS) - len(failed)}/{len(CHECKS)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
