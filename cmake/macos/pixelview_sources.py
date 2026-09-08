#!/usr/bin/env python3
"""Offline, explicit-input corresponding-source packaging; never legal clearance.

Only tracked Git blobs and explicitly hash-pinned source-cache files are read.
No checkout copying, recursive cache harvesting, downloading or release actions.
"""
import argparse
import gzip
import hashlib
import io
import json
import pathlib
import posixpath
import re
import subprocess
import tarfile
import tempfile
import urllib.parse


def validate_inventory(inventory):
    review = inventory.get('review', {})
    if inventory.get('schema_version') != 1 or review.get('status') != 'approved' or review.get('blockers') != []:
        raise ValueError('source inventory review is incomplete')
    if not review.get('evidence') or not inventory.get('runtime_bindings') or not inventory.get('components'):
        raise ValueError('source inventory review evidence, runtime bindings and components are required')
    ids = set()
    for component in inventory['components']:
        identifier = component['id']
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', identifier) or identifier in ids:
            raise ValueError('invalid or duplicate component id')
        ids.add(identifier)
        for key in ('version', 'license', 'sources', 'notices', 'recipes'):
            if not component.get(key):
                raise ValueError(f'{identifier}: missing {key}')
        if not isinstance(component.get('patches'), list):
            raise ValueError(f'{identifier}: explicit patches list required')


def safe_path(name):
    path = pathlib.PurePosixPath(name)
    if not name or path.is_absolute() or '..' in path.parts or '\\' in name or str(path) != name:
        raise ValueError(f'unsafe path: {name}')
    if any(p in ('.git', '.deps', '.runtime') for p in path.parts):
        raise ValueError(f'private/cache path forbidden: {name}')
    return path


def checked(data, record):
    if type(record.get('size')) is not int or record['size'] <= 0 or len(data) != record['size']:
        raise ValueError('source material size mismatch')
    if not re.fullmatch('[0-9a-f]{64}', record.get('sha256', '')) or hashlib.sha256(data).hexdigest() != record['sha256']:
        raise ValueError('source material checksum mismatch')
    return data


def artifact_names(release_id):
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+-[1-9][0-9]*', release_id):
        raise ValueError('invalid release id')
    prefix = f'Pixelview-Desktop-{release_id}'
    return {'sources': prefix + '-sources.tar.gz', 'notices': prefix + '-NOTICES.txt', 'inventory': prefix + '-source-inventory.json'}


