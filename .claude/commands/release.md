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

Read each file before editing. PKGBUILD는 이 단계에서 수정하지 않는다 — GitHub release 후 별도 처리.

### 3a. CMakeLists.txt
Update `project(krema VERSION x.y.z LANGUAGES CXX)`.

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
  src/com.bhyoo.krema.metainfo.xml .claude/work-state.md

git diff --cached --stat   # verify staged changes

git commit -m "chore: release vx.y.z

Co-Authored-By: Claude <model-name> <noreply@anthropic.com>"

git tag vx.y.z
git push && git push --tags
```

## Step 5: GitHub Release

Compose release notes following `.claude/rules/documentation.md` GitHub Release Notes rules:

- Intro: 1-2 sentences with SEO keywords (KDE Plasma 6, dock, Wayland, Latte Dock spiritual successor)
- Body: CHANGELOG categories, rewritten to lead with user value
- Footer: `🤖 Generated with [Claude Code](https://claude.com/claude-code)`
- English, past tense

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

## Step 7: Summary

```
✅ Released vx.y.z

Commits:
  1. chore: release vx.y.z
  2. chore: update PKGBUILD and .SRCINFO for vx.y.z

Updated files:
  - CMakeLists.txt, CHANGELOG.md, ROADMAP.md
  - metainfo.xml, work-state.md
  - PKGBUILD, .SRCINFO

PKGBUILD deps: ✅ in sync / ⚠️ <issues>
GitHub Release: <URL>
AUR: ✅ pushed / ⚠️ <error>
```
