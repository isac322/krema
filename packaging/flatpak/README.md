# Local Flatpak package

This directory contains an **AI-authored local Flatpak manifest**, not a Flathub submission. It packages the unchanged Krema v0.9.0 application code with separately identified packaging metadata companions. A separate verification worker successfully built the final metadata-corrected package, composed AppStream metadata, exported the app-ID icon, desktop entry and metainfo, published the result to the local verification repository, and installed the `aarch64` stable application at commit `3dbe354f7b70e58032fd74333fd3d8a65b223fcb13379b9bf9a9135346827472`. The installed final AppStream metadata passed pedantic validation, including both background-style corrections and all three screenshots. All three serial official lint commands still failed as detailed below. The completed runtime smoke has passing dock/zoom and window activation/restore results, a failed native host launcher, and environment-blocked preview frames. No full-functionality, tested distributable bundle, `x86_64`, or Flathub-eligibility claim is made.

Flathub's [generative AI policy](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy) currently excludes AI-generated or AI-assisted manifest content and prohibits automated agent submission PRs. Disclosure does not make this manifest eligible. The broader requirements allow discretionary exceptions, but no exception has been obtained. Local building and distribution outside Flathub are separate from Flathub acceptance.

## Sources and dependency boundaries

The application source is `https://github.com/isac322/krema/archive/v0.9.0.tar.gz`, release commit `7599715af204b8658ed79acc9ca2a118d5e6dc91`. Its downloaded bytes hash to `6dbb0de97339119371e6cb403b0d1e9dca525cdaaf3b78792865ee94c91fa6f3`.

The manifest uses `org.kde.Platform` and `org.kde.Sdk` branch `6.10`. Both `aarch64` and `x86_64` SDK refs were observed on the live Flathub remote. KDE's [qt6.10 SDK recipe](https://invent.kde.org/packaging/flatpak-kde-runtime/-/blob/qt6.10/org.kde.Sdk.json.in) currently specifies the Freedesktop 25.08 base, Qt 6.10.3, Frameworks 6.29.0, PlasmaActivities 6.7.4, KWayland 6.7.4, PlasmaWaylandProtocols 1.21.0, Canberra 0.30, and Kirigami Addons 1.13.1. This is recipe evidence, not proof of the installed SDK's contents. Inspect the actual SDK before building: the bundled Plasma 6.7.5 sources require Qt >= 6.10, Frameworks >= 6.26, Wayland >= 1.24, and wayland-protocols >= 1.46.

The Freedesktop SDK's current [Wayland recipe](https://gitlab.com/freedesktop-sdk/freedesktop-sdk/-/blob/release/25.08/elements/components/wayland.bst) pins 1.24.0; its [wayland-protocols recipe](https://gitlab.com/freedesktop-sdk/freedesktop-sdk/-/blob/release/25.08/elements/components/wayland-protocols.bst) pins 1.48. These source-recipe versions satisfy the bundled libraries' requirements without duplicate Wayland modules.

A separate verification worker queried the installed `aarch64` KDE SDK 6.10 inside its real Flatpak sandbox: `wayland-client` 1.24.0, `wayland-protocols` 1.48, `Qt6Core` 6.10.3, and `libcanberra` 0.30; the query exited successfully. These installed values confirm the relevant recipe versions. This SDK probe is not an application build or runtime smoke test, and it does not verify the `x86_64` package.

All manifest archive hashes were computed from bytes downloaded from the listed public URLs, including every bundled KDE dependency. Modules are ordered as follows:

1. `libplasma` supplies `Plasma::Plasma` for the workspace libraries. Its upstream default X11 build support is retained: the first application build attempt exposed an upstream 6.7.5 compile failure with `WITHOUT_X11=ON`, because `BlurEffectWatcher` always inherits `QAbstractNativeEventFilter` but only implements its pure virtual method when X11 is enabled. Compiling that dependency's X11 support does not grant an X11 socket or change Krema's Wayland-only platform.
2. `libkscreen` supplies `KF6::Screen` for the notification manager.
3. `layer-shell-qt` supplies the layer-shell integration plugin and C++ interface.
4. `kpipewire` supplies its required CMake package and QML PipeWire renderer.
5. `plasma-workspace-libraries` builds upstream `libkworkspace`, `libtaskmanager`, and `libnotificationmanager`, in that order.
6. `krema` builds the unchanged v0.9.0 application code with an AppStream metadata patch that adds screenshots and corrects the current background-style description and its inaccurate 0.4.0 release note, plus separately pinned icon metadata; its required build dependencies remain intact.

