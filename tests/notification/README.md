# Notification QA app

This directory describes the wire-level notification QA app in
`tools/notification-debugger.py`. It exercises the session protocols over
D-Bus; it does not call Krema internals. The scenario catalog in
[`scenarios.json`](scenarios.json) is the machine-readable source for manual
and automation runs.

## Scope

The app has these tabs and protocol paths:

| Tab | Wire path | What the run proves |
|---|---|---|
| Notifications | `org.freedesktop.Notifications` and `org.kde.NotificationManager` | `Notify`, `CloseNotification`, `RegisterWatcher`, `NotificationClosed`, `ActionInvoked`, and `NotificationReplied` |
| Portal | `org.freedesktop.portal.Notification` | `AddNotification`, `RemoveNotification`, and portal `ActionInvoked` when the portal emits it |
| SmartLauncher | `com.canonical.Unity.LauncherEntry.Update` | progress, count, urgent, and zero-state signals |
| SNI | `org.kde.StatusNotifierWatcher` / `org.kde.StatusNotifierItem` | registration, status, icon, attention icon, overlay icon, and tooltip changes |
| Jobs | `org.kde.kuiserver` / `org.kde.JobViewV2` | `requestView`, job updates, request signals, and termination |
| Inbox | fd.o notifications plus Unity/SNI updates | a local inbox simulation and its protocol side effects |
| DND | fd.o `Inhibit`, `UnInhibit`, and `Inhibited` | DND cookies and the observed session property |

The app's QA status panel exposes sent and observed state separately. It also
shows limitations instead of turning an unavailable observation into a pass.
In particular:

- Portal forwarding into Plasma's fd.o notification server is optional. A
  successful `AddNotification` does not prove forwarding or rendering.
- Jobs are sent to `kuiserver`; Krema does not consume Jobs as notifications.
- Inbox unread/read state belongs to this QA app's simulation. It does not
  prove that Krema or an external inbox application read a message.
- Unity `Update` is a signal without a protocol acknowledgement. A visible
  badge or progress indicator must be observed separately.
- DND's `Inhibited` property describes the whole session. Other applications
  may keep it true after this app releases its own cookie.

## Prerequisites

Run the manual wire scenarios on a real Plasma 6 Wayland session with:

- a working user/session D-Bus;
- `python3`, PyQt6, `dbus-python`, and PyGObject;
- the Plasma notification server and StatusNotifierWatcher;
- `xdg-desktop-portal` if the Portal scenario is used;
- `org.kde.kuiserver` if the Jobs scenario is used;
- `selenium-webdriver-at-spi-screenshotter` and `ffmpeg` when container video
  capture is requested; and
- `wf-recorder` when host `run-qa.sh --capture` is requested.

The Appium lifecycle case uses `notify.py serve` as a test-owned notification
server and does not require Plasma's notification server.

ScreenShot2 capture requires KWin to have OpenGL/DRM capture support. In the
container, provide a DRM render node (for example, `vgem`); QPainter-only KWin
sessions cannot produce screenshots and therefore cannot produce a video.

Keep the QA app and the target dock visible on the same output during a manual
run. Use a fresh scenario correlation marker for every run. Do not place
passwords, tokens, private message bodies, or other secrets in fields that are
written to JSONL.

## Launching

The existing invocation remains valid:

```sh
python3 tools/notification-debugger.py
```

The repository entrypoint adds scenario selection, JSONL destination, and
optional Wayland video capture:

```sh
tests/notification/run-qa.sh \
  --scenario notifications-basic \
  --jsonl "$PWD/notification-basic.jsonl"

tests/notification/run-qa.sh \
  --scenario status-notifier-item \
  --jsonl "$PWD/sni.jsonl" \
  --capture "$PWD/sni.wcap"
```


For the Appium notification lifecycle case, the test launches a test-owned
server without relying on Plasma's notification daemon:

```sh
python3 tests/notification/notify.py serve \
  --ready-file /tmp/krema-notify.ready \
  --watcher-ready-file /tmp/krema-notify.watcher.ready
```

The server owns `org.freedesktop.Notifications`, accepts the standard
`Notify` and `CloseNotification` calls, accepts KDE
`RegisterWatcher`/`UnRegisterWatcher`, forwards notifications to Krema's
`/NotificationWatcher`, and emits `NotificationClosed`. The Appium test uses
ready files before sending stimuli; it terminates the process during cleanup.

`--scenario ID` is passed to the app and selects the scenario shown to the
operator. `--jsonl PATH` selects the event file. If omitted, the app uses its
own temporary default. The host `run-qa.sh` invocation remains unchanged; its
capture implementation is environment-specific. The isolated container
entrypoint, `container-session.sh --capture PATH`, uses KWin ScreenShot2 and
requires `selenium-webdriver-at-spi-screenshotter` plus `ffmpeg`.

