---
name: release
description: Release a new Krema version end to end — version bump, release commit, distro E2E gate, tag, GitHub release notes, then AUR, OBS, COPR and Launchpad PPA uploads. Use when the user asks to release, cut a version, or publish (릴리즈, 버전 올려).
---

# Release Krema

Optional argument: an explicit version (`x.y.z`). Otherwise the version is derived from `CHANGELOG.md`.

Which distro releases get packaged follows the Distribution Support Policy in `AGENTS.md`: never drop a target because it is EOL.

## 1. Pre-flight

Abort with a clear message if any of these fail:

- The working tree has no uncommitted tracked changes, and the current branch is `master`, up to date with `origin/master`.
- `## [Unreleased]` in `CHANGELOG.md` has at least one entry.

Then check which upload channels are usable. Report every channel's state, skip the unusable ones, and wait for the user to confirm before continuing.

| Channel | Ready when |
|---|---|
| AUR | `git remote get-url aur` works (add it with `git remote add aur ssh://aur@aur.archlinux.org/krema.git`) |
| OBS | `osc api /about` succeeds (`osc` may run as `uvx --from osc osc`) |
| COPR | `copr-cli whoami` succeeds |
| PPA | `dput` is installed, `dpkg-buildpackage` (with `devscripts` and `debhelper`) or Docker is available, and Launchpad has a GPG key for `~isac322` (see below) |

```bash
keys=$(curl -s https://api.launchpad.net/devel/~isac322 | jq -r .gpg_keys_collection_link)
LP_GPG_KEY=$(curl -s "$keys" | jq -r '.entries[0].fingerprint // empty')
```

## 2. Version

Read the current version from `project(krema VERSION x.y.z ...)` in `CMakeLists.txt`. Unless the user gave one, derive the bump from the `[Unreleased]` categories:

| `[Unreleased]` contains | Bump |
|---|---|
| `### Removed`, or an entry marked BREAKING | major |
| `### Added` | minor |
| only `### Fixed` / `### Changed` | patch |

Show the category counts and the proposed version, and wait for confirmation. Then make sure `git tag -l vx.y.z` is empty.

## 3. Release commit

Read each file before editing it. `PKGBUILD` is updated later (step 6), after the GitHub tarball exists.

- `CMakeLists.txt`: `project(krema VERSION x.y.z ...)`.
- `packaging/obs/krema.spec`: `Version:`.
- `packaging/obs/krema.dsc`: `Version:`, `DEBTRANSFORM-TAR:` and `Files:`. The tarball is `krema-x.y.z.tar.gz`, the name OBS `_service` (tar_scm, `@PARENT_TAG@`) generates.
- `packaging/obs/debian.changelog`: prepend an entry. Copy the maintainer line from the existing entries.
  ```
  krema (x.y.z-1) unstable; urgency=medium

    * New upstream release vx.y.z
    * <key items from CHANGELOG.md>

   -- <maintainer from existing entries>  <RFC 2822 date>
  ```
- Dependency audit: compare `find_package()` in `CMakeLists.txt` with `BuildRequires` in `krema.spec` and `Build-Depends` in `debian.control`. Fix mismatches before continuing.
- `CHANGELOG.md`: move every `[Unreleased]` entry into `## [x.y.z] - YYYY-MM-DD` directly below it, keeping the categories. Leave `## [Unreleased]` empty, without category headers.
- `ROADMAP.md`: verify the current (⬅️) milestone's items against the code and tick what is implemented. If the milestone is complete, mark it ✅ and move ⬅️ to the next one.
- `src/com.bhyoo.krema.metainfo.xml`: add a `<release>` at the top of `<releases>`. Never skip this.
  ```xml
  <release version="x.y.z" date="YYYY-MM-DD">
    <description>
      <p>One or two English sentences on the user-facing value.</p>
    </description>
  </release>
  ```

```bash
git add CMakeLists.txt CHANGELOG.md ROADMAP.md src/com.bhyoo.krema.metainfo.xml \
  packaging/obs/krema.spec packaging/obs/krema.dsc packaging/obs/debian.changelog
git commit -m "chore: release vx.y.z"
git push
```

## 4. Distro E2E gate, then tag

The distro E2E matrix does not run on master pushes. Run it on the pushed release commit and tag only if it passes:

```bash
gh workflow run distro-e2e.yml --ref master
sleep 5
run_id=$(gh run list --workflow distro-e2e.yml --branch master --event workflow_dispatch \
  --commit "$(git rev-parse HEAD)" --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch "$run_id" --exit-status
```

If it fails, fix it on master with a new commit and rerun the gate. Never tag a commit whose distro E2E is red.

```bash
git tag vx.y.z
git push origin vx.y.z
```

## 5. GitHub release

Search engines index release pages, so write them for users. Read the keyword tiers in `marketing/strategy.md` and the previous release (`gh release view <previous tag>`) for style.

