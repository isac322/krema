Release a new version of Krema. Reads CHANGELOG.md [Unreleased] to auto-determine the semver bump, then executes the full release pipeline. Use whenever the user says "릴리즈", "release", "버전 올려", or wants to publish a new version.

Usage:
- `/release` — auto-detect version from CHANGELOG categories
- `/release 1.0.0` — explicit version override

---

## Step 1: Pre-flight Checks

Abort with a clear message if any check fails.

1. **Clean working tree**: `git status` — no uncommitted tracked changes (untracked OK)
2. **On master branch**: `git branch --show-current`
3. **Unreleased has content**: CHANGELOG.md `## [Unreleased]` must have at least one entry. Empty → abort ("nothing to release")
4. **Tag doesn't exist** (checked after version is determined)
5. **Multi-distribution tooling**: Detect which deployment channels are available. Report all, proceed with what's available.

```bash
AUR_READY=yes  # Always available (git subtree push)

# OBS: check both installed AND authenticated
OBS_READY=no
if command -v osc >/dev/null 2>&1; then
  osc api /about >/dev/null 2>&1 && OBS_READY=yes || OBS_REASON="osc installed but not authenticated (run: osc ls)"
else
  OBS_REASON="osc not installed (pip install osc)"
fi

# COPR: check both installed AND authenticated
COPR_READY=no
if command -v copr-cli >/dev/null 2>&1; then
  copr-cli whoami >/dev/null 2>&1 && COPR_READY=yes || COPR_REASON="copr-cli installed but not authenticated (run: copr-cli whoami)"
else
  COPR_REASON="copr-cli not installed (pip install copr-cli)"
fi

# PPA: needs dput + (dpkg-buildpackage OR Docker)
DPUT_READY=$(command -v dput >/dev/null 2>&1 && echo yes || echo no)
DPKG_READY=$(command -v dpkg-buildpackage >/dev/null 2>&1 && echo yes || echo no)
DOCKER_READY=$(docker info >/dev/null 2>&1 && echo yes || echo no)
PPA_READY=no
if [ "$DPUT_READY" = yes ] && { [ "$DPKG_READY" = yes ] || [ "$DOCKER_READY" = yes ]; }; then
  PPA_READY=yes
fi

# GPG key for PPA: query Launchpad API for the registered fingerprint
if [ "$PPA_READY" = yes ]; then
  LP_GPG_FINGERPRINT=$(curl -s "https://api.launchpad.net/devel/~isac322" \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('gpg_keys_collection_link',''))" 2>/dev/null)
  # Then fetch the actual fingerprint from that collection
  LP_GPG_KEY=$(curl -s "$LP_GPG_FINGERPRINT" \
    | python3 -c "import sys,json; entries=json.load(sys.stdin).get('entries',[]); print(entries[0]['fingerprint'] if entries else '')" 2>/dev/null)
  if [ -z "$LP_GPG_KEY" ]; then
    PPA_READY=no
    PPA_REASON="GPG key not found on Launchpad"
  fi
fi
```

Report:
```
배포 채널 사전 검사:
  AUR:  ✅ ready
  OBS:  ✅ ready / ❌ osc 미설치 (pip install osc)
  COPR: ✅ ready / ❌ copr-cli 미설치 (pip install copr-cli)
  PPA:  ✅ ready (GPG: <fingerprint>) / ❌ <reason>

❌ 채널은 릴리즈 후 건너뜁니다. 계속할까요?
```

Wait for user confirmation before proceeding.

## Step 2: Determine Version

Read the current version from `CMakeLists.txt` (`project(krema VERSION x.y.z ...)`).

If the user provided an explicit version, use that. Otherwise, read `CHANGELOG.md` `[Unreleased]` categories and apply semver:

| Categories present | Bump | Reason |
|---|---|---|
| `### Removed` or entry contains "BREAKING" | **major** | Breaking changes |
| `### Added` | **minor** | New features |
| Only `### Fixed` and/or `### Changed` | **patch** | Bug fixes / non-breaking changes |

Show the determination and ask for confirmation:

