# Krema Agent Guidelines

Rules that apply to every agent working on this repository, regardless of harness.

## Distribution Support Policy

- A distro release reaching end of life (EOL) is **never** a reason to remove a packaging target. Keep building and uploading it.
- A target is retired only when:
  1. its build breaks and cannot reasonably be fixed, or
  2. the product needs a newer dependency (Qt, KDE Frameworks, LayerShellQt, etc.) that the release cannot provide and this blocks product work.
- Platform-forced removal is per channel: when a build service stops offering a release (COPR deletes EOL Fedora chroots, Launchpad rejects uploads to Obsolete Ubuntu series, OBS removes a distro project), that channel simply stops serving the release. Other channels that still offer it keep building it — record the platform action, do not remove the release elsewhere.
- Add new distro releases promptly to every channel that offers them (OBS `packaging/obs/project.meta.xml`, COPR chroots, Launchpad PPA series, README Installation table).
- The scheduled workflow `.github/workflows/distro-release-watch.yml` opens tracking issues labeled `distro-release` when new distro versions appear; those issues drive the additions.

## Agent Rules

- Never propose or perform removal of a target, OBS repository, COPR chroot, or PPA series because it is EOL.
- When retiring a target, cite the concrete build log or the blocking dependency requirement in the PR/issue, and update `packaging/obs/project.meta.xml`, the README Installation table, and CHANGELOG (`### Removed`) accordingly.
- When reporting distro status, report EOL as information only, not as a recommendation to drop support.