```markdown
![Krema release notes](https://raw.githubusercontent.com/isac322/krema/master/branding/social/release-banner.png)

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

Show the notes to the user and create the release only after approval:

```bash
gh release create vx.y.z --title "vx.y.z" --notes-file <notes.md>
```

## 6. PKGBUILD and AUR

Do this only after the GitHub release exists, in one commit.

1. In `packaging/arch/PKGBUILD`, set `pkgver=x.y.z`, and compare `find_package()` (`CMakeLists.txt`) and `target_link_libraries()` (`src/CMakeLists.txt`) with `depends`/`makedepends`. Fix any mismatches.
2. Set `sha256sums` to the hash of the release tarball:
   ```bash
   curl -sL https://github.com/isac322/krema/archive/vx.y.z.tar.gz | sha256sum
   ```
3. Regenerate `.SRCINFO`; never edit it by hand. `makepkg` needs an Arch host or an `archlinux` container.
   ```bash
   (cd packaging/arch && makepkg --printsrcinfo > .SRCINFO)
   ```
4. Commit, push, and push the subtree to AUR:
   ```bash
   git add packaging/arch/PKGBUILD packaging/arch/.SRCINFO
   git commit -m "chore: update PKGBUILD and .SRCINFO for vx.y.z"
   git push
   git subtree push --prefix=packaging/arch aur master
   ```

## 7. OBS

`_service` tracks `master` with `@PARENT_TAG@`, so uploading the package files starts a build of the new tag. `project.config`, `project.meta.xml` and `apply-project-config.sh` configure the OBS project and must not be uploaded as package sources.

```bash
osc checkout home:isac322/krema
cp packaging/obs/{_service,krema.spec,krema.dsc,debian.changelog,debian.control,debian.rules,debian.copyright} home:isac322/krema/
(cd home:isac322/krema && osc addremove && osc commit -m "Update to vx.y.z")
rm -rf home:isac322
```

Confirm the builds started at https://build.opensuse.org/package/show/home:isac322/krema.

## 8. COPR

```bash
copr-cli edit-package-scm isac322/krema --name krema \
  --clone-url https://github.com/isac322/krema.git --commit vx.y.z \
  --subdir packaging/obs --spec krema.spec
copr-cli build-package isac322/krema --name krema
```

COPR deletes EOL Fedora chroots on its own. Those releases keep building on OBS; don't remove their OBS target or README row.

## 9. Launchpad PPA

Upload one source package per active Ubuntu series that ships Qt ≥ 6.8, which means 25.10 or later. Query the series instead of hardcoding them, because Launchpad rejects uploads to Obsolete series:

```bash
curl -s https://api.launchpad.net/devel/ubuntu/series | jq -r '.entries[] | select(.active
  and (.status == "Current Stable Release" or .status == "Supported"
       or .status == "Active Development" or .status == "Pre-release Freeze")
  and (.version | tonumber) >= 25.10) | "\(.name) \(.version)"'
```

The package version is `x.y.z-<debrev>~ppa1~<series>1`. `<debrev>` is the Debian revision of the top entry in `packaging/obs/debian.changelog`: `1` for a new upstream release, or the bumped revision for a packaging-only respin. Launchpad rejects versions that are not newer than the published one.

Write the source-package build once, then run it natively on a Debian/Ubuntu host that has `dpkg-dev`, `devscripts` and `debhelper`, or in Docker anywhere else. Run it once per series; each run rewrites `debian/changelog` for that series.

```bash
PPA_DIR=$(mktemp -d)
curl -sL https://github.com/isac322/krema/archive/vx.y.z.tar.gz -o "$PPA_DIR/krema_x.y.z.orig.tar.gz"
cat > "$PPA_DIR/build-source.sh" <<'EOF'
set -e
cd "$OUT" && rm -rf krema-x.y.z && tar xzf krema_x.y.z.orig.tar.gz && cd krema-x.y.z
mkdir -p debian/source && echo "3.0 (quilt)" > debian/source/format
cp "$SRC/packaging/obs/debian.control" debian/control
cp "$SRC/packaging/obs/debian.rules" debian/rules
cp "$SRC/packaging/obs/debian.copyright" debian/copyright
chmod +x debian/rules
cat > debian/changelog <<CHLOG
krema (x.y.z-<debrev>~ppa1~<series>1) <series>; urgency=medium

  * <top entry bullets from packaging/obs/debian.changelog>

 -- <maintainer from packaging/obs/debian.changelog>  <RFC 2822 date>
CHLOG
dpkg-buildpackage -S -us -uc -d
EOF

# Native:
SRC="$PWD" OUT="$PPA_DIR" bash "$PPA_DIR/build-source.sh"

# Docker:
docker run --rm -v "$PWD:/src:ro" -v "$PPA_DIR:/output" -e SRC=/src -e OUT=/output ubuntu:<series> bash -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq dpkg-dev devscripts debhelper >/dev/null
  bash /output/build-source.sh
  chmod 666 /output/krema_x.y.z*'
```

In Docker, the `chmod 666` lets the host user sign the root-owned output. Sign with `debsign -k "$LP_GPG_KEY" <changes>`. Without `debsign`, clearsign the `.dsc` first, then update its checksums and size in `.changes`, then clearsign `.changes`. Upload with `dput ppa:isac322/krema <changes>`; the `.orig.tar.gz`, `.debian.tar.xz`, `.dsc`, `_source.buildinfo` and `_source.changes` must all be present. Remove `$PPA_DIR` afterwards.

## 10. Summary

Report the version, the two commits, the GitHub release URL, and each channel's result (uploaded, skipped with the reason, or failed with the error), with links:

- https://copr.fedorainfracloud.org/coprs/isac322/krema/builds/
- https://build.opensuse.org/package/show/home:isac322/krema
- https://launchpad.net/~isac322/+archive/ubuntu/krema