```
CHANGELOG [Unreleased] 분석:
  - Added: 3건
  - Fixed: 2건
  → minor bump 제안: 0.7.0 → 0.8.0

진행할까요? (다른 버전을 원하면 말씀해주세요)
```

Wait for user confirmation. After confirmed, verify `git tag -l v<version>` is empty.

## Step 3: Document Updates (release commit에 포함될 파일들)

Read each file before editing. PKGBUILD는 이 단계에서 수정하지 않는다 — GitHub release 후 별도 처리. OBS 패키징 파일은 이 단계에서 함께 수정한다 — 태그에 올바른 버전이 포함되어야 COPR/OBS가 정상 빌드된다.

### 3a. CMakeLists.txt
Update `project(krema VERSION x.y.z LANGUAGES CXX)`.

### 3a-1. packaging/obs/krema.spec
Update `Version:` field to the new version.

### 3a-2. packaging/obs/krema.dsc
Update `Version:` and `DEBTRANSFORM-TAR:` and `Files:` to match the new version. The tarball name must match what OBS `_service` (tar_scm) generates — it uses the `@PARENT_TAG@` format, so the tarball will be named `krema-<version>.tar.gz`.

### 3a-3. packaging/obs/debian.changelog
Prepend new entry at top. Extract maintainer name+email from existing entries — never hardcode.
```
krema (<version>-1) unstable; urgency=medium

  * New upstream release v<version>
  * <key items from CHANGELOG.md>

 -- <maintainer from existing entries>  <RFC 2822 date>
```

### 3a-4. Dependency sync audit
Compare `CMakeLists.txt` find_package() against `krema.spec` BuildRequires and `debian.control` Build-Depends. Report mismatches — fix before proceeding.

