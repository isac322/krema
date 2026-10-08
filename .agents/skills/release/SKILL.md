---
name: release
description: Release a new Krema version end to end when the user asks — version bump, release commit, distro E2E gate, signed tag, then GitHub, AUR, OBS, COPR and Launchpad PPA publication run by the coding agent with the release CLI, plus browser handoffs for KDE Store and AlternativeTo. Use when the user asks to release, cut a version, or publish (릴리즈, 버전 올려).
---

# Release Krema

Optional argument: an explicit version (`x.y.z`). Otherwise the version is derived from `CHANGELOG.md`.

Which distro releases get packaged follows the Distribution Support Policy in `AGENTS.md`: never drop a target because it is EOL.

A release runs only when the user explicitly asks for it, and the coding agent drives every step in the current session with the CLI below. It has two halves with a hard boundary between them:

1. **Source release.** Version bump, release commit, the full distro E2E matrix, and a signed tag pushed to GitHub. Pushing a tag does not run this release workflow.
2. **Publication.** For the version the request names, the agent verifies the signed tag, confirms the full Distro E2E pass on the tagged commit, and publishes every native channel with `release.py publish --execute`. Browser-only stores get a prepared packet and a handoff; they are reported as published only after a public read confirms it.

No installed worker, scheduler, tag watcher or background retry is involved, and merging a pull request does not trigger a release either: every publication step needs the explicit invocation in this session. The agent publishes only the version covered by the current request.

## Tools

All tools are Python standard-library scripts in `scripts/`. The agent runs them from the release checkout; they need no installation and leave no process running. Except for the two `Writes externally` rows (`gate --execute`, which only dispatches the existing workflow, and `publish --execute`), every command is a dry run: it reads remote state and writes only inside its output or work directory. `release.py publish` without `--execute` prints the plan and sends nothing; that distinction is what keeps a no-execute dry run reproducible against an already published tag. The tooling itself, the submission schemas and the local packaging under `packaging/` are repository tooling and metadata; changes to them do not touch application runtime code, CI workflows or deployment configuration.

| Command | Purpose | Writes externally |
|---|---|---|
| `python3 scripts/release.py prepare --tag vX.Y.Z --output DIR` | Verify the tag and source metadata; write a source directory, `krema-X.Y.Z.tar.gz`, `notes.md` and `context.json` under `DIR` (outside the repository) | No |
| `python3 scripts/release.py gate --context DIR/context.json` | Inspect Distro E2E for the tagged commit; reports `ready`, `pending`, `failed` or `blocked` | No |
| `python3 scripts/release.py gate --context DIR/context.json --execute` | Same, and dispatch the existing `distro-e2e.yml` once if no full run for that commit exists. Never publishes | Workflow dispatch only |
| `python3 scripts/release.py status --context DIR/context.json [--channel C]` | Read-only per-channel publication report | No |
| `python3 scripts/release.py publish --context DIR/context.json --channel C` | Plan only: show what would be published | No |
| `python3 scripts/release.py publish --context DIR/context.json --channel C --execute` | Publish. `C` is `github`, `aur`, `obs`, `copr`, `ppa` or `all` | Yes |
| `python3 scripts/release_stores.py prepare --context DIR/context.json --output DIR2` | Build this version's KDE Store / AlternativeTo / Launchpad packet in `DIR2/kit`, plus `DIR2/stores.json` and `DIR2/BROWSER-TASKS.md` | No |
| `python3 scripts/release_stores.py status --context DIR/context.json [--store S]` | Read-only status; `S` is `kde-store`, `alternativeto`, `launchpad`, `github`, `awesome-kde`, `awesome-wayland`, `nixpkgs` or `linuxlinks` | No |
| `python3 scripts/release_stores.py record --context DIR/context.json [--packet DIR2] --store kde-store\|alternativeto --state start\|pending\|published\|abort [--reference R]` | Take or release the local lock for one browser task and write its receipt in `work_dir/store-receipts/`. `--packet` is required except for `abort` | No |

