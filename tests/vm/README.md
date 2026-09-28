# Krema Tier 3 — full-session VM QA

Boots a real distro cloud image in QEMU/KVM, provisions a minimal Plasma 6
Wayland desktop with SDDM autologin, installs the Krema package built by
`tests/vm/build-package.sh` through the distro package manager, and verifies
krema inside the running session over SSH. The same image doubles as a
click-around VM for manual QA (`--interactive`).

Tier 2 (`tests/appium/`) runs krema inside a private `kwin_wayland` in a
container. Tier 3 exercises everything below and around it: the real
plasmashell + kwin_wayland session, SDDM autologin, the distro package and
its dependencies, session autostart, AT-SPI, and KWin window management.

## Running

```sh
tests/vm/build-package.sh fedora-43 /tmp/krema-pkg   # RPM into /tmp/krema-pkg/fedora-43/
tests/vm/run-vm-qa.sh fedora-43 /tmp/krema-pkg/fedora-43
tests/vm/run-vm-qa.sh fedora-43 /tmp/krema-pkg/fedora-43 --interactive
```

`<target-id>` is a row of `tests/docker/targets.tsv`
(`fedora-42/43/44/rawhide`, `opensuse-*`, `debian-13`, `ubuntu-*`) or `arch`.
Each target is handled by a plug-in `tests/vm/distros/<family>.sh`; new
distros add one file there (see "Distro plug-ins").

The first run downloads the cloud image and installs Plasma inside it
(~minutes), then snapshots it as a golden image. Later runs clone the golden
image and reinstall only the Krema package — pass a new `<package-dir>` to
test a rebuild without paying the provisioning cost again. The golden image
is reprovisioned automatically when the distro image or the provision script
changes.

### Interactive QA

`--interactive` boots the VM, prints connection details, and waits:

* VNC on `vnc://127.0.0.1:<port>` (any VNC client) — `qa` autologs into a
  Plasma (Wayland) session; krema autostarts.
* SSH: `ssh -i tests/vm/.cache/<target>/id_ed25519 -p <port> qa@127.0.0.1`
  (passwordless sudo).
* Ctrl-C powers the VM off after saving a screendump.

## Host requirements

qemu-system-<arch>, qemu-img, `cloud-localds` or `genisoimage`/`xorriso`,
UEFI firmware, curl, ssh/scp/ssh-keygen, python3, and a hardware
accelerator (`/dev/kvm` on Linux, Hypervisor.framework on macOS).
Scripts pick qemu/firmware/serial by `uname -s`/`uname -m`, so they run
unchanged on aarch64 and x86_64 hosts and on Linux and macOS.

On x86_64 Linux CI (GitHub `ubuntu-latest` has /dev/kvm):

```sh
sudo apt-get install qemu-system-x86 qemu-utils cloud-image-utils ovmf
```

`.github/workflows/vm-qa.yml` runs the whole matrix (targets.tsv + `arch`)
on GitHub: manually (`gh workflow run vm-qa.yml -f targets=fedora-43,arch`),
on published releases, and weekly — never on pull requests. Each job builds
the package with `build-package.sh`, restores the golden image from the
Actions cache (keyed on the distro plug-in, `lib/cloud-init.sh`,
`guest/cargo-install.sh` and the upstream image checksum), runs
`run-vm-qa.sh`, and uploads `tests/vm/artifacts/<target>` as
`vm-qa-<target>`.

On the Lima `krema-tier3` VM (Ubuntu, aarch64, nested KVM — currently
unreliable, see below):

```sh
sudo apt-get install qemu-system-arm qemu-utils cloud-image-utils \
    qemu-efi-aarch64 ovmf
```

On macOS (aarch64, Hypervisor.framework; no Homebrew needed):

```sh
nix shell nixpkgs#qemu nixpkgs#xorriso -c ./tests/vm/run-vm-qa.sh fedora-43 <pkgdir>
```

nixpkgs qemu ships `edk2-aarch64-code.fd`; the runner creates the vars
flash itself when no distro VARS template exists.

## What it checks (tests/vm/guest/verify-session.py)

Run as `qa` over SSH inside the live session (environment grafted from the
running kwin_wayland):

| Check | Assertion |
|---|---|
| `processes.plasmashell` / `processes.kwin_wayland` | real Plasma Wayland session is up |
| `krema.installed_from_cargo` | `krema-cargo-install.service` ran and the distro package manager reports krema installed |
| `krema.running_via_autostart` | krema process exists, an autostart desktop file exists, and the process is a session descendant |
| `atspi.krema_dock_toolbar` | the `Krema Dock` tool bar is exposed in the AT-SPI tree (pyatspi) |
| `app.launch_appears_in_dock` | launching kwrite produces a matching dock button |
| `kwin.window_list_and_activation` | KWin scripting sees the app window and krema's dock surface, and can activate the window |
| `krema.version_matches_package` | `krema --version` output (when any) matches the installed/cargo package version |
| `journal.no_krema_crash` | no krema segfault/coredump lines in this boot's journal or coredumpctl |

Artifacts land in `tests/vm/artifacts/<target>/` (gitignored, recreated per
run): `screen.png` (QMP screendump, also on failure), `verify.log`,
`verify.json`, `junit.xml`, `journal.txt`, `boot-services.log`,
`serial.log`, `summary.txt`. Exit status is non-zero on any failure.

