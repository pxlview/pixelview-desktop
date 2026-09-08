#!/usr/bin/env python3
"""Validate official runtime with actual production module + native codec callbacks.
Does not configure/build Xcode or change the app's normal staging directory.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=REPO/'.deps/pixelview-gstreamer-upstream')
    parser.add_argument('--obs-build', type=Path, default=REPO/'build_macos')
    parser.add_argument('--live', action='store_true', help='also receive live WHEP; authorized URL on stdin, never argv')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('bundle_runtime', ROOT/'scripts/bundle-runtime.py')
    pack = importlib.util.module_from_spec(spec); spec.loader.exec_module(pack)
    work = REPO/'.deps/upstream-native'
    stage = work/'runtime'
    work.mkdir(parents=True, exist_ok=True)
    if stage.exists(): shutil.rmtree(stage)
    shutil.copytree(args.runtime, stage)
    pack.verify(stage)
    contents = work/'Upstream.plugin/Contents'
    (contents/'MacOS').mkdir(parents=True, exist_ok=True)
    framework = args.obs_build/'libobs/RelWithDebInfo'
    deps = sorted((REPO/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
    base = ['clang', '-arch', 'arm64', '-mmacosx-version-min=14.0', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-parameter', '-I'+str(REPO/'libobs'), '-I'+str(args.obs_build/'config'),
            '-I'+str(deps/'include'), '-F'+str(framework), '-framework', 'libobs',
            '-Wl,-rpath,'+str(framework), '-Wl,-rpath,'+str(deps/'lib'), '-Wl,-headerpad_max_install_names']
    includes = ['-I'+str(pack.GST/p) for p in ['include/gstreamer-1.0', 'include/glib-2.0', 'lib/glib-2.0/include']]
    libs = ['-L'+str(stage/'lib'), '-Wl,-rpath,'+str(stage/'lib')]+['-l'+x for x in
            ['gstapp-1.0.0','gstvideo-1.0.0','gstaudio-1.0.0','gstbase-1.0.0','gstreamer-1.0.0','gobject-2.0.0','glib-2.0.0']]
    binary = contents/'MacOS/pixelview-whep'
    subprocess.run(base+includes+['-bundle', str(ROOT/'pixelview-whep.c'), '-o', str(binary)]+libs, check=True)
    pack.embed(stage, contents, binary, '-')
    subprocess.run(['codesign','--force','--sign','-',str(binary)],check=True)
    subprocess.run(['codesign','--verify','--strict',str(binary)],check=True)
    env = {k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
    module = work/'module'
    subprocess.run(base+[str(ROOT/'tests/module.c'),'-o',str(module)],check=True)
    subprocess.run([str(module),str(binary)],env=env,check=True)
    # The codec executable and its linked runtime must be the same single copy.
    codec = work/'Codec.plugin/Contents'
    (codec/'MacOS').mkdir(parents=True,exist_ok=True)
    (codec/'Resources').mkdir(exist_ok=True)
    link=codec/'Resources/GStreamer'
    if link.is_symlink(): link.unlink()
    link.symlink_to(stage)
    executable=codec/'MacOS/codecs'
    subprocess.run(base+includes+[str(ROOT/'tests/codecs.c'),'-o',str(executable)]+libs,check=True)
    subprocess.run([str(executable)],env=env,check=True)
    print('PASS official runtime: real production module, jitter50, H264, HEVC, Opus; no app rebuild')
    live = codec/'MacOS/live-source'
    subprocess.run(base+includes+[str(ROOT/'tests/live-source.c'),'-o',str(live)]+libs,check=True)
    if args.live:
        endpoint = sys.stdin.readline().strip()
        if not endpoint:
            raise RuntimeError('Missing authorized WHEP URL on stdin')
        log = work/'live-private.log'
        fd = os.open(log,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600); os.fchmod(fd,0o600)
        with os.fdopen(fd,'w') as out:
            result = subprocess.run([str(live)],input=endpoint+'\n',text=True,stdout=out,
                                    stderr=subprocess.STDOUT,env=env,timeout=60)
        for line in log.read_text().splitlines():
            if line.startswith('LIVE_RESULT '): print(line)
        result.check_returncode()


if __name__ == '__main__':
    main()
