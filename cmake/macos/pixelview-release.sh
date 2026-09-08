#!/bin/bash
# Build, sign, notarize, stage, and optionally publish Pixelview Desktop for macOS.
set -euo pipefail

cd "$(dirname "$0")/../.."
root="$PWD"
version_file="$root/version.json"
release_config="$root/release/macos.json"
mode=prepare
release_notes=""
mounted_release_path=""
temporary_stage=""
downloaded_dmg=""
release_lock_dir=""
release_lock_owned=0
appcast_precondition=""
notary_key_file=""
notary_auth_args=()
compliance_stage=""
source_cache="${PIXELVIEW_SOURCE_CACHE:-$HOME/Library/Caches/pixelview-sources}"

cleanup() {
  if [[ -n "$mounted_release_path" && -d "$mounted_release_path" ]]; then
    hdiutil detach "$mounted_release_path" >/dev/null 2>&1 || true
  fi
  [[ -z "$compliance_stage" ]] || rm -rf "$compliance_stage"
  [[ -z "$temporary_stage" ]] || rm -rf "$temporary_stage"
  [[ -z "$downloaded_dmg" ]] || rm -f "$downloaded_dmg"
  [[ -z "$notary_key_file" ]] || rm -f "$notary_key_file"
  if [[ "$release_lock_owned" == 1 && -d "$release_lock_dir" ]]; then
    rm -f "$release_lock_dir/pid"
    rmdir "$release_lock_dir" || true
  fi
}
trap cleanup EXIT

usage() {
  cat <<'EOF'
Usage: cmake/macos/pixelview-release.sh [option]

  --prepare          Build, sign, notarize, staple, and generate appcast (default)
  --publish          Publish an already prepared release to R2
  --all              Prepare, then publish to R2
  --validate-config  Validate public release metadata without credentials
  --release-notes F  Use an HTML release-notes file
  --help             Show this help

Required for --prepare/--all:
  PIXELVIEW_CODESIGN_IDENTITY    Developer ID Application identity or SHA-1
  PIXELVIEW_CODESIGN_TEAM        10-character Apple team ID
  PIXELVIEW_NOTARY_KEY_ID        App Store Connect API key ID
  PIXELVIEW_NOTARY_ISSUER_ID     App Store Connect issuer UUID
  PIXELVIEW_NOTARY_PRIVATE_KEY   Contents of the AuthKey_*.p8 private key

Alternatively, direct invocations may set PIXELVIEW_NOTARY_PROFILE to an
existing notarytool Keychain profile. The 1Password wrapper uses API-key fields.

Required for --publish/--all:
  PIXELVIEW_R2_ENDPOINT         https://ACCOUNT_ID.r2.cloudflarestorage.com
  PIXELVIEW_R2_BUCKET           R2 bucket name
  AWS_ACCESS_KEY_ID             bucket-scoped R2 access key
  AWS_SECRET_ACCESS_KEY         bucket-scoped R2 secret

The script never creates Git tags, GitHub releases, R2 buckets, DNS records, or
credentials. It uploads immutable release assets first and the appcast last.
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 2
}

json_field() {
  python3 - "$1" "$2" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as handle:
    value = json.load(handle)[sys.argv[2]]
print(value)
PY
}

