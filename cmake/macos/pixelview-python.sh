# Sourced by the Pixelview build and release helpers. The Python helpers need
# 3.12 (tarfile extraction filters, hashlib.file_digest), and the first python3
# on PATH can be older, so put a suitable interpreter's directory first.
pixelview_python_ok() {
  "$1" -c 'import sys; sys.exit(sys.version_info < (3, 12))' 2>/dev/null
}

pixelview_select_python() {
  local candidate
  for candidate in "$(command -v python3 || true)" /opt/homebrew/bin/python3 /usr/local/bin/python3; do
    if [[ -n "$candidate" && -x "$candidate" ]] && pixelview_python_ok "$candidate"; then
      PATH="$(dirname "$candidate"):$PATH"
      export PATH
      return 0
    fi
  done
  printf 'error: Python 3.12 or newer is required as python3 (found %s); install it with Homebrew or put it first on PATH\n' \
    "$(python3 -c 'import platform; print(platform.python_version())' 2>/dev/null || printf none)" >&2
  exit 2
}

pixelview_select_python
