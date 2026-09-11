#!/usr/bin/env python3
"""Local Developer ID build; explicitly not the clean-tag release pipeline."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import plistlib
import pwd
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build_macos"
APP = BUILD / "frontend/RelWithDebInfo/Pixelview Desktop.app"
DEFAULT_IDENTITY = "62CC493EC11F0031CF3ED9419671A78F5FD8E584"


def normal_environment():
    env = os.environ.copy()
    home = str(Path(pwd.getpwuid(os.getuid()).pw_dir))
    if Path(env.get("HOME", "")).resolve() != Path(home).resolve():
        raise RuntimeError("Run with your normal login HOME; isolated HOME is not supported")
    env["HOME"] = home
    env.pop("CFFIXED_USER_HOME", None)
    return env


def check_running():
    if BUILD.is_symlink() or APP.resolve() != APP:
        raise RuntimeError("Canonical build/app path must not contain symlinks")
    processes = output("/bin/ps", "-axo", "pid=,comm=")
    users = [line for line in processes.splitlines() if str(BUILD) + "/" in line]
    print("Running canonical bundle processes: " + ("\n".join(users) or "none"), flush=True)
    if users:
        raise RuntimeError("Canonical build directory is in use; stop the app yourself before building")


def keychain_policy(result, status, unattended):
    if result or not status & 1:
        if unattended:
            raise RuntimeError("Default Keychain is locked or unavailable; unattended preflight fails closed. No unlock or ACL change attempted.")
        print("Default Keychain is locked or unavailable; interactive build will let actual codesign request native authorization. Only the user may approve. No unlock or ACL change attempted.", flush=True)


def run(*args, **kwargs):
    kwargs.setdefault("env", normal_environment())
    return subprocess.run(args, cwd=ROOT, check=True, **kwargs)


def output(*args):
    return run(*args, capture_output=True, text=True).stdout.strip()


def resolve_identity(text, team, query=None):
    identities = set(re.findall(r'\d+\) ([A-Fa-f0-9]{40}) "(Developer ID Application: [^"\n]+)"', text))
    matches = [(sha.upper(), name) for sha, name in identities
               if name.endswith(f" ({team})")
               and (query is None or query == name or query.upper() == sha.upper())]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one valid Developer ID Application identity for {team}; found {len(matches)}")
    return matches[0]


def check_keychain(unattended=False):
    # Metadata only: never unlock, fetch a credential, export a key, or change ACLs.
    normal_environment()
    security = ctypes.CDLL("/System/Library/Frameworks/Security.framework/Security")
    cf = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
    security.SecKeychainCopyDefault.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    security.SecKeychainGetStatus.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
    cf.CFRelease.argtypes = [ctypes.c_void_p]
    keychain, status = ctypes.c_void_p(), ctypes.c_uint32()
    result = security.SecKeychainCopyDefault(ctypes.byref(keychain))
    if result:
        keychain_policy(result, 0, unattended)
        return
    try:
        result = security.SecKeychainGetStatus(keychain, ctypes.byref(status))
        print(f"Default Keychain metadata: result={result}, status={status.value}", flush=True)
        keychain_policy(result, status.value, unattended)
    finally:
        cf.CFRelease(keychain)


def source_state():
    # Bind dirty development provenance to bytes, not only HEAD or porcelain names.
    names = run("git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", capture_output=True).stdout
    digest = hashlib.sha256()
    for name in sorted(set(names.split(b"\0")) - {b""}):
        path = ROOT / os.fsdecode(name)
        digest.update(name + b"\0")
        if path.is_symlink():
            digest.update(b"symlink\0" + os.fsencode(os.readlink(path)))
        elif path.is_file():
            digest.update(str(path.stat().st_mode & 0o777).encode() + b"\0")
            with path.open("rb") as handle:
                for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                    digest.update(chunk)
        else:
            digest.update(b"missing-or-submodule")
        digest.update(b"\0")
    return {"commit": output("git", "rev-parse", "HEAD"),
            "status": output("git", "status", "--porcelain"),
            "working_tree_sha256": digest.hexdigest()}


def build_environment(sha, team, state):
    env = normal_environment()
    env["PIXELVIEW_LOCAL_SIGNING_VERIFIED"] = "1"
    # Never inherit release/updater/universal-link switches into this local path.
    env.update(PIXELVIEW_CODESIGN_IDENTITY=sha, PIXELVIEW_CODESIGN_TEAM=team,
               PIXELVIEW_BUILD_DIR=str(BUILD), PIXELVIEW_RELEASE_BUILD="OFF",
               PIXELVIEW_BUILD_CONFIG="RelWithDebInfo", PIXELVIEW_SPARKLE_APPCAST_URL="",
               PIXELVIEW_SPARKLE_PUBLIC_KEY="", PIXELVIEW_LICENSE_DATA_DIR="",
               PIXELVIEW_ENABLE_UNIVERSAL_LINKS="OFF", PIXELVIEW_ASSOCIATED_DOMAINS_PROFILE="",
               PIXELVIEW_SOURCE_COMMIT=state["commit"], PIXELVIEW_SOURCE_TAG="")
    return env


def signature_details(path):
    result = run("/usr/bin/codesign", "-d", "--verbose=4", "-r-", str(path), capture_output=True, text=True)
    return result.stdout + result.stderr


def verify(app, team, name):
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app))
    with (app / "Contents/Info.plist").open("rb") as handle:
        info = plistlib.load(handle)
    if info.get("CFBundleIdentifier") != "com.pixelview.desktop" or info.get("SUFeedURL"):
        raise RuntimeError("Unexpected bundle identity or enabled updater")
    details = signature_details(app)
    requirement = next((line for line in details.splitlines() if line.startswith("designated =>")), "")
    required = ('identifier "com.pixelview.desktop"', 'anchor apple generic',
                f'certificate leaf[subject.OU] = {team}',
                'certificate leaf[field.1.2.840.113635.100.6.1.13]')
    if not all(token in requirement for token in required) or "(runtime)" not in details:
        raise RuntimeError("Missing stable Apple Developer ID designated requirement or hardened runtime")
    entries = []
    # Include all nested Mach-O code, not just the root seal. Skip symlink aliases.
    magic = {bytes.fromhex(value) for value in ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
    for path in [app] + sorted(app.rglob("*")):
        if path != app:
            if path.is_symlink() or not path.is_file():
                continue
            with path.open("rb") as handle:
                if handle.read(4) not in magic:
                    continue
        metadata = signature_details(path)
        if f"TeamIdentifier={team}\n" not in metadata or f"Authority={name}\n" not in metadata or "Signature=adhoc" in metadata:
            raise RuntimeError(f"Wrong signer on {path}")
        run("/usr/bin/codesign", "--verify", "--strict", str(path))
        entitlements = run("/usr/bin/codesign", "-d", "--entitlements", ":-", str(path), capture_output=True).stdout
        parsed = plistlib.loads(entitlements) if entitlements.strip() else {}
        if parsed.get("com.apple.security.get-task-allow"):
            raise RuntimeError(f"Debug entitlement on {path}")
        entries.append({"path": str(path.relative_to(app)) if path != app else ".",
                        "signature": metadata, "entitlements": parsed})
    with (ROOT / "frontend/cmake/macos/entitlements.plist").open("rb") as handle:
        expected = plistlib.load(handle, fmt=plistlib.FMT_XML)
    if entries[0]["entitlements"] != expected:
        raise RuntimeError("Root entitlements differ from existing native-build entitlements")
    executable = app / "Contents/MacOS" / info["CFBundleExecutable"]
    if output("/usr/bin/lipo", "-archs", str(executable)) != "arm64":
        raise RuntimeError("Unexpected main executable architecture")
    version = run(str(executable), "--version", capture_output=True, text=True, timeout=30)
    if "Pixelview Desktop" not in version.stdout:
        raise RuntimeError("Unexpected --version output")
    return {"app": str(app), "designated_requirement": requirement, "code": entries,
            "version_stdout": version.stdout, "version_stderr": version.stderr}


def invalidate_report():
    # A failed/interrupted rebuild must not retain a previous acceptance report.
    (BUILD / "signed-development-verification.json").unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-dirty", action="store_true", help="Acknowledge non-release working-tree build")
    parser.add_argument("--source-settled", action="store_true", help="Confirm all source-editing workers have finished")
    parser.add_argument("--check-only", action="store_true", help="Read-only identity, Keychain and disk preflight; no build")
    parser.add_argument("--unattended", action="store_true", help="Fail closed on locked/unavailable Keychain metadata; never unlock it")
    parser.add_argument("--identity", default=os.environ.get("PIXELVIEW_CODESIGN_IDENTITY", DEFAULT_IDENTITY), help="Developer ID SHA-1/name; defaults to the pinned Cinecode identity")
    args = parser.parse_args()
    if sys.platform != "darwin":
        raise RuntimeError("macOS is required")
    normal_environment()
    os.environ.pop("CFFIXED_USER_HOME", None)
    check_running()
    team = json.loads((ROOT / "release/macos.json").read_text())["apple_team_id"]
    sha, name = resolve_identity(output("/usr/bin/security", "find-identity", "-v", "-p", "codesigning"), team, args.identity)
    print(f"Signer: {sha} {name}\nBuild directory: {BUILD}", flush=True)
    free = shutil.disk_usage(ROOT).free
    print(f"Free disk space: {free / 1024**3:.2f} GiB (existing .deps reused; no build-directory cloning)", flush=True)
    check_keychain(unattended=args.unattended)
    if free < 6 * 1024**3:
        raise RuntimeError("Less than 6 GiB free; free space manually before building (6 GiB is a floor, not a capacity guarantee)")
    if args.check_only:
        print("Metadata preflight passed; private-key access and build NOT tested")
        return
    if not args.source_settled:
        raise RuntimeError("Wait for source-editing workers, then pass --source-settled")
    state = source_state()
    if state["status"] and not args.allow_dirty:
        raise RuntimeError("Dirty tree: pass --allow-dirty for a local non-release build")
    check_running()
    invalidate_report()
    run("/bin/bash", "cmake/macos/pixelview-build.sh", env=build_environment(sha, team, state))
    if source_state() != state:
        raise RuntimeError("Source changed during build; wait for workers and rebuild. Artifact is not accepted")
    app = APP
    report = verify(app, team, name)
    if source_state() != state:
        raise RuntimeError("Source changed during verification; wait for workers and rebuild")
    report.update(kind="signed-development-not-release", source=state, identity_sha1=sha, team=team,
                  notarized=False, private_key_access="native build succeeded")
    report_path = BUILD / "signed-development-verification.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Verified local non-release artifact: {app}\nReport: {report_path}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(2)