while (($#)); do
  case "$1" in
    --prepare) mode=prepare; shift ;;
    --publish) mode=publish; shift ;;
    --all) mode=all; shift ;;
    --validate-config) mode=validate; shift ;;
    --release-notes)
      (($# >= 2)) || die "--release-notes requires a file"
      release_notes="$2"
      shift 2
      ;;
    --help|-h) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

[[ -f "$version_file" ]] || die "missing version.json"
[[ -f "$release_config" ]] || die "missing release/macos.json"

version="$(json_field "$version_file" pixelview_version)"
build_number="$(json_field "$version_file" pixelview_build_number)"
obs_base_version="$(json_field "$version_file" obs_base_version)"
obs_base_describe="$(json_field "$version_file" obs_base_describe)"
obs_base_commit="$(json_field "$version_file" obs_base_commit)"
architecture="$(json_field "$release_config" architecture)"
appcast_url="$(json_field "$release_config" appcast_url)"
download_base_url="$(json_field "$release_config" download_base_url)"
source_repository="$(json_field "$release_config" source_repository)"
sparkle_public_key="$(json_field "$release_config" sparkle_public_key)"
sparkle_account="$(json_field "$release_config" sparkle_key_account)"
sparkle_version="$(json_field "$release_config" sparkle_version)"
sparkle_sha256="$(json_field "$release_config" sparkle_sha256)"
sparkle_generate_appcast_sha256="$(json_field "$release_config" sparkle_generate_appcast_sha256)"
sparkle_generate_keys_sha256="$(json_field "$release_config" sparkle_generate_keys_sha256)"
sparkle_sign_update_sha256="$(json_field "$release_config" sparkle_sign_update_sha256)"
apple_team_id="$(json_field "$release_config" apple_team_id)"
r2_prefix="$(json_field "$release_config" r2_prefix)"
tag="v$version"

[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "invalid Pixelview version"
[[ "$build_number" =~ ^[1-9][0-9]*$ ]] || die "invalid Pixelview build number"
[[ "$obs_base_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "invalid OBS base version"
[[ "$obs_base_describe" =~ ^${obs_base_version}-[0-9]+-g[0-9a-f]+$ ]] || die "invalid OBS base describe"
[[ "$obs_base_commit" =~ ^[0-9a-f]{40}$ ]] || die "invalid OBS base commit"
[[ "$(git describe --tags --long "$obs_base_commit" 2>/dev/null || true)" == "$obs_base_describe" ]] || die "OBS base describe does not match the pinned commit"
[[ "$architecture" == arm64 ]] || die "only arm64 releases are supported"
[[ "$appcast_url" == "https://downloads.pixelview.io/desktop/macos/appcast-arm64.xml" ]] || die "unexpected appcast URL"
[[ "$download_base_url" == "https://downloads.pixelview.io/desktop/macos" ]] || die "unexpected download base URL"
[[ "$source_repository" == "https://github.com/pxlview/pixelview-desktop" ]] || die "unexpected source repository"
[[ "$sparkle_public_key" =~ ^[A-Za-z0-9+/]{43}=$ ]] || die "invalid Sparkle public key"
[[ "$apple_team_id" =~ ^[A-Z0-9]{10}$ ]] || die "invalid Apple team ID"
for tool_hash in "$sparkle_generate_appcast_sha256" "$sparkle_generate_keys_sha256" "$sparkle_sign_update_sha256"; do
  [[ "$tool_hash" =~ ^[0-9a-f]{64}$ ]] || die "invalid Sparkle tool checksum"
done
[[ "$r2_prefix" == desktop/macos ]] || die "unexpected R2 prefix"

printf 'Pixelview Desktop %s (build %s)\n' "$version" "$build_number"
printf 'OBS %s @ %s\n' "$obs_base_describe" "$obs_base_commit"
printf 'Target: %s; feed: %s\n' "$architecture" "$appcast_url"
[[ "$mode" == validate ]] && exit 0

[[ "$(uname -s)" == Darwin ]] || die "macOS is required"
for command in git cmake codesign hdiutil security shasum spctl xcrun curl python3; do
  command -v "$command" >/dev/null || die "missing command: $command"
done

source_commit="$(git rev-parse HEAD)"
[[ -z "$(git status --porcelain)" ]] || die "working tree must be clean"
[[ "$(git rev-parse "$tag^{commit}" 2>/dev/null || true)" == "$source_commit" ]] || die "tag $tag must exist at HEAD before preparing or publishing"
git cat-file -e "$obs_base_commit^{commit}" 2>/dev/null || die "OBS base commit is unavailable locally"
git merge-base --is-ancestor "$obs_base_commit" "$source_commit" || die "OBS base commit is not an ancestor of the release"

identity="${PIXELVIEW_CODESIGN_IDENTITY:-}"
team="${PIXELVIEW_CODESIGN_TEAM:-}"
notary_profile="${PIXELVIEW_NOTARY_PROFILE:-}"
notary_key_id="${PIXELVIEW_NOTARY_KEY_ID:-}"
notary_issuer_id="${PIXELVIEW_NOTARY_ISSUER_ID:-}"
notary_private_key="${PIXELVIEW_NOTARY_PRIVATE_KEY:-}"
release_root="$root/dist/macos"
release_id="$version-$build_number"
release_dir="$release_root/releases/$release_id"
appcast_path="$release_root/appcast-arm64.xml"
dmg_name="Pixelview-Desktop-$version-build$build_number-arm64.dmg"
dmg_path="$release_dir/$dmg_name"
notes_name="Pixelview-Desktop-$version-build$build_number-arm64.html"
notes_path="$release_dir/$notes_name"
build_dir="$root/build_macos_release_${version}_${build_number}"
app_path="$build_dir/frontend/Release/Pixelview.app"

if [[ -z "$release_notes" ]]; then
  release_notes="$root/docs/releases/$version.html"
fi

acquire_release_lock() {
  mkdir -p "$release_root"
  release_lock_dir="$release_root/.release.lock"
  mkdir "$release_lock_dir" || die "another release process owns $release_lock_dir"
  release_lock_owned=1
  printf '%s\n' "$$" > "$release_lock_dir/pid"
}

verify_tool_sha256() {
  local path="$1" expected="$2" label="$3" actual
  [[ -x "$path" ]] || die "$label is missing"
  actual="$(shasum -a 256 "$path" | cut -d ' ' -f 1)"
  [[ "$actual" == "$expected" ]] || die "$label checksum mismatch"
}

ensure_sparkle_tools() {
  local cache_root="${PIXELVIEW_SPARKLE_TOOL_DIR:-$root/.runtime/release-tools/Sparkle-$sparkle_version}"
  local generate="$cache_root/bin/generate_appcast"
  local keys="$cache_root/bin/generate_keys"
  local sign_update="$cache_root/bin/sign_update"
  if [[ ! -x "$generate" || ! -x "$keys" || ! -x "$sign_update" ]]; then
    local archive="$cache_root/Sparkle-$sparkle_version.tar.xz"
    mkdir -p "$cache_root"
    curl -fL --retry 3 -o "$archive" \
      "https://github.com/sparkle-project/Sparkle/releases/download/$sparkle_version/Sparkle-$sparkle_version.tar.xz"
    local actual
    actual="$(shasum -a 256 "$archive" | cut -d ' ' -f 1)"
    [[ "$actual" == "$sparkle_sha256" ]] || die "Sparkle checksum mismatch"
    tar -xJf "$archive" -C "$cache_root"
  fi
  verify_tool_sha256 "$generate" "$sparkle_generate_appcast_sha256" generate_appcast
  verify_tool_sha256 "$keys" "$sparkle_generate_keys_sha256" generate_keys
  verify_tool_sha256 "$sign_update" "$sparkle_sign_update_sha256" sign_update
  sparkle_generate_appcast="$generate"
  sparkle_generate_keys="$keys"
  sparkle_sign_update="$sign_update"
}

verify_sparkle_key() {
  local actual
  actual="$("$sparkle_generate_keys" --account "$sparkle_account" -p | tr -d '\r\n')" || die "Sparkle private key is unavailable in Keychain"
  [[ "$actual" == "$sparkle_public_key" ]] || die "Keychain Sparkle key does not match release/macos.json"
}

verify_sparkle_signatures() {
  "$sparkle_sign_update" --account "$sparkle_account" --verify "$appcast_path" >/dev/null || die "appcast feed signature is invalid"
  local enclosure_signature
  enclosure_signature="$(python3 - "$appcast_path" <<'PY'
import sys
import xml.etree.ElementTree as ET
root = ET.parse(sys.argv[1]).getroot()
enclosure = root.find(".//enclosure")
if enclosure is None:
    raise SystemExit(2)
print(enclosure.attrib.get("{http://www.andymatuschak.org/xml-namespaces/sparkle}edSignature", ""))
PY
)"
  [[ -n "$enclosure_signature" ]] || die "appcast enclosure signature is missing"
  "$sparkle_sign_update" --account "$sparkle_account" --verify "$dmg_path" "$enclosure_signature" >/dev/null || die "DMG Sparkle signature is invalid"
  local notes_signature
  notes_signature="$(python3 - "$appcast_path" <<'PY'
import sys
import xml.etree.ElementTree as ET
sparkle = "{http://www.andymatuschak.org/xml-namespaces/sparkle}"
release_notes = ET.parse(sys.argv[1]).getroot().find(f".//{sparkle}releaseNotesLink")
if release_notes is None:
    raise SystemExit(2)
print(release_notes.attrib.get(f"{sparkle}edSignature", ""))
PY
)"
  [[ -n "$notes_signature" ]] || die "appcast release notes signature is missing"
  "$sparkle_sign_update" --account "$sparkle_account" --verify "$notes_path" "$notes_signature" >/dev/null || die "release notes Sparkle signature is invalid"
}

resolve_identity_sha1() {
  python3 - "$identity" "$apple_team_id" <<'PY'
import re
import subprocess
import sys
query, team = sys.argv[1:]
result = subprocess.run(
    ["security", "find-identity", "-v", "-p", "codesigning"],
    check=True,
    capture_output=True,
    text=True,
)
matches = []
for line in result.stdout.splitlines():
    match = re.search(r'([0-9A-F]{40})\s+"([^"]+)"', line)
    if not match:
        continue
    fingerprint, label = match.groups()
    if not label.startswith("Developer ID Application:") or f"({team})" not in label:
        continue
    if query.upper() == fingerprint or query in label:
        matches.append(fingerprint)
if len(matches) != 1:
    raise SystemExit("codesigning identity must resolve to exactly one Developer ID Application certificate for the configured team")
print(matches[0])
PY
}

certificate_sha1() {
  local bundle="$1" cert_dir fingerprint
  bundle="$(cd "$(dirname "$bundle")" && pwd)/$(basename "$bundle")"
  cert_dir="$(mktemp -d /tmp/pixelview-cert.XXXXXX)"
  fingerprint="$(cd "$cert_dir" && codesign -d --extract-certificates "$bundle" >/dev/null 2>&1 && shasum -a 1 codesign0 | cut -d ' ' -f 1)"
  rm -rf "$cert_dir"
  printf '%s\n' "$fingerprint"
}

verify_app() {
  local app_to_verify="${1:-$app_path}"
  local expected_certificate="${2:-}"
  [[ -d "$app_to_verify" ]] || die "missing application bundle: $app_to_verify"
  [[ -n "$compliance_stage" ]] || die "compliance assets have not been regenerated"
  local license_asset
  for license_asset in third-party-notices.txt source-manifest.json; do
    cmp -s "$compliance_stage/license/$license_asset" "$app_to_verify/Contents/Resources/license/$license_asset" || die "signed app compliance asset differs: $license_asset"
  done
  codesign --verify --deep --strict --verbose=2 "$app_to_verify"
  local plist="$app_to_verify/Contents/Info.plist"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")" == com.pixelview.desktop ]] || die "wrong bundle identifier"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist")" == "$version" ]] || die "wrong product version"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$plist")" == "$build_number" ]] || die "wrong build number"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :PixelviewSourceCommit' "$plist")" == "$source_commit" ]] || die "wrong source commit"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :PixelviewSourceTag' "$plist")" == "$tag" ]] || die "wrong source tag"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :PixelviewOBSBaseVersion' "$plist")" == "$obs_base_version" ]] || die "wrong OBS base version"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :PixelviewOBSBaseDescribe' "$plist")" == "$obs_base_describe" ]] || die "wrong OBS base describe"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :PixelviewOBSBaseCommit' "$plist")" == "$obs_base_commit" ]] || die "wrong OBS base commit"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :SUFeedURL' "$plist")" == "$appcast_url" ]] || die "wrong appcast URL"
  [[ "$(/usr/libexec/PlistBuddy -c 'Print :SUPublicEDKey' "$plist")" == "$sparkle_public_key" ]] || die "wrong Sparkle public key"
  [[ -d "$app_to_verify/Contents/Frameworks/Sparkle.framework" ]] || die "Sparkle.framework is missing"
  [[ "$(lipo -archs "$app_to_verify/Contents/MacOS/Pixelview")" == arm64 ]] || die "application is not arm64-only"
  codesign -d --verbose=4 "$app_to_verify" 2>&1 | grep -F "TeamIdentifier=$apple_team_id" >/dev/null || die "wrong signing team"
  if [[ -n "$expected_certificate" ]]; then
    local expected_certificate_lower
    expected_certificate_lower="$(printf '%s' "$expected_certificate" | tr '[:upper:]' '[:lower:]')"
    [[ "$(certificate_sha1 "$app_to_verify")" == "$expected_certificate_lower" ]] || die "wrong signing certificate fingerprint"
  fi
  if strings "$app_to_verify/Contents/MacOS/Pixelview" | grep -E 'obsproject\.com/(osx_update|update_studio)' >/dev/null; then
    die "application still embeds an OBS update endpoint"
  fi
}