`--repository` defaults to `isac322/krema`; `--repo-root` defaults to the current checkout. `prepare`, `gate` and `publish` accept `--signer FINGERPRINT` (repeatable) for the allowed tag signers; always pass the pinned `AC48F8F7708DDBA12D959C1FCF1251E9D42EF5B9`. `context.json` (schema version 1) holds `repository`, `tag`, `version`, `commit`, `source_dir`, `archive`, `sha256`, `source_url`, `work_dir`, `notes_path` and `debian_revision`. It never contains credentials. Every later command re-validates it against the tag, commit and archive hash, so a stale or edited context fails instead of publishing the wrong bytes. `notes.md` is checked the same way: it must equal the text generated from the tagged CHANGELOG section byte for byte, so an edited `notes.md` fails with `notes_path does not match` or `notes changed after context validation`.

The archive recorded in a context is the byte sequence every channel later checks against `source_url`. For a tag with no published GitHub release (none, or only a draft), `prepare` builds it deterministically from the signed tag and reports `archive_origin` `generated`. For a tag whose release is already published, `prepare` reuses the source bytes GitHub already serves for that tag: the uploaded `krema-X.Y.Z.tar.gz` asset when the release carries one (`published`; the named asset always wins), otherwise the tag's automatic source archive from `https://github.com/OWNER/REPO/archive/refs/tags/vX.Y.Z.tar.gz` (`autoarchive`), downloaded anonymously over HTTPS. Reused bytes are trusted only when the archive's recorded commit equals the signed tag commit, it has a single safe `krema-X.Y.Z/` root, and its full file manifest (paths, SHA-256, executable bits, symlinks) equals the deterministic archive of that commit; any mismatch fails `prepare` with no fallback. Reuse keeps an already published version's checksums identical across reruns instead of recompressing to different bytes, which is what lets the AUR, OBS, PPA and KDE Store compare the context against what they already hold. `source_url` stays the named release asset URL in every case. A new or draft-only tag always gets the generated archive; this tooling never substitutes other bytes for a genuinely new release.

Every channel result uses one status:

| Status | Meaning |
|---|---|
| `planned` | Dry run: this would be published with `--execute` |
| `already_published` | The exact version is already live; nothing was sent |
| `submitted` | Accepted by the service, build not finished (COPR/OBS/PPA queues) |
| `published` | Exact version verified live |
| `pending` | Waiting on an external gate (Distro E2E, a build, moderation) |
| `blocked` | A prerequisite is missing: credential locked or absent, tool unavailable, platform rejects the target |
| `failed` | The attempt ran and failed; the detail says why |
| `browser_required` | Needs an authenticated browser session; see the browser playbook |
| `not_applicable` | The channel has nothing to do for this release |

`submitted` is not `published`. A COPR or PPA acknowledgement says nothing about whether the builds succeed; check `status` again before reporting the channel done.

## 1. Pre-flight

Abort with a clear message if any of these fail:

- The working tree has no uncommitted tracked changes, and the current branch is `master`, up to date with `origin/master`.
- `## [Unreleased]` in `CHANGELOG.md` has at least one entry.

Then check the local credentials the release CLI uses; there are no CI secrets. Report every channel's state. If all are ready, continue without asking. If any is not, name the missing credential or tool and ask the user to restore it; never substitute another account, key or token. Leaving a channel out of the release is a scope change that needs the user's explicit go-ahead. A channel that is not ready ends up `blocked`, not skipped silently.

| Channel | Ready when |
|---|---|
| Tag signing | `gpg --batch --pinentry-mode error --list-secret-keys AC48F8F7708DDBA12D959C1FCF1251E9D42EF5B9` lists the key, and a test signature succeeds without a prompt (below) |
| GitHub | `gh auth status` shows `isac322` |
| AUR | `ssh -o BatchMode=yes -o StrictHostKeyChecking=yes aur@aur.archlinux.org help` succeeds |
| OBS | `osc api /about` succeeds (`osc` may run as `uvx --from osc osc`) |
| COPR | `copr-cli whoami` prints `isac322` |
| PPA | Docker can run `linux/arm64` or `linux/amd64` Ubuntu images (or the host has `dpkg-dev`, `devscripts`, `debhelper`), and the signing key above is the one Launchpad has for `~isac322` (below) |