`plasma-workspace-libraries/CMakeLists.txt` is packaging-only build glue. It includes the original upstream library CMake files and retains their generated bindings, headers, package exports, QML plugins, and notification configuration. It does not build an entire Plasma session inside the application. A checksummed `kscreenlocker` source archive supplies the original ScreenSaver D-Bus XML used by `libkworkspace`; the screen locker itself is not built. X11-only workspace models are excluded because Krema only supports Wayland. Upstream's optional libflatpak notification-resource support remains optional, as upstream defines it; none of Krema's required dependencies is disabled.

Build cleanup removes development headers, package metadata, archives, and documentation only after all modules have built. Runtime libraries, QML modules, layer-shell plugins, icons, translations, desktop metadata, and notification resources remain installed. The release's AppStream ID and desktop filename already use `com.bhyoo.krema`, but its desktop icon name is `krema` and the archive contains no image assets or icon install rule. The install prefix is `/app`, and the command is `krema`.

The manifest supplies the project's official SVG as **post-release packaging metadata**, downloaded from immutable commit `84184ca07e226a96ef468d33d4a3afc16a1cc533` at [branding/logo/krema-icon.svg](https://raw.githubusercontent.com/isac322/krema/84184ca07e226a96ef468d33d4a3afc16a1cc533/branding/logo/krema-icon.svg). The downloaded 782 bytes hash to `65f419d4d6ee5534a66ec141bf3810bf91ca3296db1ee5603164035735966782`. It is not part of v0.9.0. Packaging installs it into hicolor's scalable application-icon directory under the release's icon name; Flatpak Builder's top-level `rename-icon: krema` changes the installed icon and desktop reference to the app ID during cleanup. This icon companion changes packaging metadata, not application runtime code; it does not disable composition, change the app ID, or invent a substitute icon.

## AppStream metadata companion

`metainfo.patch` adds a screenshots section and corrects two proven inaccurate background-style statements: the current description and the 0.4.0 release note. Both claim six styles including Mica and adaptive opacity, while the tagged implementation and UI provide **Panel Inherit, Transparent, Tinted, and Acrylic**; the `BackgroundStyle` configuration range is 0–3. The patch preserves the rest of the description, including v0.9.0's `Meta+F5` keyboard shortcut, and leaves the application ID, licenses, URLs, developer, categories, and all other release notes intact. It does not generalize that a feature never existed, import future feature descriptions, or modify C++/QML/runtime logic.

The prepared listing screenshots are post-release media from immutable public commit `b2ac9249f0f3eb7467e24582ca27f624cd5ec0c1`. Their LFS-aware URLs serve actual PNG bytes, not Git LFS pointer files. Each downloaded image is 1920 × 1080, and each hash exactly matches the corresponding prepared KDE Store upload-kit file.

