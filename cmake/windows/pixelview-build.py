#!/usr/bin/env python3
"""Pixelview Windows build helper (x64; Visual Studio 2026 generator from the windows-x64 preset).

Development builds keep updates disabled and are unsigned. Release builds
(PIXELVIEW_RELEASE_BUILD=ON, driven by pixelview-release.py) compile in the
production WinSparkle appcast and public key and fail closed otherwise.

  python cmake/windows/pixelview-build.py              # configure + build RelWithDebInfo
  python cmake/windows/pixelview-build.py --print      # show the configure command only
  python cmake/windows/pixelview-build.py --check-only # preflight; no build

Output: build_x64/rundir/<config>/bin/64bit/Pixelview.exe
"""
import argparse
import hashlib
import io
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONFIGS = ('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')

# Mirrors cmake/macos/pixelview-build.sh: Pixelview ships no browser, What's New,
# obs-websocket, scripting, virtual camera, AJA, VST or VLC.
FEATURE_FLAGS = [
    '-DENABLE_BROWSER=OFF', '-DENABLE_WHATSNEW=OFF', '-DENABLE_WEBSOCKET=OFF',
    '-DENABLE_SCRIPTING=OFF', '-DENABLE_VIRTUALCAM=OFF',
    '-DENABLE_AJA=OFF', '-DENABLE_DECKLINK=ON',
    '-DENABLE_WEBRTC=ON', '-DENABLE_VST=OFF', '-DENABLE_VLC=OFF',
]


def die(message):
    print(f'error: {message}', file=sys.stderr)
    sys.exit(2)


