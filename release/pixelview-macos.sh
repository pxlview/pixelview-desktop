#!/bin/bash
# Authenticate with 1Password, inject only the credentials needed by the selected release phase, and run it locally.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
release_script="$root/cmake/macos/pixelview-release.sh"
config_file="$root/release/macos.env"
r2_file="$root/release/macos-r2.1password.env"
notary_file="$root/release/macos-notary.1password.env"
mode=""
for argument in "$@"; do
  case "$argument" in
    --prepare|--publish|--all|--validate-config) mode="$argument" ;;
  esac
done

case "$mode" in
  --validate-config)
    exec "$release_script" "$@"
    ;;
  --prepare)
    secret_files=(--env-file "$notary_file")
    ;;
  --publish)
    secret_files=(--env-file "$r2_file")
    ;;
  --all)
    secret_files=(--env-file "$notary_file" --env-file "$r2_file")
    ;;
  *)
    exec "$release_script" "$@"
    ;;
esac

command -v op >/dev/null || {
  printf 'error: 1Password CLI (op) is required\n' >&2
  exit 2
}
for required_file in "$config_file" "${secret_files[@]}"; do
  [[ "$required_file" == --env-file ]] && continue
  [[ -f "$required_file" ]] || {
    printf 'error: missing release environment file: %s\n' "$required_file" >&2
    exit 2
  }
done

# With 1Password desktop integration enabled, `op run` asks for biometric or
# system verification when the account is locked. Values exist only in this
# subprocess and are masked if a child command accidentally prints one.
exec op run --env-file "$config_file" "${secret_files[@]}" -- "$release_script" "$@"