def build(root, tag, inventory_path, cache, output, release_id, base_url):
    root, cache, output = pathlib.Path(root), pathlib.Path(cache), pathlib.Path(output)
    def git(*args):
        return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE)
    if git('status', '--porcelain', '--untracked-files=all').strip():
        raise ValueError('source checkout must be clean')
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+', tag) or not release_id.startswith(tag[1:] + '-'):
        raise ValueError('source tag must match release id')
    commit = git('rev-parse', 'HEAD').decode().strip()
    if git('rev-parse', f'refs/tags/{tag}^{{commit}}').decode().strip() != commit:
        raise ValueError('exact source tag must point to HEAD')
    safe_path(inventory_path)
    inventory_bytes = git('show', f'{commit}:{inventory_path}')
    inventory = json.loads(inventory_bytes)
    validate_inventory(inventory)
    tree = {}
    for entry in git('ls-tree', '-rz', commit).split(b'\0'):
        if not entry:
            continue
        metadata, name = entry.split(b'\t', 1)
        mode, kind, oid = metadata.decode().split()
        name = name.decode()
        safe_path(name)
        if kind == 'commit':
            excluded = [entry for entry in inventory.get('excluded_submodules', [])
                        if entry.get('path') == name and entry.get('commit') == oid and entry.get('reason')]
            if len(excluded) != 1:
                raise ValueError(f'submodule requires source or reviewed build exclusion: {name}')
            continue
        if kind != 'blob' or mode not in ('100644', '100755', '120000'):
            raise ValueError(f'unsupported Git entry: {name}')
        if mode == '120000':
            target = git('cat-file', 'blob', oid).decode()
            if target.startswith('/') or '\\' in target:
                raise ValueError('unsafe project symlink')
            safe_path(posixpath.normpath(posixpath.join(posixpath.dirname(name), target)))
        tree[name] = (oid, mode)
    def material(record):
        name = record['path']
        safe_path(name)
        if name not in tree or tree[name][1] == '120000':
            raise ValueError(f'material is not a tracked regular file: {name}')
        return checked(git('cat-file', 'blob', tree[name][0]), record)
    material(inventory['review']['evidence'])
    for binding in inventory['runtime_bindings']:
        material(binding)
    notices = ['Pixelview Desktop — third-party notices\nNot a legal clearance certificate.\n']
    dependencies = {}
    for component in inventory['components']:
        notices.append(f"\n=== {component['id']} {component['version']} ({component['license']}) ===\n")
        for record in component['notices']:
            notices.append(material(record).decode('utf-8'))
        for record in component['recipes'] + component['patches']:
            material(record)
        for record in component['sources']:
            name = record['name']
            if len(safe_path(name).parts) != 1:
                raise ValueError('source archive name must be a basename')
            url = urllib.parse.urlsplit(record['url'])
            if url.scheme != 'https' or not url.hostname or url.username or url.password or url.query or url.fragment:
                raise ValueError('public pinned source URL must be credential-free HTTPS')
            digest = record['sha256']
            if not re.fullmatch('[0-9a-f]{64}', digest):
                raise ValueError('invalid source checksum')
            path = cache / digest
            if path.is_symlink() or not path.is_file():
                raise ValueError('source cache input must be a regular file')
            data = checked(path.read_bytes(), record)
            # Inspect, never extract untrusted dependency archives. Symlinks,
            # devices and oversized expansions require review/repackaging first.
            try:
                with tarfile.open(fileobj=io.BytesIO(data), mode='r:*') as source_archive:
                    total = 0
                    seen = set()
                    for index, member in enumerate(source_archive):
                        safe_path(member.name.rstrip('/'))
                        total += member.size
                        if (not (member.isfile() or member.isdir()) or member.name in seen
                                or index >= 200000 or total > 8 * 1024**3):
                            raise ValueError('unsafe source archive entry or expansion limit')
                        seen.add(member.name)
            except tarfile.TarError as error:
                raise ValueError('source must be an inspectable tar archive') from error
            key = f"dependencies/{component['id']}/{name}"
            if key in dependencies:
                raise ValueError('duplicate source archive name')
            dependencies[key] = data
    names = artifact_names(release_id)
    resolved = {'schema_version': 1, 'source_commit': commit, 'source_tag': tag, 'release_id': release_id,
                'input_inventory_sha256': hashlib.sha256(inventory_bytes).hexdigest(), 'inventory': inventory}
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pixelview-sources-') as temp:
        stage = pathlib.Path(temp)
        (stage / names['inventory']).write_text(json.dumps(resolved, indent=2, sort_keys=True) + '\n')
        (stage / names['notices']).write_text('\n'.join(notices))
        with (stage / names['sources']).open('wb') as raw:
            with gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0) as gz:
                with tarfile.open(fileobj=gz, mode='w') as archive:
                    def add(name, data, mode=0o644):
                        entry = tarfile.TarInfo(name)
                        entry.size, entry.mode, entry.mtime = len(data), mode, 0
                        archive.addfile(entry, io.BytesIO(data))
                    for name, (oid, mode) in sorted(tree.items()):
                        data = git('cat-file', 'blob', oid)
                        if mode == '120000':
                            entry = tarfile.TarInfo('project/' + name)
                            entry.type, entry.linkname, entry.mode = tarfile.SYMTYPE, data.decode(), 0o777
                            archive.addfile(entry)
                        else:
                            add('project/' + name, data, int(mode, 8) & 0o777)
                    for name, data in sorted(dependencies.items()):
                        add(name, data)
                    add('source-inventory.json', (stage / names['inventory']).read_bytes())
                    add('NOTICES.txt', (stage / names['notices']).read_bytes())
        result = {}
        for role, name in names.items():
            data = (stage / name).read_bytes()
            result[role] = {'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                            'url': f'{base_url}/releases/{release_id}/{name}'}
            (output / name).write_bytes(data)
    license_dir = output / 'license'
    license_dir.mkdir(exist_ok=True)
    (license_dir / 'third-party-notices.txt').write_bytes((output / names['notices']).read_bytes())
    offline = {'schema_version': 1, 'version': tag[1:], 'source_commit': commit, 'source_tag': tag,
               'status': 'release-candidate-unpublished', 'source_url': '',
               'candidate_source_url': result['sources']['url'], 'publication_verified': False, 'artifacts': result}
    (license_dir / 'source-manifest.json').write_text(json.dumps(offline, indent=2, sort_keys=True) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--inventory', default='release/source-inventory.json')
    parser.add_argument('--cache', required=True, help='dedicated hash-addressed public source cache; never a runtime tree')
    parser.add_argument('--output', required=True)
    parser.add_argument('--release-id', required=True)
    parser.add_argument('--base-url', required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(build(args.root, args.tag, args.inventory, args.cache, args.output, args.release_id, args.base_url), sort_keys=True))
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(2, f'error: corresponding-source gate: {error}\n')