def read_json(path):
    with open(ROOT / path, encoding='utf-8') as handle:
        return json.load(handle)


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def stage_winsparkle(config):
    """Download the pinned WinSparkle package once into .deps and verify its hash."""
    version = config['winsparkle_version']
    target = ROOT / '.deps' / f'winsparkle-{version}'
    marker = target / '.sha256'
    if marker.is_file() and marker.read_text().strip() == config['winsparkle_sha256']:
        return target
    print(f'Staging WinSparkle {version}')
    with urllib.request.urlopen(config['winsparkle_url'], timeout=120) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != config['winsparkle_sha256']:
        die(f'WinSparkle download hash mismatch ({digest})')
    if target.exists():
        shutil.rmtree(target)
    prefix = f'WinSparkle-{version}/'
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for member in archive.infolist():
            name = member.filename
            if not name.startswith(prefix) or member.is_dir():
                continue  # Skips the archive's __MACOSX metadata.
            relative = pathlib.PurePosixPath(name[len(prefix):])
            if relative.is_absolute() or '..' in relative.parts:
                die(f'unsafe path in WinSparkle archive: {name}')
            destination = target.joinpath(*relative.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(archive.read(member))
    marker.write_text(config['winsparkle_sha256'] + '\n')
    return target


def configure_command(args, versions, config, winsparkle):
    release = args.release
    appcast = config['appcast_url'] if release else args.staging_feed
    key = config['winsparkle_public_key'] if release or args.staging_feed else ''
    source_commit = os.environ.get('PIXELVIEW_SOURCE_COMMIT') or git('rev-parse', 'HEAD')
    source_tag = os.environ.get('PIXELVIEW_SOURCE_TAG')
    if source_tag is None:
        source_tag = subprocess.run(['git', 'describe', '--exact-match', '--tags', 'HEAD'], cwd=ROOT,
                                    capture_output=True, text=True).stdout.strip()
    return [
        'cmake', '--preset', 'windows-x64', '-B', str(args.build_dir),
        # libobs must see the OBS base version, not `git describe` of this fork.
        f"-DOBS_VERSION_OVERRIDE={versions['obs_base_version']}",
        f"-DPIXELVIEW_RELEASE_BUILD={'ON' if release else 'OFF'}",
        f"-DPIXELVIEW_UPDATE_STAGING={'ON' if args.staging_feed else 'OFF'}",
        f'-DPIXELVIEW_STAGING_BUILD_NUMBER={args.staging_build}',
        f'-DPIXELVIEW_SOURCE_COMMIT={source_commit}',
        f'-DPIXELVIEW_SOURCE_TAG={source_tag}',
        f"-DPIXELVIEW_LICENSE_DATA_DIR={os.environ.get('PIXELVIEW_LICENSE_DATA_DIR', '')}",
        f'-DSPARKLE_APPCAST_URL={appcast}',
        f'-DSPARKLE_PUBLIC_KEY={key}',
        f"-DPIXELVIEW_WINSPARKLE_ROOT={winsparkle or ''}",
        *FEATURE_FLAGS,
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--print', action='store_true', help='print the configure command and exit')
    parser.add_argument('--check-only', action='store_true', help='preflight only')
    parser.add_argument('--config', default=os.environ.get('PIXELVIEW_BUILD_CONFIG', 'RelWithDebInfo'), choices=CONFIGS)
    parser.add_argument('--jobs', default=os.environ.get('BUILD_JOBS', ''))
    args = parser.parse_args()
    args.release = os.environ.get('PIXELVIEW_RELEASE_BUILD', 'OFF') == 'ON'
    # Local update tests (set only by pixelview-release.py --staging-build).
    args.staging_feed = os.environ.get('PIXELVIEW_UPDATE_STAGING_FEED', '')
    args.staging_build = os.environ.get('PIXELVIEW_STAGING_BUILD_NUMBER', '')
    versions = read_json('version.json')
    config = read_json('release/windows.json')
    if args.release:
        if args.config != 'Release':
            die('release builds use the Release configuration')
        if not config['winsparkle_public_key']:
            die('release/windows.json has no winsparkle_public_key; generate the update key first')
        args.build_dir = ROOT / f"build_x64_release_{versions['pixelview_version']}_{versions['pixelview_build_number']}"
    elif args.staging_feed:
        if not args.staging_build.isdigit() or int(args.staging_build) < 1:
            die('staging builds need PIXELVIEW_STAGING_BUILD_NUMBER')
        if not config['winsparkle_public_key']:
            die('release/windows.json has no winsparkle_public_key')
        args.build_dir = ROOT / f'build_x64_staging_{args.staging_build}'
    else:
        args.build_dir = ROOT / 'build_x64'

    winsparkle = None
    if args.print:
        print(subprocess.list2cmdline(configure_command(args, versions, config, '<staged WinSparkle>' if args.release or args.staging_feed else None)))
        return 0
    if platform.system() != 'Windows':
        die('Windows builds run on Windows with Visual Studio 2026 (C++ workload, Windows SDK 10.0.26100), x64 or ARM64 host')
    if sys.version_info < (3, 12):
        die('Python 3.12 or newer is required')
    for tool in ('cmake', 'git'):
        if not shutil.which(tool):
            die(f'{tool} is not on PATH')
    free = shutil.disk_usage(ROOT).free
    if free < 15 * 1024**3:
        die(f'at least 15 GiB of free disk is required ({free // 1024**3} GiB free)')
    if args.check_only:
        print('Preflight OK')
        return 0

    # win-dshow builds from the pinned libdshowcapture submodule.
    subprocess.run(['git', 'submodule', 'update', '--init', '--recursive', 'deps/libdshowcapture/src'], cwd=ROOT, check=True)
    if args.release or args.staging_feed:
        winsparkle = stage_winsparkle(config)
    subprocess.run(configure_command(args, versions, config, winsparkle), cwd=ROOT, check=True)
    build = ['cmake', '--build', str(args.build_dir), '--config', args.config]
    if args.jobs:
        build += ['--parallel', str(args.jobs)]
    subprocess.run(build, cwd=ROOT, check=True)
    exe = args.build_dir / 'rundir' / args.config / 'bin' / '64bit' / 'Pixelview.exe'
    if not exe.is_file():
        die(f'build finished without {exe}')
    print(f'Built {exe}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
