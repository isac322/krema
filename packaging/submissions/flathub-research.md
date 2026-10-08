# Flathub research notes

## Status

Flathub remains blocked for this release packet. This file records official guidance and commands for a human maintainer. It does not contain a Flatpak manifest, a pull request description, or an app-packaging contribution.

The prepared release source is:

- URL: `https://github.com/isac322/krema/archive/v0.10.0.tar.gz`
- Version: `0.10.0`
- Tag: `v0.10.0`
- Commit: `a964a908ecb8904c079a8148403bebaaa427a821`
- SHA256: `89612ed09921fa2990bc49f222352590e428a4fb38cd04b1cbefa87162bad7f0`

The source archive is the input for later human-authored Flatpak work. The submission kit does not turn it into a manifest or assert that Flathub validation has passed.

## Official policy and submission guidance

Read the current Flathub requirements before writing any packaging files:

- [Flathub requirements and guidelines](https://docs.flathub.org/docs/for-app-authors/requirements-and-guidelines/)
- [Flathub submission process](https://docs.flathub.org/docs/for-app-authors/submission/)
- [Flathub app maintenance](https://docs.flathub.org/docs/for-app-authors/maintenance)

Those pages are the authority for current naming, licensing, account, review, namespace, metadata, and submission requirements. A human maintainer must author the manifest and any pull request text under the current Flathub policy. This packet deliberately stops before that work.

The project homepage to review is `https://krema.bhyoo.com/`. Confirm its HTTPS reachability and the final app ID manually when the human-authored manifest is prepared. No DNS, website, or account change is part of this packet.

## Source and dependency guidance

Use the official Flatpak documentation for the build model and dependency declarations:

- [Flatpak first build](https://docs.flatpak.org/en/latest/first-build.html)
- [Flatpak dependencies](https://docs.flatpak.org/en/latest/dependencies.html)
- [Flatpak sandbox permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html)

A human maintainer must choose the supported KDE/Qt runtime and SDK, declare every build dependency in the manifest, and review the resulting permissions against the application. This research note does not select a runtime, claim a support lifetime, or make a sandbox claim that has not been experimentally checked.

Use the fixed source URL and SHA256 above when the manifest's source section is authored. Review the source archive's build instructions and licenses from the release itself. Do not substitute a binary, installer, or Plasma widget package.

## Official command forms for later validation

The commands below are the standard command forms documented by Flatpak and Flathub. Set `MANIFEST`, `APP_ID`, `COMMAND`, `BUILD_DIR`, `REPO`, `BUNDLE`, and `BRANCH` to values from the human-authored manifest and the current official instructions. The variables here are command inputs, not a proposed Krema manifest.

Install the builder from Flathub, if it is not already available:

```sh
flatpak install flathub org.flatpak.Builder
```

Build and export a local repository with the human-authored manifest:

```sh
flatpak run org.flatpak.Builder \
  --force-clean \
  --repo="$REPO" \
  "$BUILD_DIR" \
  "$MANIFEST"
```

Run the built application through the builder's documented run mode when the manifest supports it:

```sh
flatpak run org.flatpak.Builder \
  --run \
  "$BUILD_DIR" \
  "$MANIFEST" \
  "$COMMAND"
```

Here `COMMAND` is the executable command defined by the human-authored manifest. It is not necessarily the application ID.

Create a test bundle from the exported repository using the documented app ID and branch:

```sh
flatpak build-bundle "$REPO" "$BUNDLE" "$APP_ID" "$BRANCH"
```
- [Flathub submission documentation](https://docs.flathub.org/docs/for-app-authors/submission/)

No command in this note submits to Flathub. Human review of the manifest, source, dependencies, permissions, metadata, build output, and current Flathub policy remains required.
