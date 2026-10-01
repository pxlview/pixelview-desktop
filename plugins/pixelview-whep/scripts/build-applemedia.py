#!/usr/bin/env python3
"""Build ONLY the pinned applemedia plugin (vtdec) with Pixelview's patch (HEVC
reorder depth from the SPS, v210 output for 4:2:2 streams); never stage. Requires the locally extracted official
GStreamer SDK; meson and ninja are pinned in a private virtualenv.
The default output is separate from the mutable embedded runtime.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
VERSION = '1.28.3'
SOURCE_URL = f'https://gstreamer.freedesktop.org/src/gst-plugins-bad/gst-plugins-bad-{VERSION}.tar.xz'
SOURCE_SHA = '4213f43ddb875bb141e5040e97735579d74665bec3d17b51052aade395b83f00'
MESON = '1.9.1'
NINJA = '1.13.0'
PATCH = ROOT/'patches/gst-plugins-bad-1.28.3-vtdec-reorder-v210.patch'
DEFAULT_WORK = REPO/'.deps/applemedia-upstream-patched'
SDK = REPO/f'.deps/gstreamer-upstream-{VERSION}/sdk'
# Only applemedia; every other feature off so nothing else is configured or linked.
MESON_ARGS = ['--buildtype=release', '-Dauto_features=disabled', '-Dapplemedia=enabled',
              '-Dgl=enabled', '-Dgpl=disabled', '-Dexamples=disabled', '-Dtests=disabled',
              '-Dintrospection=disabled', '-Dnls=disabled', '-Dorc=disabled', '-Ddoc=disabled']

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def provenance():
    return dict(version=VERSION, source_url=SOURCE_URL, source_sha256=SOURCE_SHA,
                patch=PATCH.name, patch_sha256=sha(PATCH), meson=MESON, ninja=NINJA,
                meson_args=MESON_ARGS, target='arm64-apple-darwin', minimum_macos='14.0',
                sdk_distribution=json.loads((ROOT/'runtime-lock.json').read_text())['distribution'])

def sdk_environment(base):
    env = {k: v for k, v in base.items() if not k.startswith(('PKG_CONFIG_', 'DYLD_', 'GST_'))}
    env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=str(SDK/'lib/pkgconfig'),
               MACOSX_DEPLOYMENT_TARGET='14.0',
               LDFLAGS='-Wl,-headerpad_max_install_names')
    return env

def tools(work):
    venv = work/'venv'
    python = venv/'bin/python3'
    if not python.exists():
        subprocess.run([sys.executable, '-m', 'venv', str(venv)], check=True)
    subprocess.run([str(python), '-m', 'pip', 'install', '--quiet', '--disable-pip-version-check',
                    f'meson=={MESON}', f'ninja=={NINJA}'], check=True)
    return venv/'bin'

def build(work=DEFAULT_WORK):
    work = work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    archive = work/f'gst-plugins-bad-{VERSION}.tar.xz'
    if not archive.exists():
        urllib.request.urlretrieve(SOURCE_URL, archive)
    if sha(archive) != SOURCE_SHA:
        raise RuntimeError('gst-plugins-bad source hash mismatch')
    expected = json.loads((ROOT/'runtime-lock.json').read_text())['distribution']
    if json.loads((SDK/'upstream-provenance.json').read_text()) != expected:
        raise RuntimeError('Official SDK provenance mismatch; run fetch-gstreamer.py')
    bin_dir = tools(work)
    # Always unpack pristine source; never trust an edited cached source tree.
    extract = work/'src'
    if extract.exists():
        shutil.rmtree(extract)
    extract.mkdir()
    with tarfile.open(archive) as tf:
        tf.extractall(extract, filter='data')
    source = extract/f'gst-plugins-bad-{VERSION}'
    subprocess.run(['patch', '--batch', '-p1', '-i', str(PATCH)], cwd=source, check=True)
    env = sdk_environment(os.environ)
    env['PATH'] = str(bin_dir)+':'+env.get('PATH', '')
    build_dir = work/'build'
    if build_dir.exists():
        shutil.rmtree(build_dir)
    subprocess.run([str(bin_dir/'meson'), 'setup', str(build_dir), str(source)] + MESON_ARGS,
                   env=env, check=True)
    subprocess.run([str(bin_dir/'ninja'), '-C', str(build_dir), 'sys/applemedia/libgstapplemedia.dylib'],
                   env=env, check=True)
    artifact = build_dir/'sys/applemedia/libgstapplemedia.dylib'
    output = work/'output'
    if output.exists():
        shutil.rmtree(output)
    output.mkdir()
    binary = output/artifact.name
    shutil.copy2(artifact, binary)
    subprocess.run(['lipo', str(binary), '-verify_arch', 'arm64'], check=True)
    subprocess.run(['install_name_tool', '-id', '@loader_path/'+binary.name, str(binary)], check=True)
    for line in subprocess.check_output(['otool', '-l', str(binary)], text=True).split('cmd LC_RPATH')[1:]:
        path = line.split('path ', 1)[1].split(' (offset', 1)[0]
        subprocess.run(['install_name_tool', '-delete_rpath', path, str(binary)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(binary)], check=True)
    metadata = provenance()
    metadata.update(binary_sha256=sha(binary))
    (output/'provenance.json').write_text(json.dumps(metadata, indent=2)+'\n')
    # Corresponding source for the LGPL module: exact upstream archive + patch.
    for p in [archive, PATCH, source/'COPYING']:
        shutil.copy2(p, output/p.name)
    # The meson tree holds dangling symlinks to libraries never built; the
    # build helper's recursive xattr over .deps fails on them.
    shutil.rmtree(build_dir)
    shutil.rmtree(extract)
    print(binary)
    return binary

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, default=DEFAULT_WORK)
    build(parser.parse_args().work)
