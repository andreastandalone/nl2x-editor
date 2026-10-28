# Toolchain

On 2026-10-28:

- Development Mac: Intel x86_64, Xcode 26.3, CMake 4.4 (Homebrew), no Ninja. Homebrew builds from source here, so heavy brew installs are avoided; dependencies come through CMake `FetchContent` at pinned versions.
- **JUCE 9.0.4** (released 2026-10-12, AGPLv3 dual licence confirmed in its LICENSE.md). Chosen over 8.0.12 for Xcode 26 support.
- **Catch2 v3.9.1** for tests.
- **pluginval v1.0.4** (macOS zip from GitHub releases), downloaded into the session scratchpad, never into the repo.
- Universal builds: `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`; only x86_64 can run on this Mac. CI on GitHub's arm64 macOS runners runs the tests on Apple Silicon.
