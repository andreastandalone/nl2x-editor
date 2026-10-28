# Release process

Steps for Andrea to publish a version, for example v0.1.0. The workflow never publishes by itself: it creates a draft release.

1. **Hardware session.** Do [hardware-discovery-plan.md](hardware-discovery-plan.md) first and apply its results to `engine/include/nl2x/HardwareAssumptions.h`.
2. **Hardware checklist.** Run [hardware-checklist.md](hardware-checklist.md) on the Nord Lead 2X in Ableton Live and Cubase. Every item must pass, or the exception must be an issue you accept for this version.
3. **Bump the version.** The version lives in one place: `project(NL2XEditor VERSION 0.0.1 ...)` in the top-level `CMakeLists.txt`. Change it to the release version (for v0.1.0: `0.1.0`), commit and merge to `main`. The tag name must match (`v` + that version). Update the README "Status" line if it still says the release is in preparation.
4. **Optional dry run.** In GitHub, Actions > Release > Run workflow on `main`. It builds, tests and packages, and only uploads the zip as a workflow artifact; no release is created.
5. **Tag and push the tag.** From an up-to-date `main`:
   ```sh
   git tag v0.1.0
   git push origin v0.1.0
   ```
6. **Wait for the workflow** (Actions > Release, about 20-30 minutes). It builds the Universal Release, runs the tests, checks the architectures, runs pluginval and auval, packages `NL2X-Editor-0.1.0-macOS-universal.zip` and creates a **draft** release with the zip attached.
7. **Review the draft** under Releases: edit the notes (generated from the merged pull requests), download the zip on a clean Mac and check it installs as the README says.
8. **Publish** the draft. To abandon a bad release, delete the draft and the tag (`git push --delete origin v0.1.0`, `git tag -d v0.1.0`) and tag again after the fix.

The release workflow refuses a tag that does not match `project(NL2XEditor VERSION …)` in `CMakeLists.txt`, so bump the version first.
