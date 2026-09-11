#!/bin/bash
# Pixelview macOS build helper. Development builds keep updates disabled.
set -euo pipefail
cd "$(dirname "$0")/../.."
# Ordinary local builds always enter the canonical identity/process gate.
# Release preparation keeps its independent explicit identity and clean-tag policy.
if [[ "${PIXELVIEW_RELEASE_BUILD:-OFF}" != "ON" && "${PIXELVIEW_LOCAL_SIGNING_VERIFIED:-}" != "1" ]]; then
  exec python3 cmake/macos/pixelview-signed-development.py "$@"
fi
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"

read_version_field() {
  python3 - "$1" <<'PY'
import json
import sys
with open("version.json", encoding="utf-8") as handle:
    print(json.load(handle)[sys.argv[1]])
PY
}

obs_base_version="$(read_version_field obs_base_version)"
obs_base_describe="$(read_version_field obs_base_describe)"
identity="${PIXELVIEW_CODESIGN_IDENTITY:--}"
team="${PIXELVIEW_CODESIGN_TEAM:-}"
release_build="${PIXELVIEW_RELEASE_BUILD:-OFF}"
build_config="${PIXELVIEW_BUILD_CONFIG:-RelWithDebInfo}"
appcast_url="${PIXELVIEW_SPARKLE_APPCAST_URL:-}"
sparkle_key="${PIXELVIEW_SPARKLE_PUBLIC_KEY:-}"
source_commit="${PIXELVIEW_SOURCE_COMMIT:-$(git rev-parse HEAD)}"
source_tag="${PIXELVIEW_SOURCE_TAG:-$(git describe --exact-match --tags HEAD 2>/dev/null || true)}"
c_compiler="$(xcrun --find clang)"
cxx_compiler="$(xcrun --find clang++)"
macos_sdk="$(xcrun --sdk macosx --show-sdk-path)"

case "$build_config" in
  Debug|RelWithDebInfo|Release|MinSizeRel) ;;
  *) printf 'Invalid PIXELVIEW_BUILD_CONFIG: %s\n' "$build_config" >&2; exit 2 ;;
esac

build_dir=build_macos
if [[ "$identity" != "-" ]]; then
  if [[ ! "$team" =~ ^[A-Z0-9]{10}$ ]] ||
     [[ "$identity" != "Developer ID Application: "* && ! "$identity" =~ ^[A-Fa-f0-9]{40}$ ]]; then
    printf '%s\n' 'Set PIXELVIEW_CODESIGN_IDENTITY to a Developer ID Application name or SHA-1 and PIXELVIEW_CODESIGN_TEAM to its 10-character team ID.' >&2
    exit 2
  fi
  build_dir=build_macos
elif [[ -n "$team" ]]; then
  printf '%s\n' 'PIXELVIEW_CODESIGN_TEAM requires an explicit Developer ID Application identity.' >&2
  exit 2
fi

if [[ "$release_build" == "ON" ]]; then
  if [[ "$identity" == "-" || -z "$team" || "$build_config" != "Release" || -z "$appcast_url" || -z "$sparkle_key" ]]; then
    printf '%s\n' 'Release builds require Developer ID identity/team, Release configuration, and Pixelview Sparkle URL/public key.' >&2
    exit 2
  fi
  build_dir="build_macos_release_$(read_version_field pixelview_version)_$(read_version_field pixelview_build_number)"
else
  release_build=OFF
  appcast_url=""
  sparkle_key=""
fi

build_dir="${PIXELVIEW_BUILD_DIR:-$build_dir}"
if [[ "$release_build" != "ON" ]]; then
  [[ "$identity" != "-" && -n "$team" && "$build_config" == "RelWithDebInfo" && "$build_dir" == "$PWD/build_macos" ]] || {
    printf '%s\n' 'Local builds require the canonical signed-development wrapper; no ad-hoc or alternate output.' >&2
    exit 2
  }
fi

# Verify pins and stage the curated receiver runtime for EVERY build, including
# ordinary local development. Missing/unreviewed dependencies fail closed.
python3 plugins/pixelview-whep/scripts/fetch-gstreamer.py
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py
python3 plugins/pixelview-whep/scripts/bundle-runtime.py stage .deps/pixelview-gstreamer

# Upstream manual Xcode signing signs dependencies on copy. The release mode is
# fail-closed; ordinary local builds use verified Developer ID and no updater.
cmake --preset macos -B "$build_dir" \
  "-DPIXELVIEW_LICENSE_DATA_DIR=${PIXELVIEW_LICENSE_DATA_DIR:-}" \
  "-DPIXELVIEW_ENABLE_UNIVERSAL_LINKS=${PIXELVIEW_ENABLE_UNIVERSAL_LINKS:-OFF}" \
  "-DPIXELVIEW_ASSOCIATED_DOMAINS_PROFILE=${PIXELVIEW_ASSOCIATED_DOMAINS_PROFILE:-}" \
  "-DOBS_CODESIGN_IDENTITY=$identity" "-DOBS_CODESIGN_TEAM=$team" \
  -DOBS_PROVISIONING_PROFILE= \
  "-DOBS_VERSION_OVERRIDE=$obs_base_version" \
  "-DPIXELVIEW_RELEASE_BUILD=$release_build" \
  "-DPIXELVIEW_SOURCE_COMMIT=$source_commit" \
  "-DPIXELVIEW_SOURCE_TAG=$source_tag" \
  "-DCMAKE_C_COMPILER=$c_compiler" \
  "-DCMAKE_CXX_COMPILER=$cxx_compiler" \
  "-DCMAKE_OSX_SYSROOT=$macos_sdk" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DPIXELVIEW_LEGACY_TOOLCHAIN=ON \
  -DENABLE_BROWSER=OFF -DENABLE_WHATSNEW=OFF -DENABLE_WEBSOCKET=OFF \
  -DENABLE_SCRIPTING=OFF -DENABLE_VIRTUALCAM=OFF \
  "-DSPARKLE_APPCAST_URL=$appcast_url" "-DSPARKLE_PUBLIC_KEY=$sparkle_key" \
  -DENABLE_AJA=OFF -DENABLE_DECKLINK=ON \
  -DENABLE_WEBRTC=ON -DENABLE_VST=OFF \
  -DENABLE_SYPHON=OFF -DENABLE_VLC=OFF
cmake --build "$build_dir" --config "$build_config" -j "${BUILD_JOBS:-8}"
