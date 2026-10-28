#!/usr/bin/env bash
# Validates a built plugin with pluginval (https://github.com/Tracktion/pluginval).
#
#   tools/run-pluginval.sh <path/to/plugin.vst3> [cache-dir] [extra pluginval options...]
#
# pluginval is downloaded once into the cache dir (default: build/pluginval, which git ignores)
# and checked against a pinned hash. PLUGINVAL_STRICTNESS sets the strictness level (default 10).
# Example: tools/run-pluginval.sh "build/plugin/plugin/NL2XEditor_artefacts/Release/VST3/NL2X Editor.vst3"
set -euo pipefail

readonly version="1.0.4"
readonly sha256="3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f"
readonly url="https://github.com/Tracktion/pluginval/releases/download/v${version}/pluginval_macOS.zip"

if [[ $# -lt 1 ]]; then
    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi

plugin="$1"
cache="${2:-build/pluginval}"
shift $(( $# >= 2 ? 2 : 1 ))

if [[ ! -e "$plugin" ]]; then
    echo "error: plugin not found: $plugin" >&2
    exit 2
fi

install_dir="$cache/pluginval-$version"
binary="$install_dir/pluginval.app/Contents/MacOS/pluginval"

if [[ ! -x "$binary" ]]; then
    mkdir -p "$cache"
    zip="$cache/pluginval_macOS-$version.zip"
    echo "Downloading pluginval $version into $cache"
    curl -fsSL --retry 3 -o "$zip" "$url"
    echo "$sha256  $zip" | shasum -a 256 -c -
    rm -rf "$install_dir"
    mkdir -p "$install_dir"
    unzip -q "$zip" -d "$install_dir"
fi

exec "$binary" --strictness-level "${PLUGINVAL_STRICTNESS:-10}" --validate "$plugin" "$@"
