#!/bin/bash
# Local experimental Pixelview build; does not change xcode-select or install OBS.
set -euo pipefail
cd "$(dirname "$0")/../.."
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
identity="${PIXELVIEW_CODESIGN_IDENTITY:--}"
team="${PIXELVIEW_CODESIGN_TEAM:-}"
build_dir=build_macos
if [[ "$identity" != "-" ]]; then
  if [[ ! "$team" =~ ^[A-Z0-9]{10}$ ]] ||
     [[ "$identity" != "Developer ID Application: "* && ! "$identity" =~ ^[A-Fa-f0-9]{40}$ ]]; then
    printf '%s\n' 'Set PIXELVIEW_CODESIGN_IDENTITY to a Developer ID Application name or SHA-1 and PIXELVIEW_CODESIGN_TEAM to its 10-character team ID.' >&2
    exit 2
  fi
  build_dir=build_macos_developer_id
elif [[ -n "$team" ]]; then
  printf '%s\n' 'PIXELVIEW_CODESIGN_TEAM requires an explicit Developer ID Application identity.' >&2
  exit 2
fi
build_dir="${PIXELVIEW_BUILD_DIR:-$build_dir}"
# Use upstream manual Xcode signing, including sign-on-copy for dependencies.
# Explicit defaults prevent preset environment/cache from enabling signing.
cmake --preset macos -B "$build_dir" \
  "-DOBS_CODESIGN_IDENTITY=$identity" "-DOBS_CODESIGN_TEAM=$team" \
  -DOBS_PROVISIONING_PROFILE= \
  -DOBS_VERSION_OVERRIDE=32.1.0 \
  -DPIXELVIEW_LEGACY_TOOLCHAIN=ON \
  -DENABLE_BROWSER=OFF -DENABLE_WEBSOCKET=OFF \
  -DENABLE_SCRIPTING=OFF -DENABLE_VIRTUALCAM=OFF \
  -DSPARKLE_APPCAST_URL= -DSPARKLE_PUBLIC_KEY= \
  -DENABLE_AJA=OFF -DENABLE_DECKLINK=ON \
  -DENABLE_WEBRTC=ON -DENABLE_VST=OFF \
  -DENABLE_SYPHON=OFF -DENABLE_VLC=OFF
cmake --build "$build_dir" --config RelWithDebInfo -j "${BUILD_JOBS:-8}"

