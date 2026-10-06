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

## Visual Evidence for Pull Requests

- Every agent MUST upload media evidence to the PR body or a PR comment for every visual change before requesting or conducting PR review (including internal agent review), marking the PR ready for review, or merging it.
- Dynamic changes (animations, movement, interactive transitions, and live or real-time updates) MUST include a video of the changed behavior. Screenshots cannot replace that video. Use video whenever a change is not clearly static.
- Purely static visual changes may use screenshots instead of video.
- Capture the actual running implementation from the PR revision under review. The media MUST visibly demonstrate each changed appearance or behavior; logs, passing tests, mockups, and unrelated footage are not substitutes.
- For dynamic changes, show the trigger or setup and the resulting motion or live update, not just the final state.
- Use any suitable capture method within existing authorization and safety rules. If one method fails, try another available route (local, containerized, or remote); capture difficulty does not waive this requirement.
- Open the uploaded media and confirm reviewers can view it and the change is clearly visible. Refresh evidence when subsequent commits alter the demonstrated appearance or behavior.
- If no authorized capture route works, report the concrete blocker and required access; keep the visual work incomplete and the PR not ready for review until evidence is attached.

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

- Releases follow the `release` skill (`.agents/skills/release/SKILL.md`) end to end (version choice, document sync, tag, GitHub release, AUR, OBS, COPR, PPA).
- On release, move every `[Unreleased]` entry into a `## [x.y.z] - YYYY-MM-DD` section and leave `[Unreleased]` empty.

## Distribution Support Policy

- A distro release reaching end of life (EOL) is **never** a reason to remove a packaging target. Keep building and uploading it.
- A target is retired only when:
  1. its build breaks and cannot reasonably be fixed, or
  2. the product needs a newer dependency (Qt, KDE Frameworks, LayerShellQt, etc.) that the release cannot provide and this blocks product work.
- Platform-forced removal is per channel: when a build service stops offering a release (COPR deletes EOL Fedora chroots, Launchpad rejects uploads to Obsolete Ubuntu series, OBS removes a distro project), that channel simply stops serving the release. Other channels that still offer it keep building it — record the platform action, do not remove the release elsewhere.
- Add new distro releases promptly to every channel that offers them (OBS `packaging/obs/project.meta.xml`, COPR chroots, Launchpad PPA series, README Installation table).
- The scheduled workflow `.github/workflows/distro-release-watch.yml` opens one tracking issue per channel, labeled `distro-release`, once that channel offers a distro release (including pre-releases) that Krema does not build there yet; those issues drive the additions. Releases a channel does not offer yet are only logged, never filed.

### Agent Rules

- Never propose or perform removal of a target, OBS repository, COPR chroot, or PPA series because it is EOL.
- When retiring a target, cite the concrete build log or the blocking dependency requirement in the PR/issue, and update `packaging/obs/project.meta.xml`, the README Installation table, and CHANGELOG (`### Removed`) accordingly.
- When reporting distro status, report EOL as information only, not as a recommendation to drop support.
