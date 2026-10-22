# Media controls and per-app audio state

## MPRIS player controls

`MediaController` subscribes to the session bus's `NameOwnerChanged` signal
before enumerating existing names, then follows registrations and removals
matching `org.mpris.MediaPlayer2.*`. This also discovers media apps started
after Krema during login. For each service it reads the standard
`org.mpris.MediaPlayer2` and `org.mpris.MediaPlayer2.Player` properties and
subscribes to `PropertiesChanged`. A player is associated with a dock task by
its TaskManager `AppId` and MPRIS `DesktopEntry`; the application portion of the
MPRIS service name and `Identity` are fallback identifiers. The task process ID
is an additional fallback, matching the service owner's process. Matching
handles reverse-DNS desktop IDs, generated Snap desktop IDs, and dynamic MPRIS
instance suffixes. When a matching player allows control, its metadata and available
10-second rewind, PlayPause, and 10-second forward actions appear below the
window thumbnail, including while stopped if the player can be started.
Previous and Next track actions are shown when supported. Seeking uses MPRIS
`Seek` with a ±10,000,000 microsecond offset and appears only when `CanSeek` is
true.

Transport controls require the application to publish MPRIS state and actions.
Apps that only output audio still get the speaker badge and mute action below;
audio output alone does not provide play, pause, or seek controls. A browser may
expose media from a tab only when its own MPRIS integration is enabled.

## Audio mute badge

When `pactl` is installed, `MediaController` starts `pactl subscribe` and
refreshes the JSON sink-input list when audio streams change. The dock shows a
speaker badge for an app while it has a non-corked audio stream. Clicking the
badge uses `pactl set-sink-input-mute` on that app's stream; the icon reflects
the stream's reported mute state. On PipeWire desktops this uses PipeWire's
PulseAudio compatibility server. If `pactl` is unavailable, the badge is
omitted.

The main Krema package does not require `pactl`; per-app mute support is
optional. On PipeWire systems, the PulseAudio compatibility server must be
running as well as the `pactl` client.

Stream attribution prefers exact app IDs or executable names, with aliases for
reverse-DNS and generated Snap desktop IDs. It also follows a bounded process
ancestry chain from an audio stream to its task, which associates browser
renderer streams and other child processes with their dock item. TaskManager's
`AppPid` can be KWin's PID on some Wayland setups, so it is only used to
corroborate a weaker display-name match.

## Preview frames

The thumbnail remains a capture of the application's window. MPRIS transport
controls can operate the player, but they do not decode a separate video into
Krema's preview. If an application stops rendering its minimized window, KWin
may provide no new capture frames; Krema then keeps the last captured frame.