prepare_compliance() {
  [[ -z "$compliance_stage" ]] || rm -rf "$compliance_stage"
  compliance_stage="$(mktemp -d /tmp/pixelview-compliance.XXXXXX)"
  python3 "$root/cmake/macos/pixelview_sources.py" --root "$root" --tag "$tag" \
    --cache "$source_cache" --output "$compliance_stage" --release-id "$release_id" \
    --base-url "$download_base_url" > "$compliance_stage/bindings.json" || die "corresponding-source review/materials incomplete"
  # The builder also writes license/third-party-notices.txt and
  # license/source-manifest.json for installation before code signing.
}

rebuild_compliance_bindings() {
  # Rebuild from the clean tag and pinned cache, not mutable staged metadata.
  local scratch result
  scratch="$(mktemp -d /tmp/pixelview-source-verify.XXXXXX)"
  if ! result="$(python3 "$root/cmake/macos/pixelview_sources.py" --root "$root" --tag "$tag" \
    --cache "$source_cache" --output "$scratch" --release-id "$release_id" --base-url "$download_base_url")"; then
    rm -rf "$scratch"
    die "corresponding-source verification failed"
  fi
  rm -rf "$scratch"
  printf '%s\n' "$result"
}

expected_release_json() {
  local compliance_json
  compliance_json="$(rebuild_compliance_bindings)" || die "could not rebuild compliance bindings"
  python3 - "$version" "$build_number" "$release_id" "$source_commit" "$tag" "$obs_base_version" "$obs_base_describe" "$obs_base_commit" "$apple_team_id" "$source_repository" "$dmg_name" "$dmg_path" "$compliance_json" <<'PY'
import hashlib
import json
import pathlib
import sys
version, build, release_id, commit, tag, obs_version, obs_describe, obs_commit, team, source_repository, dmg_name, dmg_path, compliance_json = sys.argv[1:]
manifest = {
    "compliance": json.loads(compliance_json),
    "product": "Pixelview Desktop",
    "version": version,
    "build_number": int(build),
    "release_id": release_id,
    "architecture": "arm64",
    "source_commit": commit,
    "source_tag": tag,
    "source_url": f"{source_repository}/tree/{tag}",
    "obs_base_version": obs_version,
    "obs_base_describe": obs_describe,
    "obs_base_commit": obs_commit,
    "apple_team_id": team,
    "artifact": dmg_name,
    "sha256": hashlib.sha256(pathlib.Path(dmg_path).read_bytes()).hexdigest(),
}
print(json.dumps(manifest, sort_keys=True))
PY
}