```bash
KEY=AC48F8F7708DDBA12D959C1FCF1251E9D42EF5B9
echo ok | gpg --batch --pinentry-mode error --local-user "$KEY" --clearsign >/dev/null && echo "signing ok"
keys=$(curl -s https://api.launchpad.net/devel/~isac322 | jq -r .gpg_keys_collection_link)
curl -s "$keys" | jq -r '.entries[].fingerprint' | grep -qx "$KEY" && echo "Launchpad key matches"
```

If the signature fails with a pinentry error, the agent cache is locked: ask the user to unlock it once in a terminal. Never switch to another key.

### Credentials

No passwords, tokens or private keys are stored in this skill, the scripts or the repository, and none are exported into the checkout or into CI: there are no CI secrets at all. At execution time the tools use only the local stores that already exist on the machine: the authenticated `gh` CLI for GitHub, the SSH agent or key for AUR, the `osc` configuration for OBS, the `copr-cli` configuration for COPR, the local GPG keyring for tag and source signing, and the existing browser sessions for the store handoffs. Account names, the repository owner, the PPA name and the signer fingerprint `AC48F8F7708DDBA12D959C1FCF1251E9D42EF5B9` are public identifiers, not secrets. When a store is locked or missing, the channel is `blocked`; the agent names what is missing and never substitutes another credential.

## 2. Version

Read the current version from `project(krema VERSION x.y.z ...)` in `CMakeLists.txt`. Unless the user gave one, derive the bump from the `[Unreleased]` categories:

| `[Unreleased]` contains | Bump |
|---|---|
| `### Removed`, or an entry marked BREAKING | major |
| `### Added` | minor |
| only `### Fixed` / `### Changed` | patch |

If the user named the version, use it. Otherwise show the category counts and the proposed version, and wait for confirmation: the version defines the release scope. Then make sure `git tag -l vx.y.z` is empty and `gh release view vx.y.z` fails.

## 3. Release commit

Version bumps stay manual: the tooling only reads them, and `prepare` rejects a tag whose files disagree. Read each file before editing it.

- `CMakeLists.txt`: `project(krema VERSION x.y.z ...)`.
- `packaging/obs/krema.spec`: `Version:`.
- `packaging/obs/krema.dsc`: `Version:`, `DEBTRANSFORM-TAR:` and `Files:`. The tarball is `krema-x.y.z.tar.gz`.
- `packaging/obs/debian.changelog`: prepend an entry. Copy the maintainer line from the existing entries. Its Debian revision (`-1` for a new upstream version) becomes `debian_revision` in the context and the PPA version.
  ```
  krema (x.y.z-1) unstable; urgency=medium

    * New upstream release vx.y.z
    * <key items from CHANGELOG.md>

   -- <maintainer from existing entries>  <RFC 2822 date>
  ```
- Dependency audit: compare `find_package()` in `CMakeLists.txt` and `target_link_libraries()` in `src/CMakeLists.txt` with `BuildRequires` in `krema.spec`, `Build-Depends` in `debian.control`, and `depends`/`makedepends` in `packaging/arch/PKGBUILD`. Fix mismatches before continuing.
- `packaging/arch/PKGBUILD`: leave `pkgver` and `sha256sums` alone. The AUR adapter copies the tagged PKGBUILD to a scratch directory and sets both from the context, so the repository copy may lag one version behind; that is expected. There is no second "update PKGBUILD" commit any more.
- `CHANGELOG.md`: move every `[Unreleased]` entry into `## [x.y.z] - YYYY-MM-DD` directly below it, keeping the categories, and put the one- or two-sentence lead paragraph directly under that heading, before the first `###`. Leave `## [Unreleased]` empty, without category headers. This section becomes the GitHub release notes verbatim (section 5), so write it to those rules now.
- `ROADMAP.md`: verify the current (⬅️) milestone's items against the code and tick what is implemented. If the milestone is complete, mark it ✅ and move ⬅️ to the next one.
- `src/com.bhyoo.krema.metainfo.xml`: add a `<release>` at the top of `<releases>`. Never skip this.
  ```xml
  <release version="x.y.z" date="YYYY-MM-DD">
    <description>
      <p>One or two English sentences on the user-facing value.</p>
    </description>
  </release>
  ```

