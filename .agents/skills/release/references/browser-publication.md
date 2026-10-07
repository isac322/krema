# Browser publication playbook: KDE Store and AlternativeTo

KDE Store and AlternativeTo have no stable write API that this project can use, so their per-release updates are done in a real browser session through the installed Camofox tools. This playbook is the complete procedure. Follow it step by step, and stop at the first check that fails.

The scripts never publish to these stores. The coding agent runs this playbook in the release session, after the native channels in the skill's section 6. A task stays `browser_required` until this playbook finishes and `release_stores.py status` or a recorded receipt confirms the result.

## Ground rules

- Act only under the user's explicit release request for this version. A prepared packet or a pushed tag grants no permission. Once the request covers the store updates, work through the steps without asking for confirmation between them, except where this playbook says to ask.
- Use the existing logged-in Camofox sessions. Never import, export, copy, or print cookies or session storage, and never type a password, one-time code, or recovery code. If a site asks for any of them, or shows a CAPTCHA, stop, release the lock with `--state abort`, and report `blocked` to the user.
- Update the existing records only: KDE content record `2376676` and AlternativeTo item `9ea8dc3b-7a8b-4570-837f-9d9f2ef98af1`. Never create a new listing, a duplicate alternative relationship, or a paid priority review.
- Use only files and values from the prepared packet. Check every uploaded file's SHA-256 in the page before saving.
- Static branding (Launchpad project images, the GitHub social preview, the KDE Store logo and banner) is retained on version bumps. Replace it only when the packet's media differ from what is live.
- Awesome KDE, Awesome Wayland, Nixpkgs and LinuxLinks are one-time registrations. Check them with `status`; never resubmit them for a release.
- Never author or submit Flathub packaging.

## 0. Prepare and check

Use the context from `scripts/release.py prepare` for the verified tag.

```bash
PACKET=$(mktemp -d)/stores-vX.Y.Z
python3 scripts/release_stores.py prepare --context "$CTX" --output "$PACKET"
python3 scripts/release_stores.py status --context "$CTX" --store kde-store --store alternativeto
```

- `already_published`: nothing to do for that store. For KDE Store this means the public OCS record shows this version with the verified archive's MD5, or a receipt recorded after that public read (the OCS record can lag behind). For AlternativeTo it means a public read shows this release's packet description and item ID on the listing and Krema on the Latte Dock page, or a receipt recorded after such a read. A listing that merely exists is not enough.
- `pending`: another session holds the lock, or a submission is recorded as waiting for moderation or propagation. Do not upload again. Report it as pending. Recheck `status` within this request only with a finite wait, or when the user asks again; nothing rechecks it in the background. Then follow "Recovering a pending submission" in section 1.
- `browser_required`: continue with that store's section. For AlternativeTo this is also the result when the site refuses a scripted read (HTTP 403); a refused read is never evidence of publication.
- `blocked`: the public read failed. Check the page in the browser before doing anything, and report `blocked` if the read keeps failing.

Read `$PACKET/BROWSER-TASKS.md`. Each task lists the packet directory, exact version, source file name, label, SHA-256 and MD5, the media files, and `content_sha256`. When `claims_review_required` is `true`, the templates came from the installed fallback because the tag has no `packaging/submissions/`. Show the description to the user and get confirmation that the feature claims match this release before pasting it.

Check the packet's integrity before using any file. `record` repeats this check on every call except `abort`: it recomputes every file in `kit/SHA256SUMS`, refuses missing, unlisted, or symlinked files, and checks `kit.zip`, `kit/kit.json`, and each task's `content_sha256` against the context.

```bash
(cd "$PACKET/kit" && shasum -a 256 -c SHA256SUMS)
```

## 1. Take the lock

```bash
python3 scripts/release_stores.py record --context "$CTX" --packet "$PACKET" --store kde-store --state start
```

The lock is an exclusive file under the context's work directory. If it is already held, another session is working on the task: stop. If the command reports the store as already published for this packet, or as having a pending submission, stop; the upload must not be repeated.

If anything fails before the store accepts a change, release the lock without recording one. Abort reads only the context, not the packet, so it works when the packet is stale or deleted. It refuses a lock taken by another release's context.

```bash
python3 scripts/release_stores.py record --context "$CTX" --store kde-store --state abort
```

### Receipt states