verify_build_progression() {
  local current_appcast_dir current_appcast http_status
  current_appcast_dir="$(mktemp -d /tmp/pixelview-current-appcast.XXXXXX)"
  current_appcast="$current_appcast_dir/appcast.xml"
  if ! http_status="$(curl -sS -L --retry 3 -o "$current_appcast" -w '%{http_code}' "$appcast_url")"; then
    rm -rf "$current_appcast_dir"
    die "current appcast could not be fetched; refusing to infer an empty feed"
  fi
  if [[ "$http_status" == 200 ]]; then
    "$sparkle_sign_update" --account "$sparkle_account" --verify "$current_appcast" >/dev/null || {
      rm -rf "$current_appcast_dir"
      die "current appcast signature is invalid"
    }
    python3 "$root/cmake/macos/pixelview_release_validate.py" --build-progression \
      "$current_appcast" "$appcast_path" "$version" "$build_number" || {
      rm -rf "$current_appcast_dir"
      die "build number does not advance the current feed"
    }
  elif [[ "$http_status" == 404 ]]; then
    python3 "$root/cmake/macos/pixelview_release_validate.py" --build-progression \
      - "$appcast_path" "$version" "$build_number" || {
      rm -rf "$current_appcast_dir"
      die "only 0.0.1 build 1 may initialize the feed"
    }
  else
    rm -rf "$current_appcast_dir"
    die "current appcast returned HTTP $http_status"
  fi
  rm -rf "$current_appcast_dir"
}