| Screenshot source | SHA256 |
| --- | --- |
| [dock-zoom.png](https://media.githubusercontent.com/media/isac322/krema/b2ac9249f0f3eb7467e24582ca27f624cd5ec0c1/branding/screenshots/dock-zoom.png) | `2988184361a1884c328b8b588a02adabfea43bed6fd8bc9fc152280ec2eb7150` |
| [dock-overview.png](https://media.githubusercontent.com/media/isac322/krema/b2ac9249f0f3eb7467e24582ca27f624cd5ec0c1/branding/screenshots/dock-overview.png) | `2f65410b274c8135f88a14d32951828e0cb6c9ce1e52787e41a88c1dd928be5a` |
| [settings.png](https://media.githubusercontent.com/media/isac322/krema/b2ac9249f0f3eb7467e24582ca27f624cd5ec0c1/branding/screenshots/settings.png) | `7a7f8e8d5bfb181f73c3632285ad470341249a58a7ec01d68d32e94974931302` |

AppStream captions use the approved listing descriptions; the long settings caption is shortened to stay below the recommended 100 characters. The schema provides captions, not an image `alt` attribute. The prepared packet's full alternative text remains:

- **Dock zoom:** Krema's parabolic zoom enlarges the launcher under the pointer and moves neighboring dock icons aside on KDE Plasma.
- **Dock overview:** Krema dock at the bottom of a KDE Plasma desktop with pinned launchers, running application indicators, Dolphin, and Konsole.
- **Settings:** Krema's Kirigami Appearance settings show controls for icon size and spacing, zoom factor and style, icon normalization and scale, and attention animation.

Builder's documented `--mirror-screenshots-url=URL` option downloads media, rewrites catalogue URLs to a mirror base, and exports a screenshot OSTree ref in Builder 1.4.5 and later. It is not enabled here: no verified public hosting base for those generated mirror paths has been provided. The metadata uses the actual immutable source URLs instead. Do not insert an invented public mirror or a verification-only localhost endpoint into distributable metadata.

## Official lint status

On the final metadata-corrected package, all three serial official lint commands—manifest, build-directory, and repository—exited 1. There are no missing-screenshot, syntax, or identity errors: all three screenshots are retained, and installed metainfo passed `appstreamcli validate --pedantic`. Build-directory and repository lint still report `appstream-external-screenshot-url`, and the repository's screenshot media is not mirrored. The package nevertheless built, composed, exported, and installed successfully; strict composition and pedantic metadata validation are distinct from hosted-store lint acceptance. The factual background-description corrections do not waive or hide any policy findings.

Nine existing policy/domain findings remain for the Wayland-only permission set, read-only host OS/application/icon/Flatpak-export access, KWin D-Bus access, Unity bus ownership, and the app-ID root-domain URL. No exceptions have been granted and no lint exemptions were added. The permissions correspond to existing dock integrations and are retained, not removed just to satisfy a hosted-store check. The root `bhyoo.com` URL failed DNS while `krema.bhyoo.com` was reachable; this package does not change DNS or migrate the app ID. Runtime 6.11 availability is a warning, not evidence that the verified 6.10 branch must be replaced.

## Local build and bundle commands

Run these on Linux with Flatpak, Flatpak Builder, a configured `flathub` remote, user-namespace support, and enough scratch storage. Commands below deliberately keep build state and the repository outside the source checkout. Installing build dependencies changes the selected Flatpak installation; use an isolated container or disposable user installation for verification.

```sh
checkout="$PWD"
work="$(mktemp -d /tmp/krema-flatpak.XXXXXX)"
flatpak-builder --user --install-deps-from=flathub \
  --state-dir="$work/state" --repo="$work/repo" \
  "$work/build" "$checkout/packaging/flatpak/com.bhyoo.krema.yml"
```

If the official Builder Flatpak is used instead of a native `flatpak-builder`, replace the program in that command with `flatpak run --command=flatpak-builder org.flatpak.Builder`. Check that the Builder can access the checkout and scratch paths. Do not add `--force-clean` to an existing unrelated directory.

After a successful build, create a candidate bundle and install it for the runtime checks below. Do not distribute it until those checks pass:

```sh
flatpak build-bundle --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
  "$work/repo" "$work/krema-0.9.0.flatpak" com.bhyoo.krema stable
flatpak install --user "$work/krema-0.9.0.flatpak"
flatpak run com.bhyoo.krema
```

These commands describe the verification path; they are not evidence that a bundle has been produced. The bundle architecture is the builder's native architecture unless an explicit supported `--arch` is selected. Runtime access to Flathub is not publication of the application to Flathub.

## Permission rationale

The manifest does not grant X11, network access, the whole session bus, the system bus, unrestricted devices, or read/write host filesystem access.

| Finish argument | Existing integration |
| --- | --- |
| `--socket=wayland` | `WaylandDockPlatform`, LayerShellQt, output ordering, window task models, and KWin screencast protocols. |
| `--device=dri` | Qt Quick accelerated rendering and the KPipeWire thumbnail renderer. |
| `--filesystem=xdg-run/pipewire-0` | `PreviewThumbnail.qml` connects to host PipeWire using the node ID from `TaskManager.ScreencastingRequest`; this is not portal-based capture. |
| `--filesystem=host-os:ro` | Read-only host OS data, including installed application desktop files and icons under `/run/host/usr/share`, for KService/task-manager launchers. It does not allow executing arbitrary host binaries inside the sandbox. |
| `--filesystem=xdg-data/applications:ro` and `xdg-data/icons:ro` | User-installed desktop files and icons. Mount permission alone does not prove discovery; the private `XDG_DATA_HOME` needs runtime verification. |
| `--filesystem=xdg-data/flatpak/exports/share:ro` and `/var/lib/flatpak/exports/share:ro` | User and system Flatpak desktop/icon exports. User export discovery remains a runtime check. |
| `--talk-name=org.kde.KWin` | `KWinPointerMotionWatcher` loads, runs, and unloads its pointer-motion script over D-Bus. Host visibility of the script path is a separate compatibility check. |
| `--talk-name=org.kde.kglobalaccel` | `Application::run()` registers the existing global shortcuts. |
| `--talk-name=org.freedesktop.ScreenSaver` | `DockView` receives screen-lock changes. |
| `--talk-name=org.freedesktop.Notifications` | `NotificationTracker` registers its notification watcher. |
| `--talk-name=org.kde.StatusNotifierWatcher` | `NotificationTracker` queries and follows registered status-notifier items. This does not grant arbitrary item service names. |
| `--own-name=com.canonical.Unity` | `LauncherEntryTracker` exports its existing Unity launcher endpoint. The app's own `com.bhyoo.krema` prefix is already allowed by Flatpak. |
| `QT_QPA_PLATFORM=wayland` | Keeps this Wayland-only application on its supported Qt platform. |
| `XDG_DATA_DIRS=...` | Retains application/runtime resources and adds read-only host/system Flatpak data locations for desktop entry and icon lookup. |

The installed KDE runtime also contributes inherited grants: read-only `kdeglobals` access and D-Bus access to `org.kde.KGlobalSettings`, `com.canonical.AppMenu.Registrar`, and `org.kde.kconfig.notify`; the table above lists this manifest's additions, not the entire effective permission set.

## Observed installed runtime smoke

The verification worker exercised the installed application against a real, separate host Plasma Wayland session. The metadata-only changes do not alter the tested application code.

| Path | Result | Observed evidence |
| --- | --- | --- |
| Dock surface and hover zoom | **PASS** | Real bottom-edge dock pixels and hover magnification were observed. |
| Application rendering | **PASS** | Qt Quick used QRhi OpenGL 4.6 through Mesa llvmpipe. |
| Native host System Settings launch from dock | **FAIL** | KIO could not find `systemsettings` in the sandbox. The ordinary host launch control worked, so this is not a missing host application. |
| Host-window activation and restore | **PASS** | Windows from a distinct host namespace were activated and restored; the restored window reported `minimized=false` and `active=true`. |
| Minimize through the dock | **NOT AVAILABLE** | Genuine v0.9.0 does not expose this action; it is neither a passing result nor a new regression. |
| Live preview frames | **BLOCKED_ENV** | Hover displayed the real popup title and fallback icon, but no frames arrived and KWin reported `UnsupportedCompositingType`. Host KWin used QPainter; Xvfb lacked DRI3 and the host provided no DRM/GBM device. Real PipeWire and WirePlumber were running. |

The current OrbStack test environment has no supported flag-only fix for that compositor/capture limitation. Preview frames still require verification on a capture-capable KWin environment. The confirmed host-launch failure remains unresolved; this package does not substitute bundled host applications, spoof a GPU device, or weaken its integrations to claim a successful smoke run.

## Required runtime checks and limits

Released KWin 6.5.5, 6.6.5, and 6.7.5 were inspected: their `KWinDisplay::fetchRequestedInterfaces()` resolves a Wayland security-context app ID through the host desktop entry, and `allowInterface()` checks its declared interfaces. **Install the Flatpak before testing** so the host can discover its exported desktop file; an uninstalled `flatpak-builder --run` session is not an equivalent authorization test. Older host versions were not validated here. An independent source review found a stricter sandbox filter on unreleased KWin master; compatibility with a future release must be assessed against that release, not inferred from today's stable authorization path.

A successful compiler run is not enough. The smoke outcomes above do not establish every integration; verify remaining supported paths on an appropriate Plasma Wayland session before publishing a functional local bundle:

- The exported desktop entry remains `com.bhyoo.krema.desktop` with its `X-KDE-Wayland-Interfaces` declaration, and KWin authorizes window management, activation feedback, and screencasting for the installed Flatpak identity.
- The dock enumerates real host windows. Activation and restore passed; other exposed window actions require their own evidence. Minimization through the dock is not a v0.9.0 feature and is not claimed.
- A pinned native host application and a pinned Flatpak application launch successfully. Read-only desktop-file access does not by itself make their `Exec` commands runnable across the sandbox. The inspected KDE KIO 6.29.0 application/command launcher and process-runner sources contain no Flatpak host-spawn integration; granting `org.freedesktop.Flatpak` without a caller would not fix that path, so it is not granted here.
- Live thumbnails receive PipeWire frames; there is no promise of a portal fallback.
- Auto-hide/dodge pointer tracking can load its script into host KWin. A sandbox-private temporary path is not automatically visible to KWin.
- Global shortcuts, screen locking, notification watcher callbacks, launcher badges/progress, and status-notifier changes work through the filtered D-Bus proxy. Arbitrary status-notifier service names and launcher signal senders are not covered by narrow fixed service grants; this manifest does not conceal that limitation by opening the entire session bus.
- User-installed application and icon paths are actually discovered, not merely mounted.
- Autostart is checked separately: installing the original autostart entry under `/app/etc/xdg/autostart` does not register host-session autostart. No host autostart file is installed by this manifest.

If an unchanged application path cannot work through these permissions, report the exact failed integration and seek explicit approval for the necessary application-code scope. Do not make an integration optional, inject fallback credentials, replace required libraries with stubs, or claim success based on a window appearing.