| Command | Needs | Effect |
|---|---|---|
| `--state start` | packet; no lock; no pending receipt | takes the lock |
| `--state pending --reference R` | packet; the lock, or a pending receipt for the same packet content | records the submission as pending with `R` and releases the lock; on an existing pending receipt it only replaces the reference |
| `--state published [--reference <listing URL>]` | packet; the lock, or a pending receipt for the same packet content | re-reads the public store and records `published` only if this release's content is observed; otherwise it refuses and changes nothing |
| `--state abort` | the lock | releases the lock |
| `--state abort --reference E` | no lock; a pending receipt | withdraws the pending receipt with evidence `E` |

The published reference is always the canonical public listing (`https://store.kde.org/p/2376676` or `https://alternativeto.net/software/krema/`). Record any other evidence, such as a moderation notice, as a pending reference.

### Recovering a pending submission

A pending receipt blocks `start`, so no second upload happens while moderation is open.

- The moderation reference changed: run `--state pending --reference <new reference>`. No lock or upload is involved.
- The change is now public: run `--state published`. It succeeds only when the public read shows this release's content.
- The store rejected the submission, or it never arrived: confirm this in the browser first, then run `--state abort --reference "<what the page showed>"`, and start a new session from section 1.
- KDE Store `status` reports `browser_required` with an MD5 mismatch while pending: the uploaded file is wrong. Withdraw the pending receipt as above, then replace the file.

## 2. Sign in: GitHub first, then OpenDesktop through GitHub

1. `camofox_create_tab` with `https://github.com/`.
2. `camofox_evaluate` with `document.querySelector('meta[name="user-login"]')?.content`. It must return `isac322`. Any other value, or an empty one, means the session is not usable: stop and report `blocked`.
3. `camofox_navigate` to `https://www.opendesktop.org/`. Take a `camofox_snapshot`. If the page already shows the `isac322` account, continue with section 3.
4. Otherwise open the site's login control and choose **Login with GitHub**. On GitHub's authorization page, confirm the application belongs to OpenDesktop/Pling and requests only profile and email access, then approve it. If it requests repository or organization access, stop.
5. Snapshot again and confirm the OpenDesktop account is `isac322`.

## 3. KDE Store record 2376676

1. Open `https://store.kde.org/p/2376676` and confirm the page title is Krema and the owner is `isac322`. Use the owner's edit control from the snapshot; do not guess edit URLs.
2. Compare the form with the packet:
   - **Version**: set it to the task's `version`.
   - **Description**: compare it with `kit/kde-store/description.txt`. Replace it only if it differs. Keep the plain-text line breaks.
   - **Category**: `Various Plasma 6 Improvements` (ID `713`) stays unchanged.
   - **Fields**: map `kit/kde-store/fields.json` to the matching controls. The private form labels were not verified, so map them by meaning and leave a control alone when no field matches.
   - **Media**: compare the live gallery with `kit/kde-store/media/`. Upload a file only if it is missing or its image differs. Never add a duplicate. Use the caption and alt text from `kit/kde-store/upload-order.txt` where the form offers them.
3. Upload the source file `kit/kde-store/downloads/<source_filename>` with the injection procedure in section 5. Before saving, the in-page SHA-256 must equal the task's `source_sha256`. Set the file label to `source_label` and the file version to `version`. Do not upload a binary, installer, widget, or Plasmoid archive.
4. After the new file is accepted, deactivate or remove the previous release's source file only if the form offers that. Never remove it before the new upload has been accepted.
5. Take a `camofox_screenshot` of the filled form, then save or publish.
6. Run `status --store kde-store`. It compares the public OCS record's version, file name and MD5 with the context archive. While you hold the lock it reports `pending`, so record the outcome:
   - The OCS record shows the new version and file: record it. The command re-reads the live record and refuses if the version or MD5 does not match:
     ```bash
     python3 scripts/release_stores.py record --context "$CTX" --packet "$PACKET" --store kde-store --state published
     ```
   - The site reports moderation, or the OCS record has not refreshed yet: record `pending` with what the page showed and report it as pending. Do not upload again while it is pending. Once the OCS record matches, in this request or a later one, run `--state published` to close the receipt.
     ```bash
     python3 scripts/release_stores.py record --context "$CTX" --packet "$PACKET" --store kde-store --state pending --reference "https://store.kde.org/p/2376676 (saved <UTC time>, awaiting refresh)"
     ```
   - The `published` command refuses with an MD5 mismatch: the uploaded file is not the verified archive. Replace it with the packet's file and check again.
