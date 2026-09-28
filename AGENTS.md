# Krema Agent Guidelines

Rules that apply to every agent working on this repository, regardless of harness. Situational rules (Wayland surfaces, QML UI, KDE APIs, performance, docs/SEO, E2E scenarios) live in `.agents/rules/`; read the matching rule before working in its area.

## Project Scope

- Krema is a dock for KDE Plasma 6 on Wayland only. Do not design for other desktop environments.
- When both a Qt API and a KDE API exist, use the KDE API. Follow the KDE HIG; prefer Kirigami / Kirigami Addons over plain QQC2.
- Never guess KDE APIs. Verify them against `docs/kde/` and the installed headers before implementing (see `.agents/rules/kde-api-verification.md`).
- Krema is a dock: visual smoothness and responsiveness are non-negotiable (see `.agents/rules/performance.md`).

## Code Rules

- C++23, Qt 6, KDE Frameworks 6.
- Code is formatted with clang-format (enforced by the pre-commit hook).
- Commit messages are English and follow Conventional Commits (`feat:`, `fix:`, `docs:`, `refactor:`, …).
- Documentation, manifests, and PR communication are in English.
- When adding or removing a dependency (`find_package()` / `target_link_libraries()` in CMake), update `packaging/arch/PKGBUILD` (`depends` / `makedepends`) and the OBS packaging (`packaging/obs/krema.spec`, `packaging/obs/debian.control`) in the same change.

## Roadmap

- `ROADMAP.md` tracks milestones; the current one is marked ⬅️. Work from the current milestone.
- After finishing a feature, check its ROADMAP items. When a milestone is complete, mark it ✅ and move ⬅️ to the next one.
- Keep the tech stack table current when dependencies change.

## CHANGELOG

- Every user-visible change (feature, bug fix, behavior change) adds an entry under `## [Unreleased]` in `CHANGELOG.md`.
- Categories: `### Added`, `### Changed`, `### Fixed`, `### Removed`.
- Write in English, past tense, from the user's point of view (user benefit, not the commit message).
- Do not record changes invisible to users (internal refactors, CI, agent configuration).

## Releases

- Releases follow `.claude/commands/release.md` end to end (version choice, document sync, tag, GitHub release, AUR, OBS, COPR, PPA).
- On release, move every `[Unreleased]` entry into a `## [x.y.z] - YYYY-MM-DD` section and leave `[Unreleased]` empty.

## Distribution Support Policy

- A distro release reaching end of life (EOL) is **never** a reason to remove a packaging target. Keep building and uploading it.
- A target is retired only when:
  1. its build breaks and cannot reasonably be fixed, or
  2. the product needs a newer dependency (Qt, KDE Frameworks, LayerShellQt, etc.) that the release cannot provide and this blocks product work.
- Platform-forced removal is per channel: when a build service stops offering a release (COPR deletes EOL Fedora chroots, Launchpad rejects uploads to Obsolete Ubuntu series, OBS removes a distro project), that channel simply stops serving the release. Other channels that still offer it keep building it — record the platform action, do not remove the release elsewhere.
- Add new distro releases promptly to every channel that offers them (OBS `packaging/obs/project.meta.xml`, COPR chroots, Launchpad PPA series, README Installation table).
- The scheduled workflow `.github/workflows/distro-release-watch.yml` opens tracking issues labeled `distro-release` when new distro versions appear; those issues drive the additions.

### Agent Rules

- Never propose or perform removal of a target, OBS repository, COPR chroot, or PPA series because it is EOL.
- When retiring a target, cite the concrete build log or the blocking dependency requirement in the PR/issue, and update `packaging/obs/project.meta.xml`, the README Installation table, and CHANGELOG (`### Removed`) accordingly.
- When reporting distro status, report EOL as information only, not as a recommendation to drop support.
