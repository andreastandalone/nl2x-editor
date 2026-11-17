# Release process

Steps for Andrea to publish a version, for example the beta 0.1.0-beta.1 or the final 0.1.0. The workflow never publishes by itself: it creates a draft release.

## The version

The version lives in one place, the top-level `CMakeLists.txt`:

```cmake
project(NL2XEditor VERSION 0.1.0 LANGUAGES C CXX)
set(NL2X_VERSION_LABEL "beta.1")
```

- `VERSION` is numeric (major.minor.patch) because macOS and the Audio Unit need a numeric bundle version; it goes into every bundle's `Info.plist` (CFBundleShortVersionString and CFBundleVersion) and the AU version.
- `NL2X_VERSION_LABEL` marks a pre-release (`beta.1`, `beta.2`, `rc.1`); it is empty (`""`) for a final release. The full version, `0.1.0-beta.1`, is what the About window and the standalone app show and what the tag must be: `v0.1.0-beta.1`.
- So every beta of 0.1.0 carries bundle version 0.1.0; only the label tells them apart. The final release sets the label to `""` and keeps `VERSION 0.1.0`.
- Release notes for a version go in `docs/release-notes/<full version>.md` (for example `docs/release-notes/0.1.0-beta.1.md`): the workflow uses them as the draft's text. Without that file it generates notes from the merged pull requests. Keep them short: what it is, how to download, the features.

## Steps

1. **Hardware checklist.** Run the [beta acceptance run](hardware-checklist.md#beta-acceptance-run-010-beta1) (for a beta) or the whole [hardware checklist](hardware-checklist.md) (for a final release) on the Nord Lead 2X in Ableton Live and Cubase. Every item must pass, or the exception must be an issue you accept for this version. The run can use the dry run's zip (step 3).
2. **Set the version** in `CMakeLists.txt` as above, write the release notes and update the README's **Installation** section: the version line and the tag in the three download links (see [Download links](#download-links)). Commit and merge to `main`.
3. **Optional dry run.** In GitHub, Actions > Release > Run workflow on `main`. It builds, tests, signs ad hoc, checks and packages exactly as a tag does, and only uploads the three zips as one workflow artifact (named `NL2X-Editor-...-dry-run-<commit>`); no release is created.
4. **Tag and push the tag.** From an up-to-date `main`:
   ```sh
   git tag v0.1.0-beta.1
   git push origin v0.1.0-beta.1
   ```
5. **Wait for the workflow** (Actions > Release, about 20–30 minutes). It:
   - refuses the tag unless it equals `v` + the full version in `CMakeLists.txt`;
   - builds the Universal (arm64 + x86_64) Release of the VST3, the AU and the standalone app from a clean compile, and runs the tests;
   - checks that each binary holds both architectures;
   - signs each bundle ad hoc (no Developer ID, no notarization) and verifies the signature, so that macOS shows its "could not verify" block with Open Anyway rather than calling the download "damaged";
   - checks that each bundle carries the numeric version;
   - runs pluginval on the VST3 and auval on the AU;
   - packages one zip per format, with stable names: `NL2X-Editor-VST3.zip`, `NL2X-Editor-AU.zip` and `NL2X-Editor-App.zip`, each a folder with the bundle, `LICENSE` and a short `INSTALL.txt`;
   - creates a **draft** release named "NL2X Editor 0.1.0-beta.1" for the tag, with the three zips attached and the release notes as its text, marked **pre-release** when the version has a label.
6. **Review the draft** under Releases: read the text, download the zips on a Mac where the build has never run and check they install and open as the README says. After publishing, click the README's three download links: each must download its zip.
7. **Publish** the draft. To abandon a bad release, delete the draft and the tag (`git push --delete origin v0.1.0-beta.1`, `git tag -d v0.1.0-beta.1`) and tag again after the fix, or move on to the next label (`beta.2`).

## Download links

The README links each zip directly, by tag:

```
https://github.com/andreastandalone/nl2x-editor/releases/download/v0.1.0-beta.1/NL2X-Editor-VST3.zip
```

GitHub also offers `releases/latest/download/<name>`, which never needs updating, but "latest" skips pre-releases and drafts: while only betas exist, those links would fail. So:

- **For every pre-release** (`beta.N`, `rc.N`), change the tag in the README's three links (and its version line) in step 2. The links work only once the release is published (step 7).
- **From the first final release on**, the links can switch to `https://github.com/andreastandalone/nl2x-editor/releases/latest/download/NL2X-Editor-VST3.zip` (and `-AU`, `-App`), and then need no update. A later beta would not move them, which is what users of the stable version want.

The zip names carry no version on purpose, so that only the tag in the link changes.
