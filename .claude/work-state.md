- **2026-08-13 (Session):** **Fixed Bug #41 (The "Ghost Hover"  Invisible Dock Paradox).** The Wayland Input Region (Mask) was successfully expanded to 64px in previous steps, but the QML hit-test `triggerDepth` remained hardcoded at 2px. This caused the C++ show timer to start instantly when the mouse hit the edge, but cancel immediately when the mouse bounced up slightly. Modified `main.qml` to dynamically expand `triggerDepth = 64` when `DockVisibility.hovered` is true, syncing the QML hit-test with the Wayland region and allowing the dock to appear.
- **2026-08-13 (Session A):** Diagnosed and fixed "The Invisible Empty Dock" (Bug #42). When running on Hyprland (where Wayland limits reading windows natively), the dock had zero icons. A 0-icon dock collapsed to 0 width. Enforced a minimum width of 100px so the empty panel is always visible.
- **2026-08-13 (Session A):** Diagnosed and fixed "The Ghost Hover" (Bug #41 Part 4). Even after the Q_PROPERTY fix, the dock appeared completely invisible. The root cause: once the dock became visible, QML switched to `currentOrbit` (interaction radius) for hover checking. The bottom screen edge was 34px away from the icon center, but the orbit radius was only 29px. Thus, the dock instantly rejected the hover and hid itself before the user could even see it. Modified `main.qml` to respect the `triggerDepth` edge bounds even when visible.
- **2026-08-13 (Session A):** Diagnosed and fixed the "Missing Q_PROPERTY" (Bug #41 Part 3). Wayland hover expansion was working, but `DockVisibilityController` failed to expose `hovered` to QML. As a result, `DockVisibility.hovered` evaluated to `undefined`, silently overriding the QML trigger depth to 2px and instantly cancelling the 200ms `m_showTimer` on the slightest mouse twitch. Exposed the property, restoring the QML 64px hysteresis bridge. The dock now successfully slides up smoothly on hover.
- **2026-08-13 (Session A):** Diagnosed and fixed the "Full-Surface Blur on Launch" (Bug #40). `applyBackgroundStyle()` fired at startup before QML reported panel geometry, passing an empty `QRegion` to `KWindowEffects::enableBlurBehind` — which tells KWin to blur the entire Wayland surface. Added a guard to defer blur-using styles until the visual region is valid. Prepared comprehensive PR description for upstream contribution to isac322/krema.
- **2026-06-07 (Session A):** Diagnosed and fixed the "Hit-Testing Off-Center Zoom Deadzone". Parabolic zoom physically shifts icons apart, but `main.qml` hit-testing was rigidly anchored to the unzoomed center (`unscaledCenter`). Replaced `unscaledCenter` with dynamic `visualCenter` mapping so the hitbox perfectly tracks the icon's real-time visual coordinate shift, restoring flawless pixel-perfect hit-testing during 2.0x max zoom.
- **2026-06-07 (Session A):** Diagnosed and fixed the \"Invisible Click-Blocking Wall\". The Wayland `InputRegion` was permanently capturing a massive invisible rectangle around the dock because: (1) It mistakenly included `p.margin` (64px shadow bounds) in its hit area, and (2) it was forced to `zoomOverflowHeight` via a static `m_visible` override. Removed `margin` from input region (shadows shouldn't block clicks) and bound the overflow dynamically to `m_hovered`. The Wayland Input Region now flawlessly breathes with the QML hover state: shrinking to a safe 24px overflow to allow background window clicking, and instantly expanding to `zoomOverflowHeight` when actively interacting to catch huge zoomed icons.
- **2026-06-06 (Session B):** Fixed the Hyprland blur sharp box issue (Trial 1: Shader clamp - failed due to QRegion physically clipping the shader. Trial 2: `params.margin = 64` expansion - failed because it didn't account for perspective shadow growth. Trial 3 [FINAL]: Replaced perspective projection in `outer_shadow.frag` with orthographic projection so shadow size stays within the fixed 64px `margin` bounding box on wide panels. Verified by user.) Implemented mathematical Island Alignment in main.qml to correctly place dockRow on Left/Center/Right based on alignment settings.
# Work State (The Master Mind)

> **Role:** This file is the central hub for project coordination. It bridges sessions by tracking priorities, summarizing technical memory, and linking to deep-dive documentation.

## Knowledge Index
- **Architectural Truth:** [ARCHITECTURE Mandate.md](../ARCHITECTURE Mandate.md) (The mathematical constitution)
- **Technical Memory:** [docs/bugs_report.md](../docs/bugs_report.md) (Failed trials, root causes, and ironclad fixes)
- **Research Logs:** [docs/research/](../docs/research/) (Deep dives into math, protocols, and KWin issues)
## Current Milestone

**Phase 1: Foundational Architecture (v0.8.0)** ⬅️
*Focus: Completing the 3-Tier Mathematical Engine, workspace awareness, and layout modes.*

## Completed Items

- [x] M1-M8: Foundation stabilization, shaders, shadow clamping, and kinetic physics.
- [x] **Flatpak Identity Resolver:** Enhanced `IdentityManager` to automatically bridge short Wayland `app_id`s to reverse-DNS Flatpak identifiers, natively fixing icons and launcher URLs.
- [x] **Instance-Aware Active Indicators (Dynamic Dash):** Implemented shifting dash to represent focused window instance accurately across Hyprland and KDE via `KdeTasksProxyModel`.
- [x] **Flatpak Vector Icon Fix:** Removed strict `availableSizes().isEmpty()` checks from the icon rendering bridge, allowing scalable vector graphics (SVGs) to render successfully.
- [x] **Fuzzy Identity Resolver:** Updated suffix matching in Flatpak extractor to aggressively strip spaces and dashes, resolving "Aim Train" Wayland class vs Flatpak ID desyncs.
- [x] **Lua IPC Interception Fallback:** Resolved Hyprland context menu "Close" failure by rewriting raw socket dispatches to `hl.dsp.window.close()` when the user runs the `hyprland-lua-plugins` extension.
- [x] **Interaction Flooring Constitution:** Established 5-unit mathematical vertical stack `[LEGACY — migrating to 3-Tier]`.
- [x] **V1+V2 Document Merge:** Unified ARCHITECTURE Mandate (20 rules), ROADMAP (v0.8.0), and plans directory under V2's 3-Tier structure.
- [x] **Bug Sweep (#12, #18-#28):** Fixed autohide failure, Hyprland crashes, signal leaks, edge placement desyncs, missing flatpak SVGs, and lua IPC dispatches.
- [x] **Identity Bridge:** Resolved Wayland AppID mismatch via dynamic KService lookup (Rule 11).
- [x] **Hyprland Launch Fix:** Resolved application launch failure on Hyprland via direct KService execution.
- [x] **Absolute Sync:** Locked interaction boundaries 1:1 to visual icon pixels.
- [x] **Startup Gap Resolution:** Fixed recursive race condition via **State-Aware Layout**.
- [x] **Sticky Preview Fix:** Resolved interaction deadlocks via Trial 4 (Icon-Gated Visibility).
- [x] **UI Polish:** Resolved text overlaps (Rule 20) and restored Settings UI consistency.
- [x] **Edge Placement Fix:** Fixed settings geometry desync where Wayland window was stuck due to screen overrides.
- [x] **Configuration Desync Tracker:** Added `--debug-config` flag and override interception to track config mismatch bugs.
- [x] **Kinetic Zoom:** Implemented smoothed easing orbits (Rule 19).
- [x] **Preview Geometry Sync:** Resolved "Altitude Confusion" via Absolute Sync (Rule 17).
- [x] M1-M7: 전체 완료
- [x] 접근성 5단계 구현 + 키보드 내비게이션
- [x] E2E 테스트 인프라 (10개 메커니즘 PoC)
- [x] v0.7.0 릴리즈
- [x] M8a-M8d: 가상 데스크톱, 멀티 모니터, Per-Screen 설정, Follow Active
- [x] 멀티 배포판 패키징 인프라 구축
  - COPR (Fedora 42/43/Rawhide): 6/6 빌드 성공
  - OBS (openSUSE Tumbleweed/Slowroll/Leap 16.0, Fedora 42/43/44/Rawhide, Debian 13, Ubuntu 25.04-26.04): 21/21 builds succeeded
  - Launchpad PPA (Ubuntu 25.10 questing, 26.04 resolute): Published
  - AUR: 기존 운영 유지
- [x] 크로스 배포판 빌드 호환성
  - LayerShellQt setDesiredSize 컴파일 시점 감지 (KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE)
  - QString QT_NO_CAST_FROM_ASCII 호환
  - openSUSE ninja/autostart 경로 조건부 처리
- [x] /release 스킬에 멀티 배포판 배포 파이프라인 추가
- [x] README 배포판 배지 + 설치 가이드 업데이트
- [x] SettingsWindow 리팩터링: 폴링 루프 → configViewItem 직접 참조
- [x] Docker GUI runtime smoke infrastructure
  - 외부 OBS/osc 산출물을 distro별 컨테이너에 설치 후 host KWin virtual Wayland socket에 연결해 실행
  - 대상: openSUSE Tumbleweed/Slowroll/Leap 16.0, Fedora 42/43/44/Rawhide, Debian 13, Ubuntu 25.04/25.10/26.04
- [x] OBS 원격 artifact 기반 Docker GUI smoke 검증 (10/11 통과)
  - `origin/master` rebase 후 OBS 21/21 succeeded artifact 재다운로드
  - Runtime images built (arm64): Fedora 42/43/44/Rawhide, openSUSE Tumbleweed/Leap 16.0, Debian 13, Ubuntu 25.04/25.10/26.04
  - 통과 (status 143 = timeout 후에도 프로세스 alive): openSUSE Tumbleweed, openSUSE Leap 16.0, Fedora 42, Fedora 43, Fedora 44, Fedora Rawhide, Debian 13, Ubuntu 25.04, Ubuntu 25.10, Ubuntu 26.04
  - 미검증: openSUSE Slowroll (아래 알려진 이슈 참조)
  - Debian/Ubuntu: OBS DEB Depends를 `qml6-module-org-kde-kirigami`/`qml6-module-org-kde-pipewire`로 수정 (packaging/obs/debian.control), 임시 repack DEB로 4개 타겟 smoke 통과
  - openSUSE: spec의 Requires를 Tumbleweed/Leap 패키지명으로 분리 (packaging/obs/krema.spec), 기존 artifact + `krema-suse-compat-provides` + `dbus-1-daemon` 포함 runtime image로 2개 타겟 smoke 통과

## Known Issues

- (None — Bugs #18-#25 all fixed on 2026-05-19)
- AllScreens/FollowActive: 실제 듀얼 모니터에서 검증 필요
- QML fade/slide 전환 애니메이션 미구현 (현재 instant show/hide)
- Per-screen 설정 UI 페이지 미구현 (백엔드만 완료)
- PipeWire 글로벌 스트림 캡 미구현
- 현재 OBS 원격 DEB artifacts는 `libkirigami2-6`, `kpipewire` Depends 때문에 Debian 13/Ubuntu 25.04/25.10/26.04에 설치 불가; 수정된 `packaging/obs/debian.control`로 재빌드 필요. 임시 repack 검증에서는 Debian 13/Ubuntu 25.04/25.10/26.04 4개 전부 GUI smoke 통과
- 현재 OBS 원격 openSUSE RPM artifacts는 `kf6-kirigami-addons`/`kpipewire`/`plasma-workspace`/`layer-shell-qt` Requires가 Tumbleweed/Leap 16.0/Slowroll 패키지명과 불일치; 수정된 `packaging/obs/krema.spec`로 재빌드 필요. 기존 artifact + `krema-suse-compat-provides` 조합으로는 Tumbleweed/Leap 16.0 GUI smoke 통과
- Slowroll OBS artifact는 x86_64만 제공되며 현재 arm64 Docker 호스트는 binfmt에 `qemu-x86_64` 핸들러가 없어 amd64 컨테이너 실행이 `exec format error`로 막힘. Arch에서는 `yay -S qemu-user-static qemu-user-static-binfmt` 후 `systemctl restart systemd-binfmt`(또는 재로그인) 하면 `tests/docker/run-smoke.sh opensuse-slowroll <package-dir>`로 검증 가능. x86_64 컴팩트 RPM은 `/tmp/opencode/krema-fixed-artifacts/opensuse-slowroll/`에 준비됨 (`krema-0.7.0-18.1.x86_64.rpm`, `krema-suse-compat-provides-0.7.0-1.x86_64.rpm`)

## On Hold Items

- [ ] **Icon Sizing Regression Fix:** Bug is elusive. Shelved by user pending further reproducibility.
- [ ] **Placement Stabilization (Bug #9)**: Conflict during edge transitions.

## Active Tasks

- [x] **M3: 3-Tier Migration:** Migrate current 5-Unit Stack to Panel → Island → Item hierarchy.
- [ ] **M10 Kickoff:** Design recursive container logic for Widgets (Rule 15).

## Session History

## Session History
- **2026-06-02 (Session I):** **Settings Slider Refactor (Panel Size / Glass Pill / Panel Thickness).** Restructured the Panel Settings page to have three independent sliders: (1) **Glass Pill Size** — controls `iconSize` only, with radius ratio sync. (2) **Panel Size** — pixel-based proportional slider that scales `iconSize` + `panelHeight` together using a captured ratio. (3) **Panel Thickness** — independently controls `panelHeight`. Removed the Island Margin slider (user deemed useless). Fixed `calculateMaxEnv()` missing `+16` that caused 16px overflow.
- **2026-06-02 (Session I):** Fixed "Proportional Slider Dead Zones". Rebuilt the Panel Size slider to use **Dynamic Track Boundaries**, where the slider's physical limits (`from` and `to`) continuously adapt to exactly match the mathematical ceilings/floors of `iconSize` constraints (e.g. 96px and 12px) based on the active ratio. This entirely eliminates the bug where the user could drag the slider further but only the panel thickness grew, breaking the proportional sync.
- **2026-06-02 (Session I):** Lowered the absolute minimum `IconSize` limit in the system configuration from `24px` down to `12px`, unlocking much smaller minimal bounds for the whole dock when scaling down proportionally.
- **2026-06-02 (Session I):** Replaced hardcoded margin limits (`_panelInternalMargin: Math.max(DockSettings.islandMargin, ...)`) with a fluid `Math.max(0, ...)` clamp, allowing the panel to perfectly shrink down and "hug" the Glass Pill (0px margin) without triggering visual clipping when overflow is disabled.
- **2026-06-02 (Session I):** Enforced a strict `99%` maximum length limit on the dock when **Floating Mode** is active. This is mathematically applied in `main.qml` and dynamically bound to the UI slider in `PanelPage.qml` to prevent the floating dock from completely touching the horizontal screen edges.
- **2026-06-03 (Session I):** Fixed "Visual Overflow State Resets". Elevated `AllowOverflow` from a transient QML UI toggle into a permanent `krema.kcfg` setting. This stops the Panel Thickness slider from auto-resetting tight configs back to 41px on dock restart.
- **2026-06-03 (Session I):** Fixed Vertical Dock Max Length. Removed an inverted ternary operator in `main.qml` that forced vertical docks to calculate their 100% length limit against the screen's *width* instead of its height. `_actualContentHeight` now correctly and unconditionally binds to `root.height` (the layer shell vertical boundary).
- **2026-06-03 (Session I):** Fixed Tooltip Wayland Clipping. Increased the Wayland Layer Shell surface thickness from `120px` to `350px` in `dockview.cpp`. This gives QML ample physical room to draw wide tooltips (like "System Settings") when popping out from vertical docks without being abruptly sliced off by the compositor bounds.
- **2026-06-03 (Session I):** **Implemented Dock Alignment:** Replaced the hardcoded screen-centering math with a full `Alignment` engine (Start, Center, End). 
  - Added the `Alignment` property to `krema.kcfg` and extended `ScreenSettings` C++ to support per-monitor alignment overrides.
  - Plumbed the logic into `main.qml`'s `_panelAlignmentPos`, perfectly respecting the floating padding / screen flooring `_screenFlooring`.
  - Created a dynamic UI segment in `VisibilityPage.qml` under "Screen Edge" that seamlessly morphs its buttons (Left/Center/Right vs Top/Center/Bottom) based on the dock's current orientation.
- **2026-06-01 (Session H):** Implemented the true **Span Screen** mode cleanly in `main.qml`. In "Span Screen" mode (`DockSettings.panelLengthMode === 1`), `dockPanel.width` mathematically calculates to 100% (or `MaxLength` percentage) of the physical screen width, transforming it into a full-width taskbar. The icons naturally center themselves within this vast panel using pure coordinate math (`(dockPanel.width - implicitWidth) / 2`). This entirely removes the need for glitchy KWin KWayland constraint caps, `Flickable` scroll boundaries, or squishing logic.
- **2026-06-01 (Session H):** Restored the pure "Island Glass Pill" (Rule 18) geometry by reverting it to `anchors.fill: parent`. I had mistakenly assumed the Glass Pill's height needed to be mathematically forced to match the `DockSettings.panelHeight` to prevent it from floating below the dock. However, the original "Floating Frankenstein" bug was already cured in Session G when I anchored `dockRow` to the floor! By forcing the Glass Pill to match the shrinking dark panel height, I broke its core purpose (wrapping the protruding icons). It now correctly wraps the `IslandModule` content, stopping perfectly 8px above the bottom of the dark panel, restoring the signature Krema-v2 aesthetic.

- **2026-06-01 (Session G):** Replaced "Squish Math" with a true **Panel Length Mode** ("Span Screen" vs "Adaptive"). In "Span Screen" mode, the `_actualContentWidth` is forced to 100% of the `maxAllowedWidth` (e.g., screen width), transforming the dock into a full-span taskbar. `scrollAreaBounds` naturally centers itself inside the massive panel, keeping the icons perfectly centered in the middle of the screen without shrinking them.
- **2026-06-01 (Session G):** Diagnosed and fixed the "Thin Glass Line" and "Off-Screen Icons" bugs introduced by the forced centering in Session F. By perfectly centering the dock inside the Main Panel, lowering the panel thickness (e.g., to 24px) pushed the `dockRow` mathematically off the screen, violating Rule 6 (Top-Down Reveal). I restored Rule 6 by anchoring `dockRow` to the floor, ensuring the icons protrude visibly upwards into the screen (Top-Down Reveal) rather than being clipped by the screen edge. To preserve the "Picture Frame" (margins all around), I introduced `_panelInternalMargin: 8`. This perfectly offsets the floor anchor, granting an 8px bottom and 8px left/right margin at all times. Increasing the slider up to 122px yields the perfect 8px ceiling margin for the Picture Frame, while lowering it allows safe visual protrusion. I also fixed a critical bug in `IslandModule.qml` where `implicitHeight` was bound to an unbound `parent.height`, evaluating to 0 and drawing the Glass Pill as a 2px horizontal line. It now properly maps to `container.implicitHeight`.

- **2026-06-01 (Session F):** Implemented the "Picture Frame" (Suspended Island) architecture. User provided a diagram and a screenshot explicitly showing their desire for the Glass Pill to be completely encased in the Main Panel with padding on all four sides. I reverted the horrific Wayland overrides from Session D/E, restored strict adherence to `DockView.screenSettings.panelHeight`, and fully centered `dockRow` on both axes (`x: (dockPanel.width - implicitWidth) / 2` and `y: (dockPanel.height - implicitHeight) / 2`). Because the panel height is strictly controlled by the slider, and the slider was previously capped up to `122px` in Session C, dragging the slider to max perfectly encases the 106px icons/pill inside a mathematically pure 8px frame on all sides. This fulfills the user's diagram without breaking Wayland bounding or floating off the screen edge.

- **2026-06-01 (Session E):** Resolved horrific layout breakage caused by Session D. The forced vertical centering in `main.qml` violated Rule 6 (Top-Down Reveal) and caused severe Wayland surface bounding desyncs (floating dock). Reverted the artificial padding logic. Restored mathematical grounding so the Glass Pill and Icons securely sit on the panel ground (`dockRow.y = dockPanel.height - implicitHeight`). Fixed a horizontal asymmetric offset by centering the `IslandModule` wrapper correctly (`x: -8`). The `+16px` boundary is now strictly an optional slider cap; dragging the slider up produces a perfect top-margin frame without destroying the ground logic.
- **2026-06-01 (Session D):** Implemented the **Framed Island** architectural update. The user requested a persistent "picture frame" margin where the Tier 1 background visually wraps the Tier 2 Glass Pill on all four sides. This required an official modification to **Rule 1 (Geometric Gravity)** to permit a cross-axis internal padding (`_panelInternalMargin = 6`) that legally lifts the layout off the 0px screen edge anchor. Updated UI constraints in `IconsPage.qml` and `PanelPage.qml` to include the `+12px` bidirectional padding buffer, flawlessly preserving the "Top-Down Reveal" (Rule 6) while enforcing the requested internal frame.
- **2026-06-01 (Session C):** Diagnosed and fixed the "Hovering Island" (Bug #35) and "Slider Cap Asymmetry" (Bug #36) visual layout defects. The Tier 2 Glass Pill was appearing detached and floating above the panel ground because of a redundant 8px global offset, coupled with an asymmetrical internal bounds calculation. I firmly locked `IslandModule` to `parent.height` so it perfectly mirrors the 106px `dockRow` envelope, flawlessly centering the 90px icons within a symmetrical 8px glass border while touching the absolute screen edge. Concurrently, discovered the settings slider was artificially capped at `90px` instead of `106px` because it lacked the `+ 16px` visual padding logic. Updating the slider boundaries restored the illusion of absolute mathematical symmetry at max panel thickness.
- **2026-06-01 (Session B):** Diagnosed and fixed the "Vertical Indicator Wrap" bug. Discovered that the QML `Flow` layout for `indicatorRow` had its `height` hardcoded to `_unitIndicator` (3px) for horizontal symmetry. In vertical mode (`Flow.TopToBottom`), this artificial 3px constraint caused the engine to silently run out of space and wrap the active dashes horizontally into the next column. Removed the explicit width/height bindings and allowed the `Flow` to use `implicitWidth`/`implicitHeight`, which fixed the alignment completely.
- **2026-06-01 (Session B):** Diagnosed and fixed the "Floating Dock" Wayland desync on screen unlock (Bug #33). Discovered that using `hide()` and `show()` inside `DockView` to recover from KWin destroying layer surfaces during DPMS sleep caused a layer stacking conflict where the dock's exclusive zone stacked on top of the Plasma panel. Migrated the `org.freedesktop.ScreenSaver` listener into `MultiDockManager` and replaced the hack with a clean `scheduleTopologyUpdate()`. This rebuilds the dock entirely on unlock, forcing KWin to accurately map the absolute screen edge anchors.
- **2026-06-01 (Session A):** Diagnosed and fixed the "Sandbox Ghost Bug" (Bug #32). Discovered that `just run` within IDE terminals (like VS Code or Kitty) caused KWin to sandbox the dock as an untrusted Wayland client, silently blocking all `org_kde_plasma_window_management` IPC and dropping all unpinned Wayland apps. Modified `justfile` to inject `kstart`, enforcing native KDE trust and fully restoring active app rendering during development. Verified that the `ChildCountRole` QML logic (Bug #31) was functionally perfect and restored the `clem-master` branch.
- **2026-05-25 (Session A):** Resolved **Active Indicator Desync** (Bug #29). Discovered that `KdeTasksProxyModel` filtered out `dataChanged` signals if `IsActive` wasn't explicitly provided in the role list, causing external window activations to fail to update the QML active dot index. Modified the proxy to unconditionally emit `ActiveChildIndexRole` for the parent whenever a child changes. Also implemented **8px internal padding** for `IslandModule` to create a premium spatial separation between the glass pill and the dock panel's external boundary.
- **2026-05-25 (Session A):** Completed **M3: 3-Tier Migration**. Extracted QML Tier 1 (VisualPanel) and Tier 2 (IslandModule). Maintained Tier 3 (AppIcon) decoupling, ensuring visual overflow (Rule 6). Introduced `BaseIsland` to C++ `DockModel`. Implemented surgical QML alias forwarding (`_currentDockRepeater`) to preserve legacy Parabolic Zoom math while isolating structural tiers.
- **2026-05-23 (Session B):** Applied the Surgical Bug-Fix Protocol to resolve **Bug #28**. Discovered that the user's `hyprland-lua-plugins` extension intercepts standard `/dispatch closewindow address:` IPC socket commands. Added a fallback branch in `HyprlandIpc` that intercepts the Lua error and dynamically rewrites the socket string to `hl.dsp.window.close()`, successfully restoring the "Close" context menu action.
- **2026-05-23 (Session A):** Discovered that vector-based Flatpak icons (SVGs) were failing to render because they report an empty `availableSizes()` before rendering. Removed the strict check in the image bridge to pass SVGs successfully to QML.
- **2026-05-23 (Session A):** Updated the **Flatpak Identity Resolver** with extreme fuzzy suffix matching. Stripped spaces and dashes completely to match edge cases like `"aim train"` vs `io.gitlab.aimtrain.aimtrain.svg`.
- **2026-05-23 (Session A):** Implemented **Flatpak Direct Icon Extractor** in `TaskIconProvider`. Directly scans `~/.local/share/flatpak/` and `/var/lib/flatpak/` for icon assets on standalone window managers like Hyprland where `XDG_DATA_DIRS` might not be correctly populated, bridging the gap between flatpak apps and native rendering.
- **2026-05-23 (Session A):** Resolved **Flatpak Identity Mismatch**. Created a reverse-DNS caching resolver inside `IdentityManager` that scans `KService` via `StartupWMClass` and suffix matching. This bridges short Wayland classes (e.g. `stremio`) to Flatpak `.desktop` bundles (e.g. `com.stremio.Stremio`), inherently fixing missing Flatpak icons and middle-click launcher instances.
- **2026-05-23 (Session A):** Completed **Instance-Aware Active Indicators (Dynamic Dash)**. Injected `ActiveChildIndex` into `HyprlandTasksModel` and built `KdeTasksProxyModel` to safely adapt KDE's tree model into QML, accurately shifting the active dash to the focused window.
- **2026-05-20 (Session A):** Discovered and fixed the "Stuck at Bottom" Edge Placement bug. The UI was moving to global settings while the Wayland Window was blocked by a local screen override. Implemented the Configuration Desync Tracker (`--debug-config`) to instantly expose these state desyncs in the future.
- **2026-05-19 (Session C):** Raw code audit — found 8 bugs (#18-#25). Merged V1+V2 project documents: unified ARCHITECTURE Mandate (20 rules, 3-Tier structure), ROADMAP (v0.8.0 → v1.0.0), and plans directory. Promoted V2 plans, cleaned up duplicates, deleted v2 source files.
- **2026-05-19 (Session B):** Fixed Hyprland window grouping and identity crisis. Integrated `IdentityManager` into `HyprlandTasksModel` to properly extract `.desktop` identifiers from `launcherUrl` and Wayland `class` metadata. Restructured the `Task` model to support a `QList<WindowInfo>`, allowing the QML layer to accurately render indicator dots (`ChildCount`) and map multiple window instances to a single pinned application.
- **2026-05-19 (Session A):** Initiated Hyprland support. Created `HyprlandDockPlatform` to handle positioning on non-KDE compositors. Updated `DockPlatformFactory` to detect Hyprland sessions via `XDG_CURRENT_DESKTOP`.
- **2026-05-19 (Session A):** Fixed Hyprland application launching bug. Replaced `QDesktopServices::openUrl` with direct `KService` exec line parsing and `QProcess` execution. Verified `nwg-dock-hyprland` reference and implemented robust field code stripping.

- **2026-05-16 (Session A):** Cleaned up `ROADMAP.md`. Migrated granular bugs (#9, #10) to `docs/bugs_report.md`. Added **Instance-Aware Active Indicators (Dynamic Dash)** to Phase 2. Marked **Preview Geometry Sync** as completed. Updated `justfile` for development environment autostart. Added **Hyprland Support** to the roadmap backlog.

- **2026-05-14 (Session A):** Resolved **Dolphin/Settings Identity Crisis**. Unified AppID normalization via dynamic KService lookup, removing hardcoded bridges.

- **2026-05-14 (Session A):** Applied **Absolute Sync** to tooltips. Icon labels now track visual growth and center in real-time using `Calculated Reality` math.
- **2026-05-14 (Session A):** Resolved **RED 2 (Ghost Sheet Blur)**. Decoupled the blur region from the input region by introducing `setBlurRegion` to the platform interface. Blur is now strictly mapped to the visual panel size.
- **2026-05-14 (Session A):** Resolved **Ghost Popups** and **Settings Slider** synchronization. Implemented `IsWindow` gateway filters and the **Activation Guard** to prevent premature triggers from zoom animations. Consolidated preview math into the **Calculated Reality** system.
- **2026-05-12 (Session B):** Completed **Phase 2 Milestone 9**. Resolved **Sticky Previews** and interaction deadzones. Polished settings UI and codified **Rule 20 (Safe Spacing)**. Finalized **Strong Foundation** phase with memory-first synchronization.
- **2026-05-12 (Session A):** Definitive fix for **Startup Gaps** (State-Aware Layout). Identified **Signal Desync** and **Orbit Suffocation** as causes for zoom sluggishness.
- **2026-05-11 (Session B):** Definitive resolution of "Ghost Mouse" via **Absolute Sync**.

- M8 전체 릴리즈 검토 (v0.8.0)
- M9: Widget System + System Tray
- 수정된 `packaging/obs/debian.control`/`packaging/obs/krema.spec`로 OBS artifact 재빌드 후 `tests/docker/run-smoke.sh <target> <package-dir>`로 Debian/Ubuntu/openSUSE 전체 GUI smoke 재실행 (현재는 임시 repack/compat-provides 경로로만 통과)
- Arch 호스트에 `qemu-user-static` + `qemu-user-static-binfmt` 설치 후 `tests/docker/run-smoke.sh opensuse-slowroll /tmp/opencode/krema-fixed-artifacts/opensuse-slowroll`로 Slowroll smoke 마지막 1개 검증