verify_prepared_release() {
  prepare_compliance
  [[ -f "$dmg_path" && -f "$dmg_path.sha256" && -f "$notes_path" && -f "$release_dir/release-manifest.json" && -f "$appcast_path" ]] || die "prepared release assets are incomplete"
  local expected_json
  expected_json="$(expected_release_json)"
  python3 "$root/cmake/macos/pixelview_release_validate.py" \
    "$release_dir" "$appcast_path" "$download_base_url" "$expected_json" || die "prepared release metadata validation failed"
  codesign --verify --verbose=2 "$dmg_path" || die "DMG code signature is invalid"
  xcrun stapler validate "$dmg_path" >/dev/null || die "DMG has no valid notarization ticket"
  spctl --assess --type open --context context:primary-signature --verbose=4 "$dmg_path" || die "Gatekeeper rejected the DMG"
  verify_sparkle_signatures
  verify_build_progression

  local mountpoint
  mountpoint="$(mktemp -d /tmp/pixelview-verify-mount.XXXXXX)"
  hdiutil attach -readonly -nobrowse -mountpoint "$mountpoint" "$dmg_path" >/dev/null || die "could not mount prepared DMG"
  mounted_release_path="$mountpoint"
  verify_app "$mountpoint/Pixelview.app"
  hdiutil detach "$mountpoint" >/dev/null || die "could not detach prepared DMG"
  mounted_release_path=""
  rmdir "$mountpoint"
}

prepare_notary_auth() {
  local api_fields=0
  [[ -n "$notary_key_id" ]] && ((api_fields += 1))
  [[ -n "$notary_issuer_id" ]] && ((api_fields += 1))
  [[ -n "$notary_private_key" ]] && ((api_fields += 1))

  if [[ "$api_fields" == 3 ]]; then
    [[ "$notary_key_id" =~ ^[A-Z0-9]{10}$ ]] || die "PIXELVIEW_NOTARY_KEY_ID must be a 10-character App Store Connect key ID"
    [[ "$notary_issuer_id" =~ ^[0-9a-fA-F-]{36}$ ]] || die "PIXELVIEW_NOTARY_ISSUER_ID must be an issuer UUID"
    umask 077
    notary_key_file="$(mktemp /tmp/pixelview-notary-key.XXXXXX)"
    printf '%s\n' "$notary_private_key" > "$notary_key_file"
    unset notary_private_key PIXELVIEW_NOTARY_PRIVATE_KEY
    grep -F -- '-----BEGIN PRIVATE KEY-----' "$notary_key_file" >/dev/null || die "1Password notarization private_key is not a .p8 private key"
    notary_auth_args=(--key "$notary_key_file" --key-id "$notary_key_id" --issuer "$notary_issuer_id")
  elif [[ "$api_fields" != 0 ]]; then
    die "all three 1Password notarization fields are required"
  elif [[ -n "$notary_profile" ]]; then
    notary_auth_args=(--keychain-profile "$notary_profile")
  else
    die "notarization credentials are required; use release/pixelview-macos.sh or PIXELVIEW_NOTARY_PROFILE"
  fi

  xcrun notarytool history "${notary_auth_args[@]}" >/dev/null 2>&1 || die "Apple notarization credentials are unusable"
}

