#!/usr/bin/env python3
"""Launch the Windows development build, optionally into its own settings root and paired unattended.

  python cmake/windows/pixelview-launch.py                                  # default settings root
  python cmake/windows/pixelview-launch.py --app-config-dir C:\\pv-test      # separate settings root
  python cmake/windows/pixelview-launch.py --app-config-dir C:\\pv-test --pair-origin http://localhost:8010 --pair-code ABC123
  python cmake/windows/pixelview-launch.py ... --check-only                 # print, do not launch

Pairing (docs/features.md, "Unattended pairing") replaces the settings root's pairing, so it
needs --app-config-dir: a real pairing in the default root is never replaced by this helper. The
code reaches the app only through its environment, never its command line, and implies
PIXELVIEW_LOCAL_DEVELOPMENT=1. The app starts detached in the caller's session; run this from the
interactive desktop session for its window to be visible.
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONFIGS = ('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')
PAIRING = ('PIXELVIEW_PAIR_CODE', 'PIXELVIEW_PAIR_ORIGIN')


def die(message):
    print(f'error: {message}', file=sys.stderr)
    sys.exit(2)


def executable(config):
    return ROOT / 'build_x64' / 'rundir' / config / 'bin' / '64bit' / 'Pixelview.exe'


def plan(args, environ):
    """Command and environment for the app; refuses unsafe combinations."""
    env = {key: value for key, value in environ.items() if key not in PAIRING}
    code = (args.pair_code or environ.get('PIXELVIEW_PAIR_CODE', '')).strip()
    origin = (args.pair_origin or environ.get('PIXELVIEW_PAIR_ORIGIN', '')).strip()
    if origin and not code:
        die('--pair-origin needs a pairing code (--pair-code or PIXELVIEW_PAIR_CODE)')
    command = [str(executable(args.config))]
    if args.app_config_dir:
        # Windows-style absolute paths are checked as such on every host (--check-only tests).
        if not pathlib.PureWindowsPath(args.app_config_dir).is_absolute():
            die('--app-config-dir requires an absolute path')
        command += ['--multi', '--app-config-dir', args.app_config_dir]
    if code:
        if not args.app_config_dir:
            die('pairing replaces the saved pairing; use it only with --app-config-dir')
        env['PIXELVIEW_PAIR_CODE'] = code
        if origin:
            env['PIXELVIEW_PAIR_ORIGIN'] = origin
    if code or args.local_development:
        env['PIXELVIEW_LOCAL_DEVELOPMENT'] = '1'
    return command, env


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--config', choices=CONFIGS, default='RelWithDebInfo')
    parser.add_argument('--app-config-dir', help='absolute settings root (passed with --multi)')
    parser.add_argument('--local-development', action='store_true', help='set PIXELVIEW_LOCAL_DEVELOPMENT=1')
    parser.add_argument('--pair-origin', help='backend origin to pair with (default http://localhost:8000)')
    parser.add_argument('--pair-code', help='one-time admin pairing code (or PIXELVIEW_PAIR_CODE)')
    parser.add_argument('--wait', action='store_true', help='wait for the app to exit and return its exit code')
    parser.add_argument('--check-only', action='store_true', help='print the command and environment; do not launch')
    args = parser.parse_args()
    command, env = plan(args, os.environ)
    exe = pathlib.Path(command[0])
    if args.check_only:
        shown = {key: ('<set>' if key == 'PIXELVIEW_PAIR_CODE' else env[key])
                 for key in ('PIXELVIEW_LOCAL_DEVELOPMENT', *PAIRING) if key in env}
        print(json.dumps({'command': command, 'exists': exe.is_file(), 'environment': shown}))
        return 0
    if sys.platform != 'win32':
        die('launching needs Windows; use --check-only elsewhere')
    if not exe.is_file():
        die(f'{exe} not found; build with python cmake/windows/pixelview-build.py')
    if args.app_config_dir:
        pathlib.Path(args.app_config_dir).mkdir(parents=True, exist_ok=True)
    process = subprocess.Popen(command, cwd=exe.parent, env=env, close_fds=True,
                               creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    root = args.app_config_dir or os.path.join(os.environ.get('APPDATA', ''), 'pixelview')
    print(f'Started {exe.name} (PID {process.pid}); logs in {os.path.join(root, "obs-studio", "logs")}', flush=True)
    return process.wait() if args.wait else 0


if __name__ == '__main__':
    sys.exit(main())