`prepare` checks that CMake, the CHANGELOG section, metainfo, `krema.spec`, `krema.dsc` and `debian.changelog` all name the tag's version; any mismatch stops the release.

```bash
git add CMakeLists.txt CHANGELOG.md ROADMAP.md src/com.bhyoo.krema.metainfo.xml \
  packaging/obs/krema.spec packaging/obs/krema.dsc packaging/obs/debian.changelog
git commit -m "chore: release vx.y.z"
git push
```

## 4. Distro E2E gate, then the signed tag

The distro E2E matrix does not run on master pushes. Run the full matrix on the pushed release commit and tag only if every job passes:

```bash
gh workflow run distro-e2e.yml --ref master -f targets=all
sleep 5
run_id=$(gh run list --workflow distro-e2e.yml --branch master --event workflow_dispatch \
  --commit "$(git rev-parse HEAD)" --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch "$run_id" --exit-status
```

Always dispatch `targets=all`. `release.py gate` derives the expected job set from the tagged `tests/distro/targets.tsv` plus `arch` and requires every one of those jobs to have succeeded on the exact commit; a subset run, a skipped or cancelled job, or a run on another SHA does not count. If it fails, fix it on master with a new commit, redo the release commit checks if versions moved, and rerun the gate. Never tag a commit whose distro E2E is red.

Then create a signed, annotated tag with the pinned key. `prepare`, `gate` and `publish` refuse unsigned tags and tags signed by any key other than the `--signer` fingerprints, even one the local keyring trusts.

```bash
git tag -s -u AC48F8F7708DDBA12D959C1FCF1251E9D42EF5B9 -m "Krema vx.y.z" vx.y.z
git tag -v vx.y.z
```

Pushing the tag completes the source step; publication through the release CLI still requires the explicit invocation in section 6. Only push it once the notes in the CHANGELOG section are final (section 5).

```bash
git push origin vx.y.z
```

Never move or re-push a pushed tag. Every command re-checks the tag against the context commit, so a tag that later points elsewhere fails closed for every channel.

## 5. GitHub release notes

Search engines index release pages, so write them for users. `prepare` writes `notes.md` as the release banner (when the tagged tree has `branding/social/release-banner.png`) followed by the tagged CHANGELOG `## [x.y.z]` section body, verbatim. The lead sentence is the paragraph directly under that heading, before the first `###`; `prepare` does not write one for you. So write the CHANGELOG section to these rules in section 3. Read the keyword tiers in `marketing/strategy.md` and the previous release (`gh release view <previous tag>`) for style.

The resulting `notes.md`:

```markdown
![Krema release notes](https://media.githubusercontent.com/media/isac322/krema/master/branding/social/release-banner.png)

Krema vx.y.z <one or two sentences on the user-facing value, with primary keywords such as "KDE Plasma 6 dock", "Wayland", "parabolic zoom">.

### Added
- <user benefit first> — <technical detail if useful>

### Changed
- ...

### Fixed
- ...
```

- Use the same categories as the CHANGELOG section, and omit empty ones.
- Lead with value: write "Live window previews on hover", not "Implemented PipeWire stream capture".
- Rephrase internal work as a user benefit, or leave it out. Never list CI or agent configuration.
- Refer to Latte Dock only as the "spiritual successor", never as a "replacement", "fork" or "clone".
- Write in English and past tense, and don't stuff keywords.

The banner is a Git LFS file, so it is linked through `media.githubusercontent.com`; `raw.githubusercontent.com` would serve the LFS pointer.

Before pushing the tag, show the user the notes and get approval. Render them from the local signed tag without publishing anything:

```bash
python3 scripts/release.py prepare --tag vx.y.z --output "$(mktemp -d)/vx.y.z" \
  --signer AC48F8F7708DDBA12D959C1FCF1251E9D42EF5B9
# then show <output>/notes.md
```

