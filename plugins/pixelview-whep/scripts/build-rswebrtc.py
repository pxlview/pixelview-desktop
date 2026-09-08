#!/usr/bin/env python3
"""Build ONLY pinned rswebrtc with Pixelview's WHEP origin patch; never stage.
Requires rustup toolchain 1.94.0 and locally extracted official GStreamer SDK.
The default output is separate from the mutable embedded runtime.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
SOURCE_URL = 'https://static.crates.io/crates/gst-plugin-webrtc/gst-plugin-webrtc-0.15.2.crate'
SOURCE_SHA = '58b0f7af06bd2e98c71e8ad76a27a4045727b88af769a89d6a7f0fd5903047b0'
TOOLCHAIN = '1.94.0'
PATCH = ROOT/'patches/rswebrtc-0.15.2-same-origin.patch'
DEFAULT_WORK = REPO/'.deps/rswebrtc-upstream-patched'
SDK = REPO/'.deps/gstreamer-upstream-1.28.3/sdk'

def sdk_environment(base):
    env = {k:v for k,v in base.items() if not k.startswith(('PKG_CONFIG_', 'DYLD_', 'GST_'))}
    env.update(PKG_CONFIG_PATH='', PKG_CONFIG_LIBDIR=str(SDK/'lib/pkgconfig'),
               DYLD_LIBRARY_PATH=str(SDK/'lib'), GST_PLUGIN_SYSTEM_PATH_1_0='',
               GST_PLUGIN_PATH_1_0='')
    return env

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def provenance():
    return dict(version='0.15.2', source_url=SOURCE_URL, source_sha256=SOURCE_SHA,
                patch=PATCH.name, patch_sha256=sha(PATCH), rust_toolchain=TOOLCHAIN,
                target='aarch64-apple-darwin', minimum_macos='14.0',
                cargo_args=['--locked', '--release', '--lib'], features='upstream-default',
                sdk_distribution=json.loads((ROOT/'runtime-lock.json').read_text())['distribution'])

def build(work=DEFAULT_WORK, test=False, unpatched=False):
    work = work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    archive = work/'gst-plugin-webrtc-0.15.2.crate'
    if not archive.exists():
        urllib.request.urlretrieve(SOURCE_URL, archive)
    if sha(archive) != SOURCE_SHA:
        raise RuntimeError('rswebrtc source hash mismatch')
    cargo = shutil.which('cargo') or str(Path.home()/'.cargo/bin/cargo')
    subprocess.run([cargo, '+'+TOOLCHAIN, '--version'], check=True)
    source = work/'gst-plugin-webrtc-0.15.2'
    # Always unpack pristine source; never trust an edited cached source tree.
    if source.exists():
        shutil.rmtree(source)
    with tarfile.open(archive) as tf:
        tf.extractall(work, filter='data')
    if not unpatched:
        subprocess.run(['patch', '--batch', '-p1', '-i', str(PATCH)], cwd=source, check=True)
    if test:
        client = source/'src/whep_signaller/client.rs'
        with client.open('a') as f:
            f.write('\n'+(ROOT/'tests/whep_origin.rs').read_text())
    expected = json.loads((ROOT/'runtime-lock.json').read_text())['distribution']
    if json.loads((SDK/'upstream-provenance.json').read_text()) != expected:
        raise RuntimeError('Official SDK provenance mismatch; run fetch-gstreamer.py')
    env = sdk_environment(os.environ)
    for key in ('GIT_DIR', 'GIT_WORK_TREE'):
        env.pop(key, None)
    # Do not accidentally stamp the enclosing Pixelview commit/date into a
    # registry crate. Upstream helper then uses its declared release metadata.
    env['GIT_CEILING_DIRECTORIES'] = str(work.parent)
    env.update(MACOSX_DEPLOYMENT_TARGET='14.0', CARGO_TARGET_DIR=str(work/'target'),
               PATH=str(Path(cargo).parent)+':'+env.get('PATH', ''),
               RUSTFLAGS='-C link-arg=-Wl,-headerpad_max_install_names')
    command = [cargo, '+'+TOOLCHAIN, 'test' if test else 'build', '--locked', '--release', '--lib',
               '--target', 'aarch64-apple-darwin', '--manifest-path', str(source/'Cargo.toml')]
    if test:
        command += ['--', '--test-threads=1']
    subprocess.run(command, env=env, check=True)
    if test:
        return None
    if unpatched:
        raise RuntimeError('Unpatched mode is for RED tests only')
    artifact = work/'target/aarch64-apple-darwin/release/libgstrswebrtc.dylib'
    output = work/'output'
    output.mkdir(exist_ok=True)
    binary = output/artifact.name
    shutil.copy2(artifact, binary)
    subprocess.run(['install_name_tool', '-id', '@loader_path/'+binary.name, str(binary)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(binary)], check=True)
    metadata = provenance()
    metadata.update(binary_sha256=sha(binary), cargo_lock_sha256=sha(source/'Cargo.lock'))
    (output/'provenance.json').write_text(json.dumps(metadata, indent=2)+'\n')
    # Preserve exact corresponding upstream source, patch and locked dependency
    # list alongside the binary for the parent packager and source distribution.
    for p in [archive, PATCH, source/'Cargo.lock', source/'Cargo.toml', source/'LICENSE-MPL-2.0']:
        shutil.copy2(p, output/p.name)
    print(binary)
    return binary

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, default=DEFAULT_WORK)
    parser.add_argument('--test', action='store_true')
    parser.add_argument('--unpatched', action='store_true', help='reproduce RED; only with --test')
    args = parser.parse_args()
    if args.unpatched and not args.test:
        parser.error('--unpatched requires --test')
    build(args.work, args.test, args.unpatched)
