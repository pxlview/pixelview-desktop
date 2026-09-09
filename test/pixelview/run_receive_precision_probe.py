#!/usr/bin/env python3
"""Offline existing-libobs probe; no building/staging production targets."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / 'build_macos/libobs/RelWithDebInfo'
DEPS = ROOT / '.deps/obs-deps-2026-08-26-universal'
env = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
with tempfile.TemporaryDirectory(prefix='pixelview-precision-') as tmp:
    env.update(HOME=tmp, CFFIXED_USER_HOME=tmp)
    exe = str(Path(tmp) / 'probe')
    subprocess.run(['xcrun', 'clang++', '-std=c++17', '-I'+str(ROOT/'libobs'),
                    '-I'+str(ROOT/'build_macos/config'), '-I'+str(ROOT/'build_macos/libobs'),
                    '-I'+str(DEPS/'include'), str(Path(__file__).with_name('receive_precision_probe.mm')),
                    '-F'+str(FW), '-framework', 'libobs', '-framework', 'Foundation',
                    '-Wl,-rpath,'+str(FW), '-Wl,-rpath,'+str(DEPS/'lib'), '-o', exe], check=True, env=env)
    for source in ('P010', 'I210'):
        for canvas in ('NV12', 'P010'):
            subprocess.run([exe, str(ROOT/'libobs/data'),
                            str(ROOT/'build_macos/libobs-opengl/RelWithDebInfo/libobs-opengl.dylib'),
                            canvas, source], check=True, env=env, timeout=25)