`notes.md` is generated, not edited: `publish` refuses a `notes.md` that differs from the tagged CHANGELOG section. It is used only when the GitHub release does not exist yet; an existing published release body is never changed. So fix wording before pushing the tag: delete the unpushed local tag (`git tag -d vx.y.z`), fix the CHANGELOG in a new commit, rerun the gate on it, sign the tag again and run `prepare` again.

## 6. Publication

Publish only the version the user's release request names. Tag pushes, existing tags and prepared contexts never authorize publication by themselves. Once the request covers publication, run this sequence without asking for confirmation between steps. Still follow every applicable approval guard, stop on any result the request does not cover, and report missing credentials by name.

`--execute` is the only switch that writes to external services. Run each `publish` without it first and show the plan. Add `--execute` only when the plan names this tag, commit, archive hash and the requested channels.

```bash
KEY=AC48F8F7708DDBA12D959C1FCF1251E9D42EF5B9
OUT=$(mktemp -d)/vx.y.z
python3 scripts/release.py prepare --tag vx.y.z --output "$OUT" --signer "$KEY"
CTX="$OUT/context.json"
python3 scripts/release.py gate --context "$CTX" --execute --signer "$KEY"
python3 scripts/release.py publish --context "$CTX" --channel github --signer "$KEY"
python3 scripts/release.py publish --context "$CTX" --channel github --execute --signer "$KEY"
python3 scripts/release.py publish --context "$CTX" --channel all --signer "$KEY"
python3 scripts/release.py publish --context "$CTX" --channel all --execute --signer "$KEY"
python3 scripts/release.py status --context "$CTX"
```

1. `prepare` verifies the tag signature against the pinned signer and the source metadata, then writes the archive, its hash and the notes into the context. The archive is generated deterministically for a new tag, or the published GitHub source bytes for a tag whose release already exists; both cases are described under Tools and never change what the tag points to. Use this one context for the rest of the release.
2. `gate --execute` reports `ready` when a full Distro E2E run on the tagged commit passed; normally that is the section 4 run. Without one, it dispatches `distro-e2e.yml --ref vX.Y.Z -f targets=all` once and records the dispatch in `gate-dispatch.json` in the context's work directory. If `gh` refuses the dispatch, the record is removed and the gate reports `failed`; a dispatched run that never shows up within an hour also turns the gate `failed`. Never publish on `pending`, `failed` or `blocked`.
3. `publish --channel github --execute` creates the GitHub release as a draft on the tagged commit with the notes, attaches the immutable `krema-X.Y.Z.tar.gz` and `SHA256SUMS`, then publishes the draft. When the tag's release is already published but lacks those assets, `--execute` only attaches them from the context; it never edits the existing release's title or body. The remote tag is checked against the context commit before the draft is created and again right before it is published; a mismatch leaves the draft unpublished and fails the channel. The other channels use that asset's URL and hash, so GitHub always goes first.
4. `publish --channel all --execute` reports GitHub as `already_published` and publishes `aur`, `obs`, `copr` and `ppa`. Each channel is independent: one failure does not stop the others, and a channel already `published`, `already_published` or `submitted` is not sent again.
5. `status` reports each channel. Report `submitted` as submitted, not published, until a later `status` shows the build finished.
6. Prepare the browser handoffs (section 7). The release is finished only when `release_stores.py status` shows KDE Store and AlternativeTo as `published`, `already_published` or `not_applicable`. Otherwise, report them as `pending` or `browser_required`. Static branding and the one-time catalogs are reported alongside but never hold a release open.

Waiting is part of the current request only. When the gate is `pending`, find the run ID in the `gate` output or with `gh run list --workflow distro-e2e.yml --commit <tagged commit>`, wait with `gh run watch <run_id> --exit-status`, then rerun `gate`. To wait for `submitted` builds, use one finite watcher: a bounded loop over `release.py status` with a deadline. When the deadline passes, report the current status. Never leave a polling loop, scheduled job or background retry behind; a later recheck needs a new user request.

To retry one channel, rerun `publish` for the same context with `--channel C`, plan first, then `--execute`. The tools compare against what is already live, so a repeat run reports `already_published` instead of uploading twice. Run only one release session per tag at a time.

### Standalone adapter commands (read-only)

