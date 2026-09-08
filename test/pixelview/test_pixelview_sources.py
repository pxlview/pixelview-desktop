"""Fail-closed corresponding-source packaging contracts (no release operations)."""
import importlib.util
import pathlib
import sys
import hashlib
import json
import subprocess
import tarfile
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'cmake/macos'))


class SourceInventoryTests(unittest.TestCase):
    def fixture(self, temp):
        root = pathlib.Path(temp) / 'project'
        root.mkdir()
        cache = pathlib.Path(temp) / 'cache'
        cache.mkdir()
        def git(*args):
            return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.DEVNULL).decode().strip()
        git('init')
        git('config', 'user.email', 'test@example.invalid')
        git('config', 'user.name', 'Packaging test')
        (root / 'COPYING').write_bytes((ROOT / 'COPYING').read_bytes())
        (root / 'recipe.sh').write_text('cc source.c -o example\n')
        (root / 'review.txt').write_text('TEST ONLY: fixture review, not product clearance\n')
        (root / 'runtime-lock.json').write_text('{"fixture":true}\n')
        archive = pathlib.Path(temp) / 'dependency.tar'
        with tarfile.open(archive, 'w') as handle:
            handle.add(root / 'COPYING', arcname='dependency/COPYING')
        data = archive.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        (cache / digest).write_bytes(data)
        def local(path):
            data = (root / path).read_bytes()
            return {'path': path, 'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}
        inventory = {'schema_version': 1, 'review': {'status': 'approved', 'blockers': [], 'evidence': local('review.txt')},
                     'runtime_bindings': [local('runtime-lock.json')],
                     'components': [{'id': 'example', 'version': '1.0', 'license': 'GPL-2.0-or-later',
                                     'sources': [{'name': 'dependency.tar', 'url': 'https://example.org/releases/1.0/dependency.tar', 'sha256': digest, 'size': len(data)}],
                                     'notices': [local('COPYING')], 'recipes': [local('recipe.sh')], 'patches': []}]}
        (root / 'inventory.json').write_text(json.dumps(inventory))
        git('add', '.')
        git('commit', '-qm', 'fixture only')
        git('tag', 'v1.0.0')
        return root, cache, inventory, git

    def test_builds_explicit_sources_and_tagged_project(self):
        import pixelview_sources as sources
        self.assertTrue(hasattr(sources, 'build'), 'source archive builder must exist')
        with tempfile.TemporaryDirectory() as temp:
            root, cache, inventory, git = self.fixture(temp)
            out = pathlib.Path(temp) / 'out'
            result = sources.build(root, 'v1.0.0', 'inventory.json', cache, out, '1.0.0-1', 'https://example.org/downloads')
            self.assertEqual(set(result), {'sources', 'notices', 'inventory'})
            with tarfile.open(out / result['sources']['name']) as archive:
                self.assertIn('project/COPYING', archive.getnames())
                self.assertIn('dependencies/example/dependency.tar', archive.getnames())
                self.assertNotIn('.git/config', archive.getnames())
            self.assertIn('GNU GENERAL PUBLIC LICENSE', (out / result['notices']['name']).read_text())
            self.assertEqual(json.loads((out / result['inventory']['name']).read_text())['source_commit'], git('rev-parse', 'HEAD'))
            self.assertEqual((out / 'license/third-party-notices.txt').read_bytes(), (out / result['notices']['name']).read_bytes())
            offline = json.loads((out / 'license/source-manifest.json').read_text())
            self.assertEqual(offline['candidate_source_url'], result['sources']['url'])
            self.assertEqual(offline['source_commit'], git('rev-parse', 'HEAD'))
            self.assertFalse(offline['publication_verified'])
            self.assertEqual(offline['status'], 'release-candidate-unpublished')

    def test_rejects_unsafe_source_archive_even_with_correct_hash(self):
        import io
        import pixelview_sources as sources
        with tempfile.TemporaryDirectory() as temp:
            root, cache, inventory, git = self.fixture(temp)
            raw = io.BytesIO()
            with tarfile.open(fileobj=raw, mode='w') as archive:
                member = tarfile.TarInfo('../private.txt')
                member.size = 1
                archive.addfile(member, io.BytesIO(b'x'))
            data = raw.getvalue()
            record = inventory['components'][0]['sources'][0]
            record.update(sha256=hashlib.sha256(data).hexdigest(), size=len(data))
            (cache / record['sha256']).write_bytes(data)
            (root / 'inventory.json').write_text(json.dumps(inventory))
            git('add', '.')
            git('commit', '-qm', 'unsafe fixture')
            git('tag', '-f', 'v1.0.0')
            with self.assertRaisesRegex(ValueError, 'unsafe'):
                sources.build(root, 'v1.0.0', 'inventory.json', cache, pathlib.Path(temp) / 'out', '1.0.0-1', 'https://example.org')

    def test_preserves_safe_project_symlinks_and_explicit_unused_submodules(self):
        import pixelview_sources as sources
        with tempfile.TemporaryDirectory() as temp:
            root, cache, inventory, git = self.fixture(temp)
            (root / 'license-link').symlink_to('COPYING')
            oid = git('rev-parse', 'HEAD')
            (root / 'disabled-module').mkdir()
            git('update-index', '--add', '--cacheinfo', f'160000,{oid},disabled-module')
            inventory['excluded_submodules'] = [{'path': 'disabled-module', 'commit': oid, 'reason': 'test-only disabled module'}]
            (root / 'inventory.json').write_text(json.dumps(inventory))
            git('add', '.')
            git('commit', '-qm', 'symlink fixture')
            git('tag', '-f', 'v1.0.0')
            out = pathlib.Path(temp) / 'out'
            result = sources.build(root, 'v1.0.0', 'inventory.json', cache, out, '1.0.0-1', 'https://example.org')
            with tarfile.open(out / result['sources']['name']) as archive:
                self.assertEqual(archive.getmember('project/license-link').linkname, 'COPYING')

    def test_rejects_missing_tampered_dirty_and_untagged_inputs(self):
        import pixelview_sources as sources
        for case in ('missing', 'tampered', 'symlink-cache', 'dirty', 'wrong-tag', 'missing-review', 'missing-notice', 'missing-recipe', 'missing-binding', 'unsafe-notice', 'unsafe-project-link'):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temp:
                root, cache, inventory, git = self.fixture(temp)
                record = inventory['components'][0]['sources'][0]
                blob = cache / record['sha256']
                if case == 'missing':
                    blob.unlink()
                elif case == 'tampered':
                    blob.write_bytes(b'x' * record['size'])
                elif case == 'symlink-cache':
                    target = pathlib.Path(temp) / 'elsewhere'
                    blob.rename(target)
                    blob.symlink_to(target)
                elif case == 'dirty':
                    (root / 'untracked-secret').write_text('must not be copied')
                elif case == 'wrong-tag':
                    git('tag', '-d', 'v1.0.0')
                else:
                    if case == 'missing-review':
                        inventory['review'].pop('evidence')
                    elif case == 'missing-notice':
                        inventory['components'][0]['notices'] = []
                    elif case == 'missing-recipe':
                        inventory['components'][0]['recipes'] = []
                    elif case == 'missing-binding':
                        inventory['runtime_bindings'] = []
                    elif case == 'unsafe-notice':
                        inventory['components'][0]['notices'][0]['path'] = '../COPYING'
                    elif case == 'unsafe-project-link':
                        (root / 'escape').symlink_to('../private')
                    (root / 'inventory.json').write_text(json.dumps(inventory))
                    git('add', '.')
                    git('commit', '-qm', 'invalid fixture')
                    git('tag', '-f', 'v1.0.0')
                out = pathlib.Path(temp) / 'out'
                with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                    sources.build(root, 'v1.0.0', 'inventory.json', cache, out, '1.0.0-1', 'https://example.org')
                self.assertFalse(out.exists(), 'failure must not emit partial release assets')

    def test_available_real_rswebrtc_source_and_project_materials(self):
        """Actual pinned dependency inputs; isolated test Git tree, not release clearance."""
        import pixelview_sources as sources
        production = json.loads((ROOT / 'release/source-inventory.json').read_text())
        component = production['components'][0]
        archive_record = component['sources'][0]
        public_cache = pathlib.Path.home() / 'Library/Caches/pixelview-sources'
        if not (public_cache / archive_record['sha256']).is_file():
            self.skipTest('optional real pinned source cache not downloaded')
        with tempfile.TemporaryDirectory() as temp:
            root, cache, inventory, git = self.fixture(temp)
            for record in component['notices'] + component['recipes'] + component['patches'] + production['runtime_bindings']:
                destination = root / record['path']
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes((ROOT / record['path']).read_bytes())
            inventory['components'] = [component]
            inventory['runtime_bindings'] = production['runtime_bindings']
            (root / 'inventory.json').write_text(json.dumps(inventory))
            git('add', '.')
            git('commit', '-qm', 'real public inputs in test-only project')
            git('tag', '-f', 'v1.0.0')
            out = pathlib.Path(temp) / 'out'
            command = [sys.executable, str(ROOT / 'cmake/macos/pixelview_sources.py'),
                       '--root', str(root), '--tag', 'v1.0.0', '--inventory', 'inventory.json',
                       '--cache', str(public_cache), '--output', str(out), '--release-id', '1.0.0-1',
                       '--base-url', 'https://example.org/test-only']
            result = json.loads(subprocess.check_output(command))
            with tarfile.open(out / result['sources']['name']) as archive:
                data = archive.extractfile('dependencies/rswebrtc/gst-plugin-webrtc-0.15.2.crate').read()
                self.assertEqual(hashlib.sha256(data).hexdigest(), archive_record['sha256'])
                self.assertIn('project/plugins/pixelview-whep/patches/rswebrtc-0.15.2-same-origin.patch', archive.getnames())
            again = sources.build(root, 'v1.0.0', 'inventory.json', public_cache, pathlib.Path(temp) / 'again', '1.0.0-1', 'https://example.org/test-only')
            self.assertEqual(result, again, 'identical real inputs must produce identical artifact bytes')

    def test_unreviewed_inventory_is_rejected(self):
        spec = importlib.util.find_spec('pixelview_sources')
        self.assertIsNotNone(spec, 'corresponding-source gate must exist')
        import pixelview_sources as sources
        with self.assertRaisesRegex(ValueError, 'review'):
            sources.validate_inventory({'schema_version': 1, 'review': {'status': 'blocked'}})


if __name__ == '__main__':
    unittest.main()
