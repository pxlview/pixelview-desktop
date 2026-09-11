#!/usr/bin/env python3
"""Extract pinned official packages locally; never execute installer scripts."""
import hashlib
import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
DEFAULT_WORK = REPO/'.deps/gstreamer-upstream-1.28.3'
SDK = DEFAULT_WORK/'sdk'

def verify_archive(path, expected):
    with path.open('rb') as handle:
        actual = hashlib.file_digest(handle, 'sha256').hexdigest()
    if actual != expected:
        raise RuntimeError(f'Package hash mismatch: {path.name}')

def merge_payload(payload, sdk):
    """Merge a component prefix without following links outside its payload."""
    payload, sdk = payload.resolve(), sdk.resolve()
    for src in sorted(payload.rglob('*')):
        dst = sdk/src.relative_to(payload)
        if src.is_symlink():
            target = os.readlink(src)
            if Path(target).is_absolute() or not (src.parent/target).resolve().is_relative_to(payload):
                raise RuntimeError(f'Unsafe payload symlink: {src}')
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.is_symlink() or dst.exists():
                dst.unlink()
            dst.symlink_to(target)
        elif src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
        elif src.is_file():
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.is_symlink():
                raise RuntimeError(f'File collides with payload symlink: {dst}')
            # Both trees are disposable siblings on the same filesystem. Link
            # verified extracted bytes instead of allocating a second SDK-sized
            # copy. Unlink collisions so later components cannot mutate earlier
            # payload inodes; TemporaryDirectory cleanup leaves SDK links intact.
            dst.unlink(missing_ok=True)
            os.link(src, dst)
            if dst.suffix == '.pc':
                text = dst.read_text()
                text = re.sub(r'^prefix=.*$', 'prefix='+str(sdk), text, flags=re.M)
                text = text.replace('/Library/Frameworks/GStreamer.framework/Versions/1.0', str(sdk))
                dst.write_text(text)

def publish_sdk(source, final):
    previous = source.parent/'previous-sdk'
    if final.exists():
        final.rename(previous)
    try:
        source.rename(final)
    except OSError:
        if previous.exists():
            previous.rename(final)
        raise

def acquire(work=DEFAULT_WORK):
    work = work.resolve()
    lock = json.loads((ROOT/'runtime-lock.json').read_text())['distribution']
    downloads = work/'downloads'; downloads.mkdir(parents=True, exist_ok=True)
    # Always verify archives and reconstruct the SDK; cached edits are not inputs.
    # Finder can create .DS_Store while pkgutil's directory is being removed.
    # Cleanup leftovers must not turn a successfully verified SDK into a failure.
    with tempfile.TemporaryDirectory(prefix='extract-', dir=work, ignore_cleanup_errors=True) as tmp:
        sdk = Path(tmp)/'sdk'
        for kind, pin in lock['packages'].items():
            archive = downloads/Path(pin['url']).name
            if not archive.exists():
                partial = archive.with_suffix('.pkg.partial')
                subprocess.run(['curl', '--fail', '--location', '--proto', '=https', '--proto-redir', '=https',
                                '--retry', '3', '--output', str(partial), pin['url']], check=True)
                verify_archive(partial, pin['sha256']); partial.rename(archive)
            verify_archive(archive, pin['sha256'])
            expanded = Path(tmp)/kind
            subprocess.run(['pkgutil', '--expand-full', str(archive), str(expanded)], check=True)
            for component in sorted(expanded.glob('*.pkg')):
                # Framework skeleton/scripts have no headers or curated runtime.
                if component.name.startswith('osx-framework-'):
                    continue
                payload = component/'Payload'
                if payload.is_dir():
                    merge_payload(payload, sdk)
            notice = expanded/'Resources/license.txt'
            if notice.exists():
                dst = sdk/'share/upstream-packages'/kind
                dst.mkdir(parents=True, exist_ok=True)
                shutil.copy2(notice, dst/'license.txt')
        # pkg-config paths must name the final prefix, not this temporary tree.
        final = work/'sdk'
        for pc in (sdk/'lib/pkgconfig').glob('*.pc'):
            pc.write_text(pc.read_text().replace(str(sdk), str(final)))
        (sdk/'upstream-provenance.json').write_text(json.dumps(lock, indent=2)+'\n')
        publish_sdk(sdk, final)
    print(final)
    return final

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, default=DEFAULT_WORK)
    acquire(parser.parse_args().work)