prepare_release() {
  prepare_compliance
  [[ -n "$identity" ]] || die "PIXELVIEW_CODESIGN_IDENTITY is required"
  [[ "$team" == "$apple_team_id" ]] || die "PIXELVIEW_CODESIGN_TEAM must match the configured Apple team"
  identity="$(resolve_identity_sha1)" || die "Developer ID identity is not installed or ambiguous"
  [[ -f "$release_notes" ]] || die "missing release notes: $release_notes"
  ensure_sparkle_tools
  verify_sparkle_key
  prepare_notary_auth

  rm -rf "$release_dir" "$appcast_path"
  rm -rf -- "$build_dir"
  mkdir -p "$release_dir"

  PIXELVIEW_RELEASE_BUILD=ON \
  PIXELVIEW_BUILD_CONFIG=Release \
  PIXELVIEW_BUILD_DIR="$build_dir" \
  PIXELVIEW_CODESIGN_IDENTITY="$identity" \
  PIXELVIEW_CODESIGN_TEAM="$team" \
  PIXELVIEW_SPARKLE_APPCAST_URL="$appcast_url" \
  PIXELVIEW_SPARKLE_PUBLIC_KEY="$sparkle_public_key" \
  PIXELVIEW_SOURCE_COMMIT="$source_commit" \
  PIXELVIEW_SOURCE_TAG="$tag" \
  PIXELVIEW_LICENSE_DATA_DIR="$compliance_stage/license" \
    bash cmake/macos/pixelview-build.sh

  verify_app "$app_path" "$identity"

  local stage
  stage="$(mktemp -d /tmp/pixelview-dmg.XXXXXX)"
  temporary_stage="$stage"
  ditto "$app_path" "$stage/Pixelview.app"
  ln -s /Applications "$stage/Applications"
  cp "$root/COPYING" "$root/AUTHORS" "$stage/"
  cp "$compliance_stage/Pixelview-Desktop-$release_id-NOTICES.txt" "$stage/THIRD-PARTY-NOTICES.txt"
  cat > "$stage/RELEASE.txt" <<EOF
Pixelview Desktop $version (build $build_number)
Architecture: arm64
Source: $source_repository/tree/$tag
Source commit: $source_commit
OBS base: $obs_base_describe ($obs_base_commit)
EOF

  hdiutil create -volname "Pixelview Desktop $version" -srcfolder "$stage" -ov -format UDZO "$dmg_path"
  codesign --force --sign "$identity" --timestamp "$dmg_path"
  codesign --verify --verbose=2 "$dmg_path"
  xcrun notarytool submit "$dmg_path" "${notary_auth_args[@]}" --wait --output-format json \
    > "$release_dir/notarization.json"
  xcrun stapler staple "$dmg_path"
  xcrun stapler validate "$dmg_path"
  spctl --assess --type open --context context:primary-signature --verbose=4 "$dmg_path"

  cp "$release_notes" "$notes_path"
  "$sparkle_generate_appcast" \
    --account "$sparkle_account" \
    --download-url-prefix "$download_base_url/releases/$release_id/" \
    --release-notes-url-prefix "$download_base_url/releases/$release_id/" \
    --link "$source_repository/releases/tag/$tag" \
    --maximum-versions 10 \
    --maximum-deltas 0 \
    -o "$release_dir/appcast-arm64.xml" \
    "$release_dir"
  [[ -f "$release_dir/appcast-arm64.xml" ]] || die "Sparkle did not generate appcast-arm64.xml"
  mv "$release_dir/appcast-arm64.xml" "$appcast_path"
  grep -F "$download_base_url/releases/$release_id/$dmg_name" "$appcast_path" >/dev/null || die "appcast has the wrong download URL"
  grep -F 'sparkle:edSignature=' "$appcast_path" >/dev/null || die "appcast enclosure is unsigned"

  (cd "$release_dir" && shasum -a 256 "$dmg_name" > "$dmg_name.sha256")
  # Keep source tarballs out of Sparkle's application-archive scan above.
  cp "$compliance_stage/Pixelview-Desktop-$release_id-sources.tar.gz" \
    "$compliance_stage/Pixelview-Desktop-$release_id-NOTICES.txt" \
    "$compliance_stage/Pixelview-Desktop-$release_id-source-inventory.json" "$release_dir/"
  expected_release_json > "$release_dir/release-manifest.json"
  verify_prepared_release
  printf 'Prepared %s\nStaged appcast: %s\n' "$dmg_path" "$appcast_path"
}

verify_remote_tag() {
  local remote_commit="" hash ref
  while read -r hash ref; do
    if [[ "$ref" == "refs/tags/$tag^{}" || -z "$remote_commit" ]]; then
      remote_commit="$hash"
    fi
  done < <(git ls-remote --tags "$source_repository" "refs/tags/$tag" "refs/tags/$tag^{}")
  [[ "$remote_commit" == "$source_commit" ]] || die "push $tag to $source_repository before publishing R2 assets"
}

