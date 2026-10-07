# New distribution channel research (2026-10-07)

Research only. Nothing listed here was registered, requested or submitted, and a release never registers a new channel. Acting on any candidate is a separate task that needs the user's approval, and each one must be rechecked against its official rules at that time.

## Method and counts

- 34 candidate channels were classified against their official documentation with a fixed rubric.
- 13 more were excluded before classification because Krema is already on them, a submission is pending, or they duplicate another row (see the channel map in `../SKILL.md`, section 7).
- No candidate was dropped by a size cap, and no classification errored.
- The first pass gave: conditional 23, other 8, incompatible 2, viable for automation 1. All 33 flagged rows were then checked against their official quotations; one high-confidence incompatible row was not re-read by hand.

After that review: **conditional 22, other 7, incompatible 2, viable (manual) 2, viable (automation) 1**, total 34. The sections below name the rows the review discussed. Gentoo GURU and Chimera count as conditional even though their AI policies block us; the second incompatible row is the one that was not re-read by hand. Flathub was researched earlier (`packaging/submissions/flathub-research.md`) and is listed under the policy blocks for completeness.

## Recommendations

In priority order:

1. **Awesome-Linux-Software** — viable, manual. The official CONTRIBUTING accepts an application name, a homepage or install guide, a short description, an icon and the correct section; the README already has a Dock section with Cairo-Dock, Docky and Latte-style projects. A one-time pull request. Source: <https://raw.githubusercontent.com/luong-komorebi/Awesome-Linux-Software/master/CONTRIBUTING.md>
2. **freshcode.club** — viable, manual. The submit form takes project, homepage and release metadata plus an Autoupdate URL, with GitHub/changelog autodiscovery, so later releases can update from a feed after a one-time form submission. Source: <https://freshcode.club/submit>
3. **Chaotic-AUR** — conditional. It builds binaries from AUR recipes, which would add prebuilt Arch packages on top of the existing AUR entry, but its request and acceptance policy has not been verified. Source: <https://raw.githubusercontent.com/chaotic-aur/packages/main/README.md>
4. **FSF Free Software Directory** — conditional. Its requirements accept free software that runs on GNU/Linux, which matches Krema's license and platform; the submission form details are still unknown. Source: <https://directory.fsf.org/wiki/Free_Software_Directory:Requirements>

**Repology** was rated viable for automation, but it is not new work: <https://repology.org/project/krema/versions> already indexes the AUR package at 0.10.0. Whether it also picks up COPR, OBS or the PPA is not established. Requirements: <https://repology.org/docs/requirements>

## Blocked by policy

Do not author contributions to these or suggest working around their rules:

- **Flathub**: its AI policy excludes AI-generated or AI-assisted manifests and automated agent pull requests. See `packaging/submissions/flathub-research.md`.
- **Gentoo GURU** and **Chimera Linux**: explicit policies against AI-assisted contributions.
- **elementary AppCenter**: incompatible. It requires the elementary Flatpak runtime and forbids AI-generated submission content.

## Conditional, not ready

Each needs facts that the official sources did not establish. Don't call any of them ready or open requests.

| Channel | Open question |
|---|---|
| Debian, Ubuntu, Fedora, openSUSE official archives | Long-term maintainer/sponsor review; inclusion rules and availability of every dependency at the required versions |
| Snap Store | A KDE neon 6 extension exists, but Qt ≥ 6.8, layer-shell-qt/plasma-workspace access and confinement compatibility are not established. Adapting the application would need separate approval |
| NUR | Technically possible, but overlaps the pending nixpkgs pull request; no action now |
| apps.kde.org | Indexes projects hosted in KDE repo-metadata, not external applications; incubation/hosting prerequisites unverified |
| Void Linux | Prefers mature release history and usually non-developer submission; KDE dependencies unknown |
| Alpine, Solus, pkgsrc, FreeBSD, Guix, Pacstall, SlackBuilds, CachyOS | Dependency, platform or inclusion prerequisites not fully established |
| Product Hunt | Needs a personal account, onboarding and a launch; marketing outreach is out of scope |

## Other (insufficient evidence)

- **KaOS KCP**: moving from GitHub to Codeberg; acceptance details missing.
- **makedeb MPR**: the official site shows an unmaintained banner; upload process unverified.
- **SaaSHub, Slant, OpenHub, Mageia**: the policy pages could not be fetched (HTTP 502/526/403 or a bot challenge). Treat that as unknown, not as incompatible or as proof Krema is not listed.
- **pkgs.org**: a package index, not a way to publish an application directly.
