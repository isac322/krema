# Submission preparation kit

These files are the store and catalog data for Krema's existing listings. KDE Store already has [Krema's listing](https://store.kde.org/p/2376676), and each release prepares a version update for it. AlternativeTo already has [Krema's listing](https://alternativeto.net/software/krema/) and its Latte Dock alternative relationship; its packet supports maintaining those entries, not creating them. The [Launchpad project](https://launchpad.net/krema), its branding, and the PPA are already published and need verification only. The generator prepares local files; it does not authenticate, publish updates, create projects, or change package channels.

The JSON schemas use `{{version}}`, `{{tag}}`, `{{commit}}`, `{{repository}}`, `{{source_url}}`, `{{source_sha256}}` and `{{source_filename}}` tokens, which the kit fills in from a verified release context, so a normal release needs no schema edit. The source file is always the GitHub release asset `https://github.com/isac322/krema/releases/download/vX.Y.Z/krema-X.Y.Z.tar.gz`. Each schema's `observed_release` (recorded as `schema_baseline` in `kit.json`) is the release the listings were last verified against, `0.10.0` at commit `a964a908ecb8904c079a8148403bebaaa427a821`. Those receipts are historical evidence and are not rewritten for later releases. Static branding stays as published, so packets for later releases do not ask for it to be uploaded again.

The kit reads the schemas, descriptions and media from the tagged source tree when it contains all three schemas. Otherwise it falls back to the copies installed with the release tooling, marks `kit.json` with `templates.source: "installed-fallback"` and `human_review_required: true`, and a person must confirm the feature claims before anything is pasted.

## Build the kit

Create a release context first (see `.agents/skills/release/SKILL.md`), then run either command from the repository root. The output directory must be new or empty and outside the repository:

```sh
python3 scripts/release.py prepare --tag vX.Y.Z --output /tmp/krema-X.Y.Z

# Kit only:
python3 scripts/prepare_submission_kit.py \
  --context /tmp/krema-X.Y.Z/context.json \
  --output /tmp/krema-X.Y.Z-submission-kit

# Kit plus stores.json and BROWSER-TASKS.md (the kit lands in <output>/kit):
python3 scripts/release_stores.py prepare \
  --context /tmp/krema-X.Y.Z/context.json \
  --output /tmp/krema-X.Y.Z-stores
```

Both read `kde-store.json`, `alternativeto.json` and `launchpad.json` from the tagged source tree (or the installed fallback described above). The kit is written as the output directory, an adjacent ZIP, and `SHA256SUMS`. The source file comes from the context's archive; the command checks it against the context's SHA-256, checks the archive layout without extracting it and requires its CMake version to match the context version. It refuses incomplete or unsafe inputs, checks PNG signatures, dimensions, and Git LFS pointer files, and refuses a non-empty output directory or an existing ZIP. It downloads nothing.

`python3 scripts/release_stores.py status --context CONTEXT.json` reports the live state of each listing and catalog queue with read-only requests. Browser work is tracked with `python3 scripts/release_stores.py record --context CONTEXT.json [--packet DIR] --store kde-store|alternativeto --state start|pending|published|abort [--reference R]`. The receipts are kept in the context's `work_dir/store-receipts/` and are bound to the tag, commit, source hash and packet content. `--packet` is required for every state except `abort`, and each of those calls re-verifies the whole packet first. `published` is recorded only after a public read shows this release: for the KDE Store, the version and the archive's MD5 in the OCS record; for AlternativeTo, the packet's description on the existing listing and its Latte Dock relationship. A listing merely existing is not evidence of publication. The lock, pending, abort and status rules are in `.agents/skills/release/SKILL.md`, section 7. All three subcommands print `{"results": [...]}`, or `{"error": ...}` with exit code 2.

Each channel directory in the kit contains:

- `description.txt`, the text to paste into the channel;
- `fields.json`, the final field values;
- `submission.json`, the complete source schema for provenance;
- `upload-order.txt`, local filenames, media captions and alt text, field values, steps, evidence, and limitations;
- `media/`, with the final upload filenames; and
- `downloads/`, with verified download files where that channel has them.

Use `SHA256SUMS` before copying files to a browser upload form. The ZIP preserves these channel directories so the packet remains inspectable offline.

## Upload order and boundaries

Use the channels in this order, verifying existing destinations before creating anything. Launchpad is a verification-only handoff for an already published project, not a registration task. The packet itself grants no publication permission; KDE Store and AlternativeTo updates still require the maintainer's authorization and review of the authenticated form. These stores have no write API the tooling uses, so every KDE Store and AlternativeTo task is reported as `browser_required`; the browser steps are in [`.agents/skills/release/references/browser-publication.md`](../../.agents/skills/release/references/browser-publication.md).

### 1. KDE Store / OpenDesktop / Pling / LinuxApps.org

1. Open the existing listing at `https://store.kde.org/p/2376676` using the intended maintainer account; do not create a duplicate.
2. Update the listed version to the packet's release version (the current live version is shown by `release_stores.py status`) and review the prepared metadata, including category `713`, `Various Plasma 6 Improvements`, against the available authenticated controls. The public OCS record is `https://api.kde-look.org/ocs/v1/content/data/2376676`; verify the mirrored frontends after publishing.
3. Update the description with `kde-store/description.txt`.
4. Keep the existing gallery. Replace an image from `kde-store/media/` only when the screenshots themselves changed, using the captions and alt text in `kde-store/upload-order.txt`. Avoid duplicate gallery images.
5. Update the source download with `kde-store/downloads/krema-X.Y.Z.tar.gz` from the packet. Preserve the exact label and SHA-256 from `kde-store/upload-order.txt`, and state that it is source requiring a build. Do not present it as a native installer or a Plasmoid package.
6. Review the rendered update and authenticated form, then verify `https://www.opendesktop.org/p/2376676/`, `https://www.pling.com/p/2376676/`, and `https://www.linux-apps.org/p/2376676/`. Final submission or publication requires the maintainer's authorization.

The packet does not claim that the authenticated KDE Store form was tested. The channel URLs in the description are public references for the listing and do not represent a submission performed by this tool.

### 2. AlternativeTo

1. Open the existing Krema listing at `https://alternativeto.net/software/krema/`; do not create a duplicate.
2. Verify its existing relationship on the Latte Dock alternatives page linked in `alternativeto/upload-order.txt`; do not add that relationship again.
3. Review and update metadata where needed using `alternativeto/upload-order.txt`, keeping website and repository links in dedicated URL controls.
4. Use `alternativeto/description.txt` for the description. This copy contains no URL or email address.
5. Review existing media and update it where needed from `alternativeto/media/`, using the local filenames and media notes without duplicating gallery images.
6. Leave optional paid priority review unselected. Stop before final submission or publication until the maintainer authorizes the update.

The packet does not claim that AlternativeTo's private form was tested or that an update was submitted or approved.

The finalized `name`, `short_summary`, `relation.rationale`, and `optional_fee.chosen` values are top-level submission values. Copy them from `alternativeto/upload-order.txt` or the channel's `submission.json`; they do not belong to `fields.json`.

### 3. Launchpad

1. Open the existing project at `https://launchpad.net/krema`, the entry URL in `launchpad/upload-order.txt`. Verify the name `Krema`, owner `isac322`, information type `Public`, and license `GNU GPL v3` against the canonical API resource at `https://api.launchpad.net/1.0/krema`.
2. Keep that existing project as the destination. Skip `https://launchpad.net/projects/+new`; do not create a duplicate or use `Complete Registration`. Activating that control or pressing Enter in a registration text input publishes a project.
3. Verify the published branding URLs and SHA-256 values in `launchpad/submission.json` against the corresponding files in `launchpad/media/`: the 14px project icon, 64px project logo, and 192px project brand image. These assets are already published; this handoff calls for verification, not another upload.
4. Keep the existing `~isac322` personal profile, `ppa:isac322/krema` archive, PPA ownership, and PPA install URL unchanged. Project branding is separate from the personal profile and PPA; do not upload a personal profile image as project branding.
5. Treat `registration_field_mapping` in `launchpad/submission.json` as historical pre-registration provenance, not current editable controls or instructions to register again. The `published_project`, `branding_upload`, and media records describe the existing published destination.

The generator does not create or modify this project. It copies the recorded publication evidence and branding into an offline verification packet.

### 4. Local Flatpak artifact and Flathub policy blocker

[`flathub-research.md`](flathub-research.md) records official policy, source and dependency requirements, and validation commands. It is research only, not a submission manifest or Flathub pull request handoff.

The separate [local Flatpak packaging README](../flatpak/README.md) documents an **AI-authored local build/install artifact**, its build and `aarch64` installation evidence, and the outstanding verification. It is outside the three-channel submission kit. Official lint is not passing, and the installed Plasma Wayland smoke test remains pending; no Flathub acceptance is claimed.

Flathub's documented AI policy currently excludes AI-generated or AI-assisted manifest content and automated agent submission pull requests. Local build or installation success does not make this manifest eligible for Flathub. A separate human-authored, human-reviewed submission route is required; do not submit this local AI-authored manifest or turn the research packet into an agent-authored Flathub pull request.

`prepare_submission_kit.py` reads the repository files and the context's verified archive. It has no login flow, credential handling, browser automation, external submission, Git operation, project creation, PPA edit, or Flatpak publication path. Its generated `kit.json` publication state describes what the generator did, not whether an external project exists. The complete channel schemas in `submission.json` retain recorded external status and evidence, including Launchpad's published project and branding.