7. Check the mirrors in the task (`opendesktop.org`, `pling.com`, `linux-apps.org`). They read the same record, so do not edit them separately.

## 4. AlternativeTo item 9ea8dc3b-7a8b-4570-837f-9d9f2ef98af1

Most releases need no AlternativeTo change. Take the lock with `--store alternativeto --state start` first.

1. Open `https://alternativeto.net/software/krema/` in Camofox. Confirm that it is the existing Krema listing: `camofox_evaluate` with `document.documentElement.innerHTML.includes('9ea8dc3b-7a8b-4570-837f-9d9f2ef98af1')` must return `true`. Confirm the session is signed in through the snapshot. If it is not signed in, stop and report `blocked`.
2. Open `https://alternativeto.net/software/latte-dock/` and confirm Krema is listed among the alternatives. If it is, never use "Add alternative" or "Suggest alternative" for Krema again. If it is missing, report this to the user instead of re-adding it; a missing relationship can be pending moderation or removed by a moderator.
3. Compare the listing with the packet: name and short summary in `kit/alternativeto/upload-order.txt`, the description in `kit/alternativeto/description.txt`, the fields in `kit/alternativeto/fields.json`, and the media in `kit/alternativeto/media/`. If everything already matches, run `--state published`. It succeeds only when a scripted public read shows the packet description. If AlternativeTo refuses that read (HTTP 403), the command refuses too: record `pending` with what you saw in the browser (for example `"browser check <UTC time>: listing matches packet; scripted read refused"`), which also releases the lock. Status then reports the store as `pending`, never as published, until a later `--state published` succeeds.
4. Otherwise use the listing's edit or suggest-changes control and update only the fields that differ. The description must not contain URLs or email addresses. Put the website and repository links in their dedicated link controls. Leave paid priority review unselected. Upload media only if missing or different, with the injection procedure in section 5.
5. Take a screenshot, then submit. AlternativeTo edits normally go to moderation: record `pending` with the confirmation shown on the page. Later, when the change is visible on the public listing, run `--state published` without another upload. The receipt then stores the public-read observation, and status keeps reporting `already_published` even if later reads are refused, as long as the packet description is unchanged.

## 5. Uploading a local file into a page

Camofox has no file-chooser tool, so files are injected into the form's file input from the packet. Do it in chunks so no tool call carries an oversized expression.

1. On the host, encode the file and split it:
   ```bash
   base64 < "$PACKET/kit/kde-store/downloads/<source_filename>" | tr -d '\n' | fold -w 262144 > /tmp/krema-upload.b64
   wc -l /tmp/krema-upload.b64
   ```
2. `camofox_evaluate` with `window.__kremaChunks = []; 0`.
3. For each line of `/tmp/krema-upload.b64`, in order, `camofox_evaluate` with `window.__kremaChunks.push("<line>")`. The returned count must increase by one each time.
4. Build the file, check its hash, and attach it to the input that the snapshot identified (replace the selector, name, and MIME type):
   ```js
   (async () => {
     const bin = atob(window.__kremaChunks.join(""));
     const bytes = Uint8Array.from(bin, c => c.charCodeAt(0));
     const hex = [...new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))]
       .map(b => b.toString(16).padStart(2, "0")).join("");
     const input = document.querySelector("<file input selector>");
     const transfer = new DataTransfer();
     transfer.items.add(new File([bytes], "<file name>", { type: "<application/gzip or image/png>" }));
     input.files = transfer.files;
     input.dispatchEvent(new Event("change", { bubbles: true }));
     delete window.__kremaChunks;
     return hex;
   })()
   ```
5. The returned hex must equal the expected SHA-256: `source_sha256` for the source archive, or the file's line in `kit/SHA256SUMS` for media. If it differs, reload the page without saving and start the upload again.
6. Delete `/tmp/krema-upload.b64`.

## 6. Finish

- Close the tabs with `camofox_close_tab`.
- Run `python3 scripts/release_stores.py status --context "$CTX"` and report each surface's status as printed. Report `pending` as pending, and `browser_required` as not done. Report `published` only when status shows `already_published`.
- Report the screenshots and references you recorded. Every receipt ties the release tag, commit, source SHA-256, and packet `content_sha256` together and keeps the earlier states in `history`, so a later session will not repeat the same upload.