The channel scripts also run on their own for inspection and dry runs. They cannot publish: their `publish` action only prints the plan. Real uploads go only through `release.py publish --channel C --execute`, which adds the tag, signer, Distro E2E gate and GitHub-asset checks.

```bash
python3 scripts/release_aur.py status  --context "$CTX"
python3 scripts/release_aur.py prepare --context "$CTX" --output "$(mktemp -d)"   # pinned PKGBUILD and .SRCINFO; no AUR contact
python3 scripts/release_aur.py publish --context "$CTX"                            # plan only
python3 scripts/release_ppa.py status  --context "$CTX"
python3 scripts/release_ppa.py prepare --context "$CTX"                            # build and sign source packages in scratch; no upload
python3 scripts/release_ppa.py publish --context "$CTX"                            # plan only
python3 scripts/release_rpm.py status-obs  --context "$CTX"
python3 scripts/release_rpm.py status-copr --context "$CTX"
python3 scripts/release_rpm.py prepare-obs --context "$CTX" --output "$(mktemp -d)/obs"   # output must not exist yet
```

`release_ppa.py prepare` takes `--series NAME` (repeatable) to limit the build to target series. It signs locally with the pinned key and uploads nothing.

Before building, the PPA adapter downloads any `krema_X.Y.Z.orig.tar.gz` the PPA already holds for that upstream version and compares it with the context's archive. Launchpad rejects a second orig tarball with different bytes, so a mismatch makes `prepare` and `publish` return `blocked` and nothing is built or sent. This check always runs; there is no switch to skip it, and the fix is never to repackage or re-version the orig. `release.py prepare` reuses the published GitHub source bytes when the tag already has a release, which is what keeps the check passing for versions that predate this tooling: v0.10.0 was published before the named asset existed, so its context archive is the tag's GitHub source archive, which is byte-identical to the orig the PPA already holds. `blocked` therefore means the upstream source genuinely differs, and the remedies are to use a context built from the published bytes or to release a new upstream version.

### Channel rules

These are enforced by the adapters; know them when reading a `blocked` or `failed` result.

- **GitHub** (`scripts/release_github.py`): creates the release for the existing tag as a draft, uploads `krema-X.Y.Z.tar.gz` plus `SHA256SUMS` to it, and publishes it after re-checking that the remote tag still points at the context commit. A retry resumes a draft whose notes and target commit match the context exactly (reported as `planned` before `--execute`); any other draft for the tag is `blocked` and is never edited or deleted, so publish or delete it by hand. If the published release or an asset already exists, its hash must match the context or the channel fails; when only assets are missing, `--execute` uploads them from the context without editing the release title, body or existing assets. GitHub has no compare-and-swap for tags, so a tag moved after the last check is detected only by the post-publication check, which fails the channel with `remote tag vX.Y.Z moved during publication`; stop and report it to the user as an integrity problem. The context's `source_url` is this asset (`https://github.com/isac322/krema/releases/download/vX.Y.Z/krema-X.Y.Z.tar.gz`), so `release.py` refuses `--execute` for any other channel until the asset exists with the matching hash.
- **AUR** (`scripts/release_aur.py`): stages the tagged `packaging/arch/PKGBUILD` in scratch, sets `pkgver`, the source URL and `sha256sums` from the context, regenerates `.SRCINFO` with `makepkg --printsrcinfo` (in an `archlinux` container on the Mac), and pushes to `ssh://aur@aur.archlinux.org/krema.git` over BatchMode SSH with strict host keys. It never edits the checkout and no longer uses `git subtree`. An identical remote state means `already_published`.
- **OBS** (`scripts/release_rpm.py`, `home:isac322/krema`): uploads only `_service`, `krema.spec`, `krema.dsc`, `debian.changelog`, `debian.control`, `debian.rules` and `debian.copyright` from the tag, with `_service`'s `revision` pinned to the tag instead of `master`. `project.config`, `project.meta.xml` and `apply-project-config.sh` configure the OBS project and are never uploaded, and no repository is removed. Uses the existing `osc` configuration through `uvx --from osc osc`.
- **COPR** (`scripts/release_rpm.py`, `isac322/krema`): points the SCM package `krema` at the tag (`packaging/obs/krema.spec`) and starts a build unless one for that version and tag already succeeded or is running. Never changes chroots or webhooks. COPR deletes EOL Fedora chroots on its own. Those releases keep building on OBS; don't remove their OBS target or README row.
- **Launchpad PPA** (`scripts/release_ppa.py`, `ppa:isac322/krema`): uploads one source package per active Ubuntu series that ships Qt ≥ 6.8 (25.10 or later), queried from Launchpad rather than hardcoded, because Launchpad rejects uploads to Obsolete series. The version is `x.y.z-<debrev>~ppa1~<series>1`, with `<debrev>` taken from the top `debian.changelog` entry. Sources are built in an Ubuntu container, signed with the pinned key (the private key never leaves the local keyring), and uploaded. A series that already has that exact version, pending or published, is skipped; the revision is never bumped automatically. A packaging-only respin needs a new `debian.changelog` revision and therefore a new release commit and tag.