Container capture writes the requested MP4 at `PATH`, a companion
`PATH`-stemmed `.frames.jsonl` file containing every captured frame and its
monotonic timestamp, and a retained `.ffconcat` manifest used for encoding.
PNG frames are temporary and removed after a successful encode; failed captures
retain their temporary frame directory and report it in
`screenshot-recorder.log`.

Capture is intentionally a hard requirement when requested: if the ScreenShot2
helper or `ffmpeg` is unavailable, the container wrapper exits instead of
silently producing a run without video. A QPainter-only KWin session likewise
cannot satisfy this capture path.

Additional arguments after `--` are passed to the QA app. For example:

```sh
tests/notification/run-qa.sh --scenario dnd -- --help
```

The wrapper does not create a private compositor or alter the user's Plasma
configuration. For isolated automated sessions, start the existing KWin
harness first and invoke the wrapper with that session's `WAYLAND_DISPLAY`,
`DBUS_SESSION_BUS_ADDRESS`, and `XDG_RUNTIME_DIR`.

## GUI automation contract

Automation should locate controls by stable Qt object names rather than tab
position or pixel coordinates. The required automation objects are:

- `scenarioState`: selected scenario and current QA state;
- `sentTable`: sent wire events;
- `observedTable`: callbacks and external observations;
- `limitationLabel`: explicit protocol/session limitations.

The app also exposes stable names for the window and tabs:

- window: `notificationDebuggerWindow`;
- shared target selector: `targetBar`;
- tabs: `notificationsTab`, `portalTab`, `smartLauncherTab`, `sniTab`,
  `jobsTab`, `inboxTab`, and `dndTab`; and
- reset control: `resetAllButton`.

The visible labels remain useful secondary anchors. The scenario entries list
the controls and observable assertions for each wire path. An automated
runner should:

1. start the app through `run-qa.sh` with a unique `--scenario` and JSONL path;
2. wait for the named tab/control to be visible and sensitive;
3. activate the protocol control;
4. wait for the corresponding `sent` JSONL record;
5. wait for an `observed` record only when that protocol has a real callback or
   external monitor; and
6. treat `limitationLabel` or an absent optional service as an explicit result,
   not as a synthetic success.

For deterministic cleanup, activate `resetAllButton` and wait for the reset
completion state before ending the run.

## JSONL event contract

Each event is one JSON object on one line. Records contain these fields:

| Field | Meaning |
|---|---|
| `ts` | ISO-8601 event timestamp |
| `dir` | `sent`, `observed`, or `reset` |
| `scenario` | selected scenario ID |
| `mechanism` | protocol mechanism, such as `freedesktop-notifications` |
| `corr` | correlation token shared by related calls and observations |
| `serverId` | server/object/cookie/portal ID when one exists; otherwise `null` |
| `reason` | close reason, limitation, reset phase, or explanatory reason |
| `payload` | safe, structured protocol details; never secrets |

Automation should not infer observation from a send record. For example,
`Notify` is `sent`, `RegisterWatcher`/`NotificationClosed` are `observed`,
and an unavailable `NotificationReplied` capability is a `reason` or
limitation rather than an invented callback.

The three directions have these meanings:

- `sent`: the QA app issued a real wire-level call or signal;
- `observed`: the app received a real callback, signal, watcher update, or
  externally visible state; and
- `reset`: the app attempted deterministic cleanup or recorded its result.

## Manual scenario procedure

Use the matching ID in `scenarios.json`. Every run follows this baseline:

1. Start `run-qa.sh` in the Plasma session and note the JSONL path shown in
   the QA status panel.
2. Verify the QA status panel reports the intended scenario and that the
   selected target desktop entry is correct.
3. Exercise the tab controls in the order listed by that scenario.
4. Confirm the sent event first, then confirm the independent observed event
   or record the displayed limitation.
5. Capture the GUI, the real Plasma/KWin result, and the JSONL tail at each
   acceptance point.
6. Press **Reset All** before closing the app.

The reset order is fixed:

1. close tracked fd.o numeric notification IDs;
2. remove tracked portal IDs;
3. send Unity zero/not-visible state;
4. set SNI `Active` and unregister the tracked item where supported;
5. terminate tracked Jobs where possible;
6. release DND cookies; and
7. clear local Inbox simulation state and refresh the status panel.

Each attempted phase emits a `reset` record with its correlation, server ID,
result, and reason. A missing optional service is recorded as unavailable; the
app does not fabricate an observation.

## Video capture

`container-session.sh --capture PATH` records the current KWin output with
ScreenShot2 while the QA app runs. It captures real PNG frames through
`selenium-webdriver-at-spi-screenshotter 0 0 0 0`, records monotonic timestamps,
and encodes a variable-frame-rate MP4 without audio. For a useful review, keep
the app and target dock visible, then capture this sequence:

