#!/bin/bash
# Local experimental Pixelview build; does not change xcode-select or install OBS.
set -euo pipefail
cd "$(dirname "$0")/../.."
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
cmake --preset macos \
  -DOBS_VERSION_OVERRIDE=32.1.0 \
  -DPIXELVIEW_LEGACY_TOOLCHAIN=ON \
  -DENABLE_BROWSER=OFF -DENABLE_WEBSOCKET=OFF \
  -DENABLE_SCRIPTING=OFF -DENABLE_VIRTUALCAM=OFF \
  -DSPARKLE_APPCAST_URL= -DSPARKLE_PUBLIC_KEY= \
  -DENABLE_AJA=OFF -DENABLE_DECKLINK=ON \
  -DENABLE_WEBRTC=OFF -DENABLE_VST=OFF \
  -DENABLE_SYPHON=OFF -DENABLE_VLC=OFF
cmake --build build_macos --config RelWithDebInfo -j "${BUILD_JOBS:-8}"