r2_conditional_put() {
  local endpoint="$1" bucket="$2" key="$3" local_file="$4" content_type="$5" cache_control="$6" precondition="$7"
  local response_file http_status
  response_file="$(mktemp /tmp/pixelview-r2-put.XXXXXX)"
  if ! http_status="$(curl --silent --show-error --output "$response_file" --write-out '%{http_code}' \
    --aws-sigv4 'aws:amz:auto:s3' \
    --config <(printf 'user = "%s:%s"\n' "$AWS_ACCESS_KEY_ID" "$AWS_SECRET_ACCESS_KEY") \
    --header "Content-Type: $content_type" \
    --header "Cache-Control: $cache_control" \
    --header "$precondition" \
    --upload-file "$local_file" \
    "$endpoint/$bucket/$key")"; then
    rm -f "$response_file"
    die "conditional R2 upload transport failed: $key"
  fi
  case "$http_status" in
    200|201|204) ;;
    *)
      printf 'conditional R2 upload failed for %s (HTTP %s):\n' "$key" "$http_status" >&2
      while IFS= read -r line; do printf '%s\n' "$line" >&2; done < "$response_file"
      rm -f "$response_file"
      die "R2 precondition rejected the write"
      ;;
  esac
  rm -f "$response_file"
}

preflight_immutable_object() {
  local endpoint="$1" bucket="$2" key="$3" local_file="$4"
  local error_file remote_file
  error_file="$(mktemp /tmp/pixelview-r2-head.XXXXXX)"
  if aws --endpoint-url "$endpoint" s3api head-object --bucket "$bucket" --key "$key" >/dev/null 2>"$error_file"; then
    remote_file="$(mktemp /tmp/pixelview-r2-existing.XXXXXX)"
    aws --endpoint-url "$endpoint" s3 cp "s3://$bucket/$key" "$remote_file" >/dev/null || {
      rm -f "$error_file" "$remote_file"
      die "could not download existing immutable R2 object: $key"
    }
    cmp -s "$remote_file" "$local_file" || {
      rm -f "$error_file" "$remote_file"
      die "immutable R2 object differs from the prepared asset: $key"
    }
    rm -f "$error_file" "$remote_file"
    printf 'existing\n'
    return
  fi
  if python3 - "$error_file" <<'PY'
import pathlib
import sys
message = pathlib.Path(sys.argv[1]).read_text(errors="replace")
raise SystemExit(0 if any(marker in message for marker in ("(404)", "Not Found", "NoSuchKey")) else 1)
PY
  then
    rm -f "$error_file"
    printf 'missing\n'
    return
  fi
  printf 'R2 head-object failed for %s:\n' "$key" >&2
  while IFS= read -r line; do printf '%s\n' "$line" >&2; done < "$error_file"
  rm -f "$error_file"
  die "could not prove immutable R2 object absence"
}

prepare_appcast_precondition() {
  local endpoint="$1" bucket="$2" key="$r2_prefix/appcast-arm64.xml"
  local head_file error_file current_dir current_appcast etag
  head_file="$(mktemp /tmp/pixelview-r2-appcast-head.XXXXXX)"
  error_file="$(mktemp /tmp/pixelview-r2-appcast-error.XXXXXX)"
  if aws --endpoint-url "$endpoint" s3api head-object --bucket "$bucket" --key "$key" >"$head_file" 2>"$error_file"; then
    etag="$(python3 - "$head_file" <<'PY'
import json
import sys
value = json.load(open(sys.argv[1], encoding="utf-8")).get("ETag", "")
if not value:
    raise SystemExit(2)
print(value)
PY
)" || die "existing appcast has no usable R2 ETag"
    current_dir="$(mktemp -d /tmp/pixelview-r2-appcast.XXXXXX)"
    current_appcast="$current_dir/appcast.xml"
    aws --endpoint-url "$endpoint" s3 cp "s3://$bucket/$key" "$current_appcast" >/dev/null || die "could not download current R2 appcast"
    "$sparkle_sign_update" --account "$sparkle_account" --verify "$current_appcast" >/dev/null || die "current R2 appcast signature is invalid"
    python3 "$root/cmake/macos/pixelview_release_validate.py" --build-progression \
      "$current_appcast" "$appcast_path" "$version" "$build_number" || die "staged appcast would roll back or reuse the current release"
    rm -rf "$current_dir"
    appcast_precondition="If-Match: $etag"
  elif python3 - "$error_file" <<'PY'
import pathlib
import sys
message = pathlib.Path(sys.argv[1]).read_text(errors="replace")
raise SystemExit(0 if any(marker in message for marker in ("(404)", "Not Found", "NoSuchKey")) else 1)
PY
  then
    python3 "$root/cmake/macos/pixelview_release_validate.py" --build-progression \
      - "$appcast_path" "$version" "$build_number" || die "only 0.0.1 build 1 may initialize R2"
    appcast_precondition="If-None-Match: *"
  else
    printf 'R2 head-object failed for appcast:\n' >&2
    while IFS= read -r line; do printf '%s\n' "$line" >&2; done < "$error_file"
    rm -f "$head_file" "$error_file"
    die "could not establish an atomic appcast precondition"
  fi
  rm -f "$head_file" "$error_file"
}