1. the clean QA status panel;
2. the protocol send control and sent state;
3. the real Plasma/KWin notification, dock item, SNI item, job, or DND state;
4. the observed table/status change;
5. **Reset All** and the final clean state.

Review the MP4 together with the `.frames.jsonl` companion and the QA JSONL.
The video proves visible surfaces only; `.frames.jsonl` proves the recorder
observed live KWin frames and their timing; JSONL proves wire-level events.
Do not treat an unverified or empty artifact as a passing capture. ScreenShot2
requires OpenGL/DRM capture support; QPainter-only sessions fail honestly
instead of producing synthetic or mock frames.

## Krema notification product-input gates

These gates distinguish framework integration, real KWin capture, and
optional recorder-regression evidence for Krema's direct notification inputs.
They are separate from the wire-level Portal and JobView scenarios, which
are transport/coverage evidence only and are not Krema notification inputs.

| Gate | Tier | Test or evidence producer | Required observable oracle |
|---|---|---|---|
| Application-owned DND inhibition | Tier 2 (KWin integration) | `tests/integration/test_notification_tracker.cpp` — `DND application inhibition is active without an Until deadline and clears` | Real `Inhibit/UnInhibit` drives the private notification server; `NotificationManager::Settings::notificationsInhibitedByApplication()` reads true/false with no `Until`, and `NotificationTracker::dndActive` follows |
| Timed DND long-deadline arming and real deadline rewrite | Tier 2 (Qt/KWin integration) | `tests/integration/test_notification_tracker.cpp` — timed DND boundary, early timeout re-arm, and short-deadline rewrite coverage | The tracker safely arms a 30-day `Until` without waiting 30 days; an unchanged-clock early timeout recomputes the real deadline and remains active, then a real config rewrite to a short `Until` expires on wall-clock time |
| Notification service atomic owner handover | Tier 3 (real Appium/KWin video) | `tests/appium/record_notifications.py` — replacement notification server capture | A non-empty old and new `org.freedesktop.Notifications` owner transition is observed; the replacement server receives one watcher registration and a real notification reaches the dock after handover |
| Recorder acquisition and teardown failure containment | Tier 3 (opt-in real-capture regression) | `tests/appium/notification_cleanup_probe.py`, loaded explicitly with `-p`; see `tests/appium/README.md` | Real SNI setup rejection, missing-writer, malformed-snapshot, kded-wait timeout, original-scenario, and artifact-write failures must verify acquired-resource release, independent teardown, honest failed metadata, and exact original exception preservation. A failed snapshot read must leave no receiver that records later probe signals, whose delivery is independently confirmed. After a kded-wait timeout the recorder itself must KILL and reap its child (`kded-kill`/`kded-reap` stages on failure, source-reviewed only); probe-only cleanup after the saved verdict is recorded separately and never justifies a pass. Baseline-source comparisons stay outside the repository |
| SNI attention clears watcher unread state | Tier 2 (KWin/private D-Bus integration) | `tests/integration/test_notification_tracker.cpp` — `SNI registration, status transitions, re-registration and owner loss are observable` | Real SNI `NeedsAttention -> Active/Passive` clears only the matching canonical app's watcher unread count and changes revision; re-entering `NeedsAttention` keeps the boolean attention state without recreating a numeric badge |
| Context-menu Clear Notifications | Tier 2 (KWin/native QMenu integration) | `tests/integration/test_launcher_entry.cpp` — `Context menu clears only watcher unread notifications` | The real `QMenu` contains `Clear Notifications` iff the selected app has watcher unread; triggering it clears only that app's watcher count and leaves another app plus a visible Unity count unchanged |

The tests use the private notification/SNI peers and installed pinned
launchers from the integration harness. They do not assert Portal forwarding,
JobView synchronization, application-server read cursors, or
`ActionInvoked`/`NotificationReplied` as product inputs.

For the long-deadline old-fail/new-pass proof, run the Qt integration gate on
Qt 6.8. Newer Qt versions can saturate an oversized timer interval, so a
30-day DND state remaining active there alone does not prove the regression
is caught. No test waits 30 days or advances the clock; timer-arm inspection
and unchanged-clock early-timeout rearming supply the boundary proof.

The real capture uses `/build/bin/krema_notification_config_writer`, built
from `tests/notification/config-writer.cpp`. Its CLI is `--until <ISO8601
datetime>` or `--clear`; it uses the recorder's isolated `XDG_CONFIG_HOME` and
session bus. The helper writes a typed `QDateTime` through KConfig with
`KConfig::Notify`, syncs, then performs a blocking
`org.freedesktop.DBus.GetId` call on that same connection before exit. This
drains the framework's queued notification without manual `ConfigChanged`
signals, retries, or a fallback writer. Missing helpers and failed writes or
bus barriers fail the recording.