A series or chroot the platform rejects is reported per channel as `blocked` with the platform's reason. That is a platform action under the Distribution Support Policy, not a reason to remove the target anywhere else.

## 7. Stores, branding and catalogs

Every channel Krema is already on, and what a version release needs there:

| Channel | Destination | Per-release action | Mode |
|---|---|---|---|
| GitHub Release | https://github.com/isac322/krema/releases | Release, source asset, `SHA256SUMS` | CLI (`release.py publish`) |
| AUR | https://aur.archlinux.org/packages/krema | New `PKGBUILD`/`.SRCINFO` | CLI (`release.py publish`) |
| OBS | https://build.opensuse.org/package/show/home:isac322/krema | Updated package sources | CLI (`release.py publish`) |
| COPR | https://copr.fedorainfracloud.org/coprs/isac322/krema/ | Build of the tag | CLI (`release.py publish`) |
| Launchpad PPA | https://launchpad.net/~isac322/+archive/ubuntu/krema | Signed source upload per series | CLI (`release.py publish`) |
| KDE Store (mirrored on OpenDesktop, Pling, LinuxApps.org) | https://store.kde.org/p/2376676 | Update the version and source download on the existing listing | `browser_required` until the public OCS record shows this version and the archive's MD5 |
| AlternativeTo | https://alternativeto.net/software/krema/ | Make the existing listing's description match this packet; most releases change nothing there | `browser_required` until a public read shows the packet's description and the Latte Dock relationship |
| Launchpad project branding | https://launchpad.net/krema | None. Icon, logo and brand image are static | Status only |
| GitHub repository branding | Social preview and About section | None. Static | Status only |
| Awesome KDE | https://github.com/francoism90/awesome-kde/pull/24 (open) | None. One-time list entry waiting on the maintainer | Status only |
| Awesome Wayland | https://github.com/rcalixte/awesome-wayland/pull/109 (open) | None. One-time list entry waiting on the maintainer | Status only |
| Nixpkgs | https://github.com/NixOS/nixpkgs/pull/570731 (open) | None from this tooling. Version updates after merge go through nixpkgs' own update process | Status only |
| LinuxLinks | https://www.linuxlinks.com/ | None. Submitted 2026-10-06 with a correction request about its AI-use answer; listing and correction not yet verified | Status only |
| Repology | https://repology.org/project/krema/versions | None. Indexes the AUR by itself (other repositories not yet established) | No action; not covered by `release_stores.py` |
| Flathub | — | None. Blocked by Flathub's AI policy (`packaging/submissions/flathub-research.md`); never author or submit a manifest or PR | Blocked |

Static branding is not re-uploaded and catalog entries are not re-registered on version bumps. Only the version-specific packet (version number, source archive from the context, hash, changelog text) is regenerated. The schemas in the tagged `packaging/submissions/` use version tokens that the kit fills from the context, so a normal release needs no schema edit. If the tagged tree lacks them, the kit falls back to the installed copies and sets `human_review_required` in `kit.json`; have a person confirm the feature claims before using that packet. After the browser work, record the outcome with `release_stores.py record` (table above).