upload_release_assets() {
  verify_remote_tag
  verify_prepared_release
  local endpoint="${PIXELVIEW_R2_ENDPOINT:-}"
  local bucket="${PIXELVIEW_R2_BUCKET:-}"
  [[ "$endpoint" =~ ^https://[0-9a-fA-F]{32}\.r2\.cloudflarestorage\.com$ ]] || die "PIXELVIEW_R2_ENDPOINT is invalid"
  [[ -n "$bucket" && "$bucket" =~ ^[a-z0-9][a-z0-9.-]{1,61}[a-z0-9]$ ]] || die "PIXELVIEW_R2_BUCKET is invalid"
  [[ "${AWS_ACCESS_KEY_ID:-}" =~ ^[A-Za-z0-9]{16,64}$ ]] || die "AWS_ACCESS_KEY_ID is invalid"
  [[ "${AWS_SECRET_ACCESS_KEY:-}" =~ ^[A-Za-z0-9+/=_-]{32,128}$ ]] || die "AWS_SECRET_ACCESS_KEY is invalid"
  command -v aws >/dev/null || die "AWS CLI is required for R2 publishing"
  prepare_appcast_precondition "$endpoint" "$bucket"
  [[ -f "$dmg_path" && -f "$dmg_path.sha256" && -f "$notes_path" && -f "$release_dir/release-manifest.json" ]] || die "prepared release assets are incomplete"

  local key_prefix="$r2_prefix/releases/$release_id"
  local files=("$dmg_path" "$dmg_path.sha256" "$notes_path" "$release_dir/release-manifest.json")
  local keys=("$key_prefix/$dmg_name" "$key_prefix/$dmg_name.sha256" "$key_prefix/$notes_name" "$key_prefix/release-manifest.json")
  local content_types=("application/x-apple-diskimage" "text/plain" "text/html" "application/json")
  local compliance_name
  for compliance_name in "Pixelview-Desktop-$release_id-sources.tar.gz" "Pixelview-Desktop-$release_id-NOTICES.txt" "Pixelview-Desktop-$release_id-source-inventory.json"; do
    files+=("$release_dir/$compliance_name")
    keys+=("$key_prefix/$compliance_name")
  done
  content_types+=("application/gzip" "text/plain; charset=utf-8" "application/json")
  local states=() state i

  # Check every immutable key before writing any of them. Existing identical
  # objects make publication retryable; a conflict aborts before the first write.
  for ((i = 0; i < ${#files[@]}; i++)); do
    state="$(preflight_immutable_object "$endpoint" "$bucket" "${keys[$i]}" "${files[$i]}")"
    states[$i]="$state"
  done
  for ((i = 0; i < ${#files[@]}; i++)); do
    if [[ "${states[$i]}" == missing ]]; then
      r2_conditional_put "$endpoint" "$bucket" "${keys[$i]}" "${files[$i]}" \
        "${content_types[$i]}" 'public,max-age=31536000,immutable' 'If-None-Match: *'
    fi
  done

  local public_file
  for ((i = 0; i < ${#files[@]}; i++)); do
    public_file="$(mktemp /tmp/pixelview-r2-public.XXXXXX)"
    curl -fL --retry 3 -o "$public_file" "$download_base_url/releases/$release_id/$(basename "${keys[$i]}")"
    cmp -s "$public_file" "${files[$i]}" || {
      rm -f "$public_file"
      die "public R2 object differs after upload: ${keys[$i]}"
    }
    rm -f "$public_file"
  done
}

upload_appcast() {
  local endpoint="${PIXELVIEW_R2_ENDPOINT:-}"
  local bucket="${PIXELVIEW_R2_BUCKET:-}"
  [[ -f "$appcast_path" ]] || die "missing staged appcast"
  [[ -n "$appcast_precondition" ]] || die "appcast write precondition was not established"
  r2_conditional_put "$endpoint" "$bucket" "$r2_prefix/appcast-arm64.xml" "$appcast_path" \
    application/xml 'no-cache, max-age=0, must-revalidate' "$appcast_precondition"
  local downloaded_appcast
  downloaded_appcast="$(mktemp /tmp/pixelview-appcast-verify.XXXXXX)"
  curl -fL --retry 3 -o "$downloaded_appcast" "$appcast_url"
  cmp -s "$downloaded_appcast" "$appcast_path" || die "public R2 appcast differs from the staged appcast"
  rm -f "$downloaded_appcast"
  printf 'Published %s after verifying immutable release assets.\n' "$appcast_url"
}

acquire_release_lock
case "$mode" in
  prepare) prepare_release ;;
  publish)
    ensure_sparkle_tools
    verify_sparkle_key
    upload_release_assets
    upload_appcast
    ;;
  all)
    prepare_release
    upload_release_assets
    upload_appcast
    ;;
  *) die "unsupported mode" ;;
esac
