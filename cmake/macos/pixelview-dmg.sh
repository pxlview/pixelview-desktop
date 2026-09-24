#!/usr/bin/env bash
# Builds the Pixelview Desktop install DMG from a staged folder that holds
# "Pixelview Desktop.app", the "Applications" symlink and a "Licenses" folder.
# Adds the background, has Finder lay out the install window (app -> Applications,
# Licenses below) and compresses the result. Signing and notarization stay with
# pixelview-release.sh.
#
#   cmake/macos/pixelview-dmg.sh <stage-dir> <volume-name> <output.dmg>
#
# Finder does the layout, so the first run asks to let the terminal control
# Finder (System Settings > Privacy & Security > Automation).
set -euo pipefail

[[ $# -eq 3 ]] || { echo "usage: $0 <stage-dir> <volume-name> <output.dmg>" >&2; exit 2; }
stage="$1" volume_name="$2" output="$3"
here="$(cd "$(dirname "$0")" && pwd)"

for item in "Pixelview Desktop.app" Applications Licenses; do
  [[ -e "$stage/$item" || -L "$stage/$item" ]] || { echo "error: stage is missing $item" >&2; exit 1; }
done
# Finder addresses the disk by name; another mounted volume with it would be laid out instead.
[[ ! -e "/Volumes/$volume_name" ]] || { echo "error: /Volumes/$volume_name is already mounted; eject it first" >&2; exit 1; }

work="$(mktemp -d /tmp/pixelview-dmg-rw.XXXXXX)"
mountpoint=""
cleanup() {
  [[ -z "$mountpoint" ]] || hdiutil detach "$mountpoint" -force >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

mkdir -p "$stage/.background"
cp "$here/resources/pixelview-dmg-background.tiff" "$stage/.background/background.tiff"

# Headroom for .DS_Store and the filesystem; the image is compressed afterwards.
size_mb=$(( $(du -sm "$stage" | cut -f1) + 64 ))
hdiutil create -volname "$volume_name" -srcfolder "$stage" -fs HFS+ -format UDRW -size "${size_mb}m" -ov "$work/rw.dmg" >/dev/null
mountpoint="/Volumes/$volume_name"
hdiutil attach "$work/rw.dmg" -readwrite -noverify -noautoopen -mountpoint "$mountpoint" >/dev/null

osascript "$here/pixelview-dmg-layout.applescript" "$volume_name"
# Finder writes .DS_Store asynchronously; wait for it before detaching.
for _ in $(seq 1 20); do
  [[ -f "$mountpoint/.DS_Store" ]] && break
  sleep 0.5
done
[[ -f "$mountpoint/.DS_Store" ]] || { echo "error: Finder did not record the window layout" >&2; exit 1; }
rm -rf "$mountpoint/.fseventsd" "$mountpoint/.Trashes"
sync

hdiutil detach "$mountpoint" >/dev/null
mountpoint=""
hdiutil convert "$work/rw.dmg" -format UDZO -imagekey zlib-level=9 -ov -o "$output" >/dev/null
rm -rf "$stage/.background"
echo "Created $output"