## How it works

```
base.qcow2 ── download (sha256-verified)
     │  qemu-img convert+resize
prov.qcow2 ── boot #1 with seed.iso (cloud-init):
     │          user qa + ssh key, dnf/apt/zypper install Plasma + sddm,
     │          autologin, a11y env, install krema-cargo-install.service,
     │          marker /var/lib/krema-provisioned, poweroff
     ▼
golden.qcow2 + golden.key (fingerprint of provision inputs) ── cached
     │  qemu-img create -b golden (fresh overlay each run)
run.qcow2 ── boot #2 with seed.iso (same instance-id → cloud-init no-ops)
             + cargo.iso (packages/ + guest/):
     │        krema-cargo-install.service mounts the ISO, dnf-installs the
     │        .rpm (deps resolved from Fedora repos), then sddm starts and
     │        qa autologs into Plasma Wayland where krema autostarts
     ▼
ssh verify → junit.xml + screen.png + logs
```

A second seed ISO is unnecessary on run boots: cloud-init sees the same
instance-id and skips per-instance modules, while the systemd unit handles
the package install.

## Environment variables

| Variable | Default | Effect |
|---|---|---|
| `KREMA_VM_CACHE` | `tests/vm/.cache` | cloud images, golden image, SSH key |
| `KREMA_VM_ARTIFACTS` | `tests/vm/artifacts` | per-run artifacts root |
| `KREMA_VM_MEM` / `KREMA_VM_SMP` | `4096` / `4` | VM RAM (MiB) / vCPUs |
| `KREMA_VM_HEADS` | `1` | virtio-gpu outputs (multi-monitor QA: `2`) |
| `KREMA_VM_DISK_GB` | `14` | provisioned disk size |
| `KREMA_VM_PROVISION_TIMEOUT` | `3600` | first-boot package install ceiling |
| `KREMA_VM_BOOT_TIMEOUT` | `900` | run-boot SSH + install ceiling |
| `KREMA_VM_VERIFY_TIMEOUT` | `60` | per-wait timeout inside the guest verifier |
| `KREMA_VM_APP` / `KREMA_VM_APP_DESKTOP` / `KREMA_VM_APP_REGEX` | `kwrite` / `org.kde.kwrite` / `(?i)kwrite\|untitled` | the app launched for the dock-button check |

## Distro plug-ins

`tests/vm/distros/<family>.sh` is sourced by `run-vm-qa.sh` with `TARGET_ID`
and `FAMILY` set and provides:

* `DISTRO_PKG_GLOB` — package glob for `<package-dir>` (`*.rpm`, `*.deb`, ...)
* `distro_image()` — prints `<url> <sha256|sha512:hex|->` of the official
  cloud image (a bare digest is sha256; `sha512:` for distros such as Debian
  that publish only SHA512SUMS; `-` skips verification)
  for the host arch (`krema_arch` gives `x86_64`/`aarch64`,
  `krema_arch_deb` gives `amd64`/`arm64`)
* `distro_provision()` — prints bash (runs as root in the provision boot)
  that installs a minimal Plasma 6 Wayland session, SDDM + Wayland greeter,
  the QA tools (`python3-pyatspi`, `python3-gobject`, a launchable app like
  konsole/kwrite) and enables `sddm` + `graphical.target`. The shared
  epilogue adds autologin, the cargo unit, screen-lock/a11y config and the
  serial console.

The provision script's content (plus the cargo installer and the resolved
image checksum) is the golden-image fingerprint — editing `distro_provision`
automatically triggers reprovisioning.

Currently implemented: `fedora` (`fedora-42`, `fedora-43`, `fedora-44`,
`fedora-rawhide`), `arch` (`arch`; x86_64 hosts only — Arch publishes no
aarch64 cloud image), `debian` (`debian-13`, `ubuntu-25.04`, `ubuntu-25.10`,
`ubuntu-26.04`; EOL Ubuntu releases fall back to
`cloud-images.ubuntu.com/releases/<codename>/release/`), `suse`
(`opensuse-tumbleweed`, `opensuse-leap-16.0`, `opensuse-slowroll`; official
Minimal-VM Cloud qcow2s. Slowroll has no image or aarch64 repositories: it
boots the Tumbleweed x86_64 image and `zypper dup`s to the Slowroll repos, so
it runs on x86_64 hosts (CI) only).

## Files

| Path | Role |
|---|---|
| `run-vm-qa.sh` | host entrypoint: image cache, provisioning, QEMU, verify, artifacts |
| `lib/common.sh` | logging, tools, ports, ISO/download/ssh/QMP helpers |
| `lib/cloud-init.sh` | user-data + NoCloud seed + cargo ISO generation |
| `lib/qemu.sh` | arch-aware QEMU command line |
| `lib/qmp.py` | one-shot QMP client (screendump, quit) |
| `distros/fedora.sh` | Fedora image resolution + provision script |
| `distros/arch.sh` | Arch Linux image resolution + provision script (x86_64) |
| `distros/debian.sh` | Debian/Ubuntu image resolution + provision script |
| `distros/suse.sh` | openSUSE Tumbleweed/Leap/Slowroll image resolution + provision script |
| `guest/cargo-install.sh` | in-VM per-boot package installer (pre-sddm) |
| `guest/verify-session.py` | in-session verification checks |
| `build-package.sh`, `pkg/` | sibling-owned package builder |
