#!/usr/bin/env python3
"""Launch only the canonical signed app, with normal HOME and separate app settings."""
import argparse
import configparser
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

SPEC = importlib.util.spec_from_file_location("signed_build", Path(__file__).with_name("pixelview-signed-development.py"))
assert SPEC is not None and SPEC.loader is not None
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)

# Intentionally narrow: never copy scenes, services, pairing/receiver data,
# arbitrary plugin fields, keychains, or full INI files from a private HOME.
SAFE_SETTINGS = {
    "Video": {"BaseCX", "BaseCY", "OutputCX", "OutputCY", "FPSType", "FPSCommon", "FPSInt", "FPSNum", "FPSDen", "ColorFormat", "ColorSpace", "ColorRange", "ScaleType"},
    "Audio": {"SampleRate", "ChannelSetup"},
}


def migrate_settings(source, destination):
    """New root only; retain originals and save a filtered snapshot for rollback."""
    if not source.is_absolute() or not destination.is_absolute():
        raise RuntimeError("Migration requires absolute Pixelview config roots (not HOME directories)")
    if not source.is_dir() or source.is_symlink() or destination.exists():
        raise RuntimeError("Source must exist and destination must be new; no settings are overwritten")
    profiles = source / "obs-studio/basic/profiles"
    snapshots = {}
    for path in sorted(profiles.glob("*/basic.ini")):
        if path.is_symlink() or path.parent.is_symlink() or not path.resolve().is_relative_to(source.resolve()):
            raise RuntimeError("Refusing symlinked profile")
        original = configparser.ConfigParser(interpolation=None, strict=False)
        original.optionxform = lambda optionstr: optionstr
        original.read(path, encoding="utf-8-sig")
        filtered = {section: {key: original.get(section, key) for key in keys if original.has_option(section, key)}
                    for section, keys in SAFE_SETTINGS.items()}
        snapshots[str(path.relative_to(source))] = {s: v for s, v in filtered.items() if v}
    if not any(snapshots.values()):
        raise RuntimeError("No allowlisted video/audio settings found; nothing copied")
    destination.mkdir(parents=True, mode=0o700, exist_ok=False)
    backup = destination / "migration-settings-backup.json"
    backup.write_text(json.dumps(snapshots, indent=2) + "\n")
    backup.chmod(0o600)
    for relative, settings in snapshots.items():
        target = destination / relative
        target.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
        filtered = configparser.ConfigParser(interpolation=None)
        filtered.optionxform = lambda optionstr: optionstr
        filtered.read_dict(settings)
        with target.open("x", encoding="utf-8") as handle:
            filtered.write(handle)
        target.chmod(0o600)
    print(f"Migrated allowlisted audio/video settings to {destination}; filtered backup: {backup}. Originals untouched. Re-select hardware/scenes manually; no credentials copied.")


def launch_command(config_root):
    command = [str(BUILD.APP / "Contents/MacOS/Pixelview Desktop")]
    if config_root:
        if not config_root.is_absolute():
            raise RuntimeError("--app-config-dir requires an absolute path")
        command += ["--multi", "--app-config-dir", str(config_root)]
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-config-dir", type=Path)
    parser.add_argument("--check-only", action="store_true", help="Verify signature and print command; do not launch")
    parser.add_argument("--migrate-from", type=Path, help="Copy only allowlisted audio/video settings into a NEW config root, then exit")
    args = parser.parse_args()
    env = BUILD.normal_environment()
    if args.migrate_from:
        if not args.app_config_dir or args.check_only:
            raise RuntimeError("Migration needs --app-config-dir and cannot be combined with --check-only")
        migrate_settings(args.migrate_from, args.app_config_dir)
        return
    if BUILD.APP.resolve() != BUILD.APP:
        raise RuntimeError("Canonical app path must not contain symlinks")
    command = launch_command(args.app_config_dir)
    team = json.loads((BUILD.ROOT / "release/macos.json").read_text())["apple_team_id"]
    report_path = BUILD.BUILD / "signed-development-verification.json"
    report = json.loads(report_path.read_text())
    if report.get("app") != str(BUILD.APP) or report.get("identity_sha1") != BUILD.DEFAULT_IDENTITY or report.get("team") != team:
        raise RuntimeError("No matching canonical Developer ID build report; rebuild before launch")
    name = f"Developer ID Application: Cinecode OU ({team})"
    # Full verification is read-only apart from harmless --version; no credential access.
    actual = BUILD.verify(BUILD.APP, team, name)
    if actual["code"] != report["code"]:
        raise RuntimeError("Bundle differs from verified build report; rebuild before launch")
    print("Canonical app: " + str(BUILD.APP), flush=True)
    if args.check_only:
        print(json.dumps({"command": command, "HOME": env["HOME"], "CFFIXED_USER_HOME": None}))
        return
    os.execve(command[0], command, env)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(2)