Tagged media are Git LFS files, so the extracted source tree holds pointers. The kit resolves each referenced pointer into a separate stage, `work_dir/submission-kit-lfs/`, fetching the object from the tagged commit on `media.githubusercontent.com` without credentials and checking it against the pointer's SHA-256 and size. Only verified bytes are copied into the packet; the source tree and the context archive stay untouched, and any missing or mismatching object stops the kit. `release_stores.py prepare` is still a dry run as defined under Tools: it writes only inside its output and work directories and sends nothing to any store.

```bash
STORES=$(mktemp -d)/stores-vx.y.z
python3 scripts/release_stores.py prepare --context "$CTX" --output "$STORES"
python3 scripts/release_stores.py status --context "$CTX"
```

Hand `$STORES/BROWSER-TASKS.md` and `$STORES/kit/` to the browser playbook. Every task in it is `browser_required`; the one-time catalogs are listed with their status, not resubmitted.

KDE Store and AlternativeTo have no stable write API we can use, so their updates are browser work: follow [`references/browser-publication.md`](references/browser-publication.md) with the prepared packet. It uses the existing logged-in sessions; never export session cookies or invent an API.

The browser work goes through the same CLI state and receipts, which are bound to the tag, commit, source hash and packet content:

```bash
python3 scripts/release_stores.py record --context "$CTX" --packet "$STORES" --store kde-store --state start
# browser update per the playbook, then one of:
python3 scripts/release_stores.py record --context "$CTX" --packet "$STORES" --store kde-store --state published
python3 scripts/release_stores.py record --context "$CTX" --packet "$STORES" --store kde-store --state pending --reference "$MODERATION_REF"
python3 scripts/release_stores.py record --context "$CTX" --store kde-store --state abort
```

`MODERATION_REF` is the submission or moderation reference the store showed. The same commands apply with `--store alternativeto`.

- `start` takes the lock. It is refused while another session holds the lock, while a `pending` receipt exists, or when this exact packet content is already recorded as published. Every call except `abort` first re-verifies the whole packet against its `SHA256SUMS`, `kit.json` and the context.
- `pending` needs `--reference`. It needs the lock or an existing pending receipt for the same content, writes the pending receipt and releases the lock. Repeating it replaces the reference and keeps the history.
- `published` needs the lock or a pending receipt for the same content, then reads the public store before writing anything. KDE Store: the OCS record must show this version and a file whose MD5 matches the archive. AlternativeTo: the listing must return HTTP 200 for the existing item, contain every description paragraph from the packet, and the Latte Dock page must link to `/software/krema/`. On any mismatch it refuses. If AlternativeTo answers 403, it refuses too; record `pending` instead. `--reference` here accepts only the canonical listing URL, which is also the default.
- `abort` releases a lock taken for this tag, commit, source hash and store. Without a lock, it withdraws a pending receipt and then requires `--reference` with the evidence for the withdrawal; after that `start` is allowed again.

`release_stores.py status` reads the same receipts. A held lock is `pending`. On the KDE Store, a live file with this release's filename but a different MD5 is `browser_required`, even while a receipt is pending. Otherwise a pending receipt is `pending`, and a public listing that matches this release is `already_published`. For the KDE Store, a verified `published` receipt also counts during OCS caching lag. For AlternativeTo, it counts only when the public page answers 403 and the receipt's description hash matches the packet. Everything else is `browser_required`. The listing merely existing never counts as published, and neither does a receipt the public read did not verify.

New channels are not registered as part of a release. Candidate channels and their policy blockers are in [`references/channel-research.md`](references/channel-research.md); registering any of them is a separate task that needs the user's go-ahead.

## 8. Summary

Report the version, the release commit, the tag's commit and signer, the Distro E2E run URL, and each channel's status from `release.py status` and `release_stores.py status`, with the detail for anything other than `published`/`already_published`. Link:

- https://github.com/isac322/krema/releases/tag/vx.y.z
- https://aur.archlinux.org/packages/krema
- https://copr.fedorainfracloud.org/coprs/isac322/krema/builds/
- https://build.opensuse.org/package/show/home:isac322/krema
- https://launchpad.net/~isac322/+archive/ubuntu/krema
- https://store.kde.org/p/2376676 (state whether the browser update is done, pending moderation, or still `browser_required`)
