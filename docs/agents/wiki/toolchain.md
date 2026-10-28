# Toolchain

On 2026-10-28:

- Development Mac: Intel x86_64, Xcode 26.3, CMake 4.4 (Homebrew), no Ninja. Homebrew builds from source here, so heavy brew installs are avoided; dependencies come through CMake `FetchContent` at pinned versions.
- **JUCE 9.0.4** (released 2026-10-12, AGPLv3 dual licence confirmed in its LICENSE.md). Chosen over 8.0.12 for Xcode 26 support.
- **Catch2 v3.9.1** for tests.
- **pluginval v1.0.4** (macOS zip from GitHub releases), downloaded into the session scratchpad, never into the repo.
- Universal builds: `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`; only x86_64 can run on this Mac. CI on GitHub's arm64 macOS runners runs the tests on Apple Silicon.
- 2026-10-28 (#2): inside the agent sandbox, CMake's own downloader times out on github.com while `curl` works. Workaround: fetch the tarballs with `curl` into a directory and pass it as `-DNL2X_DOWNLOAD_CACHE=<dir>` (files named `v3.9.1.tar.gz` and `9.0.4.tar.gz`, as in the URLs); CMake checks the hash and skips the download. CI downloads normally.
- 2026-10-28 (#2): a clean Universal Release build of the plugin takes several minutes on the development Mac; engine-only builds (`-DNL2X_BUILD_PLUGIN=OFF`) take seconds.