### 3b. CHANGELOG.md
- Move all entries from `## [Unreleased]` into `## [x.y.z] - YYYY-MM-DD` (today's date)
- Leave `## [Unreleased]` empty above the new version (no category headers)
- Preserve category structure (Added/Changed/Fixed/Removed)

### 3c. ROADMAP.md
- Find current milestone (⬅️)
- Cross-reference items against actual code (grep/read to verify)
- Check all implemented items; add missing sub-items if code has more than ROADMAP lists
- If all items done: ✅ the milestone, move ⬅️ to next
- If partial: update checkboxes, keep ⬅️

### 3d. metainfo.xml (`src/com.bhyoo.krema.metainfo.xml`)
Add `<release>` at TOP of `<releases>`:
```xml
<release version="x.y.z" date="YYYY-MM-DD">
  <description>
    <p>English summary, 1-2 sentences, user-facing value.</p>
  </description>
</release>
```
This was forgotten in v0.5.1 and caught in a later session. Never skip.

### 3e. work-state.md (`.claude/work-state.md`)
- Update 현재 마일스톤 to match ROADMAP
- Move completed items to 완료된 항목
- Update 다음 작업

## Step 4: Release Commit + Tag + Push

```bash
git add CMakeLists.txt CHANGELOG.md ROADMAP.md \
  src/com.bhyoo.krema.metainfo.xml .claude/work-state.md \
  packaging/obs/krema.spec packaging/obs/krema.dsc packaging/obs/debian.changelog

git diff --cached --stat   # verify staged changes

git commit -m "chore: release vx.y.z

Co-Authored-By: Claude <model-name> <noreply@anthropic.com>"

git tag vx.y.z
git push && git push --tags
```

## Step 5: GitHub Release

GitHub release pages are indexed by search engines — treat them as SEO and value communication opportunities.

### 5a. Gather context

1. Read `marketing/strategy.md` for current SEO keywords (Primary, Secondary, Long-tail)
2. Read `.claude/rules/documentation.md` § "GitHub Release Notes" and § "Latte Dock References"
3. Check previous release style for consistency:
```bash
gh release view $(git describe --tags --abbrev=0 HEAD~1) 2>/dev/null || gh release list --limit 1
```

### 5b. Compose release notes (template by release type)

**Minor/Major release template:**
```markdown
Krema vx.y.z — {1-2 sentence value summary with SEO keywords from marketing/strategy.md}.
{For first/major releases, add: "Krema is a lightweight dock for KDE Plasma 6, the spiritual successor to Latte Dock."}

### Added
- {User benefit first} — {technical detail}

### Changed
...

### Fixed
...

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

**Patch release template:**
```markdown
Krema vx.y.z — {one-line summary of what was fixed/improved}.

### Fixed
- {Detailed fix description, user-facing impact}

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

### 5c. SEO rules for release notes

- **Keyword source**: Read from `marketing/strategy.md` — never hardcode keywords
- **Lead with value**: "Live window previews on hover" not "Implemented PipeWire stream capture"
- **Latte Dock**: Only "spiritual successor" — never "replacement", "fork", or "clone"
- **Internal refactors**: Reframe as user benefit or omit entirely
- **Developer-only items** (CI, agent config): Omit from release notes
- **Language**: English, past tense, user-facing

### 5d. Present for review, then create

Show the composed release notes to the user for review. After approval:
```bash
gh release create vx.y.z --title "vx.y.z" -n "<notes>"
```

## Step 6: PKGBUILD + .SRCINFO + AUR 배포

GitHub release가 완료된 후에만 진행. 하나의 커밋으로 처리.

### 6a. PKGBUILD 업데이트 (`packaging/arch/PKGBUILD`)

1. Update `pkgver=x.y.z`

2. **Dependency sync audit** — compare:
   - `find_package()` in `CMakeLists.txt` → PKGBUILD `depends`/`makedepends`
   - `target_link_libraries()` in `src/CMakeLists.txt` → PKGBUILD `depends`
   - Report as table. Fix mismatches before proceeding.

3. Get sha256 of release tarball (requires sandbox bypass):
```bash
curl -sL "https://github.com/isac322/krema/archive/vx.y.z.tar.gz" | sha256sum | awk '{print $1}'
```

4. Update `sha256sums=('...')` with the new hash

### 6b. .SRCINFO 재생성

절대 수동으로 편집하지 않는다. 반드시 아래 명령으로 생성:
```bash
cd packaging/arch && makepkg --printsrcinfo > .SRCINFO && cd -
```

### 6c. Commit + Push

```bash
git add packaging/arch/PKGBUILD packaging/arch/.SRCINFO
git commit -m "chore: update PKGBUILD and .SRCINFO for vx.y.z

Co-Authored-By: Claude <model-name> <noreply@anthropic.com>"
git push
```

### 6d. AUR 업로드

AUR remote가 설정되어 있는지 확인:
```bash
git remote get-url aur 2>/dev/null || git remote add aur ssh://aur@aur.archlinux.org/krema.git
```

AUR에 push:
```bash
git subtree push --prefix=packaging/arch aur master
```

## Step 7: OBS Deployment

Skip if `OBS_READY=no` from Step 1.

The `_service` file points to `master` with `@PARENT_TAG@` versioning, so it automatically picks up the new version tag. The spec and debian.changelog were already updated in Step 3 and included in the tagged release commit. Just upload and trigger.

```bash
osc checkout home:isac322/krema
cp packaging/obs/* home:isac322/krema/
cd home:isac322/krema && osc addremove && osc commit -m "Update to v<version>" && cd -
rm -rf home:isac322
```

OBS `_service` (tar_scm) automatically fetches the tagged source and starts building. Verify builds are triggered by checking https://build.opensuse.org/package/show/home:isac322/krema

## Step 8: COPR Deployment

Skip if `COPR_READY=no` from Step 1.

```bash
copr-cli edit-package-scm isac322/krema \
  --name krema \
  --clone-url https://github.com/isac322/krema.git \
  --committish "v<version>" \
  --subdir packaging/obs \
  --spec krema.spec

copr-cli build-package isac322/krema --name krema
```

## Step 9: Launchpad PPA Deployment

Skip if `PPA_READY=no` from Step 1.

### 9a. Determine active Ubuntu series

Do NOT hardcode series names — they become obsolete. Query active series dynamically:
```bash
curl -s "https://api.launchpad.net/devel/ubuntu/series" \
  | python3 -c "
import json, sys
d = json.load(sys.stdin)
# Active statuses that accept PPA uploads
ok = ('Current Stable Release', 'Supported', 'Active Development', 'Pre-release Freeze')
for s in d['entries']:
    if s['active'] and s['status'] in ok and float(s['version']) >= 25.10:
        print(s['name'], s['version'], s['status'])
"
```

The version filter (>= 25.10) ensures Qt >= 6.8 availability. Upload a separate source package per series — the only difference is the `debian/changelog` target and version suffix (`~ppa1~<series>1`).

### 9b. Build source packages

```bash
PPA_DIR=$(mktemp -d)
curl -sL "https://github.com/isac322/krema/archive/v<version>.tar.gz" \
  -o "$PPA_DIR/krema_<version>.orig.tar.gz"
```

For each active series, build a source package with version `<version>-1~ppa1~<series>1`.

Use Docker to run `dpkg-buildpackage` (Arch doesn't have it natively):
```bash
DOCKER_HOST=tcp://localhost:2375 docker run --rm \
  -v "$(pwd):/src:ro" -v "$PPA_DIR:/output" ubuntu:25.10 bash -c '
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq dpkg-dev devscripts debhelper >/dev/null 2>&1
cd /output && tar xzf krema_<version>.orig.tar.gz && cd krema-<version>
mkdir -p debian/source && echo "3.0 (quilt)" > debian/source/format
cp /src/packaging/obs/debian.control debian/control
cp /src/packaging/obs/debian.rules debian/rules
cp /src/packaging/obs/debian.copyright debian/copyright
chmod +x debian/rules
cat > debian/changelog << "CHLOG"
krema (<version>-1~ppa1~<series>1) <series>; urgency=medium

  * New upstream release v<version>

 -- Byeonghoon Yoo <bhyoo@bhyoo.com>  <RFC 2822 date>
CHLOG
dpkg-buildpackage -S -us -uc -d
chmod 666 /output/krema_<version>*
'
```

The `chmod 666` is critical — Docker creates files as root, but GPG signing runs on the host as the current user.

### 9c. Sign source packages

The signing order matters because checksums change when files are signed:

1. **Sign `.dsc`** with `gpg --clearsign`
2. **Recalculate checksums** of the signed `.dsc` in `.changes` (md5, sha1, sha256, size)
3. **Sign `.changes`** with `gpg --clearsign`

If `debsign` is available, it handles all of this automatically:
```bash
debsign -k "$LP_GPG_KEY" <changes_file>
```

Otherwise, sign manually and update checksums with a script.

### 9d. Upload to Launchpad

Upload ALL 5 files (missing any one causes rejection):
- `krema_<version>.orig.tar.gz`
- `krema_<version>-1~ppa1~<series>1.debian.tar.xz`
- `krema_<version>-1~ppa1~<series>1.dsc`
- `krema_<version>-1~ppa1~<series>1_source.buildinfo`
- `krema_<version>-1~ppa1~<series>1_source.changes`

```bash
dput "ppa:isac322/krema" <changes_file>
```

If using Python ftplib fallback, upload to `~isac322/ubuntu/krema/` directory (NOT the FTP root).

### 9e. Cleanup

```bash
rm -rf "$PPA_DIR"
```

## Step 10: Summary

```
✅ Released v<version>

Commits:
  1. chore: release v<version> (includes OBS spec+debian.changelog)
  2. chore: update PKGBUILD and .SRCINFO for v<version>

Distribution deployment:
  AUR:  ✅ pushed / ⚠️ <error>
  OBS:  ✅ uploaded / ⏭️ skipped (osc not installed)
  COPR: ✅ build triggered / ⏭️ skipped (copr-cli not installed)
  PPA:  ✅ uploaded (<series list>) / ⏭️ skipped (<reason>)

GitHub Release: <URL>
COPR: https://copr.fedorainfracloud.org/coprs/isac322/krema/builds/
OBS:  https://build.opensuse.org/package/show/home:isac322/krema
PPA:  https://launchpad.net/~isac322/+archive/ubuntu/krema
```
