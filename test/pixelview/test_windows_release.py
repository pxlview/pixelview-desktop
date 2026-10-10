"""Offline checks of the Windows build/release tooling (no Windows, network or credentials)."""
import base64, hashlib, importlib.util, os, json, pathlib, re, shutil, subprocess, sys, tempfile, unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release_module = load('pixelview_release_windows', 'cmake/windows/pixelview-release.py')
SPARKLE = release_module.SPARKLE_NS


class Config(unittest.TestCase):
    def test_pins_and_feed(self):
        config = json.loads((ROOT / 'release/windows.json').read_text())
        self.assertEqual(config['appcast_url'], 'https://downloads.pixelview.io/desktop/windows/appcast-x64.xml')
        self.assertEqual(config['r2_prefix'], 'desktop/windows')
        self.assertRegex(config['winsparkle_sha256'], r'^[0-9a-f]{64}$')
        self.assertIn(f"/v{config['winsparkle_version']}/", config['winsparkle_url'])
        cmake = (ROOT / 'frontend/cmake/feature-winsparkle.cmake').read_text()
        self.assertIn(config['appcast_url'], cmake)
        self.assertIn('obsproject', cmake)  # The guard against OBS update infrastructure.

    def test_no_obs_updater_on_windows(self):
        cmake = (ROOT / 'frontend/cmake/os-windows.cmake').read_text()
        for source in ('AutoUpdateThread', 'OBSUpdate', 'WhatsNew', 'add_subdirectory(updater)', 'update-helpers'):
            self.assertNotIn(source, cmake)
        for path in ('frontend/widgets/OBSBasic_Updater.cpp', 'frontend/widgets/OBSBasic_MainControls.cpp'):
            text = (ROOT / path).read_text()
            self.assertNotRegex(text, r'#ifdef _WIN32\n#include <utility/AutoUpdateThread.hpp>')
            self.assertNotRegex(text, r'#(el)?if _WIN32\n')

    def test_build_flags_match_macos(self):
        out = subprocess.run([sys.executable, ROOT / 'cmake/windows/pixelview-build.py', '--print'],
                             capture_output=True, text=True, check=True).stdout
        mac = (ROOT / 'cmake/macos/pixelview-build.sh').read_text()
        for flag in re.findall(r'-DENABLE_[A-Z_]+=(?:ON|OFF)', mac):
            if 'SYPHON' in flag: continue
            self.assertIn(flag, out)
        version = json.loads((ROOT / 'version.json').read_text())['obs_base_version']
        self.assertIn(f'-DOBS_VERSION_OVERRIDE={version}', out)
        self.assertIn('-DPIXELVIEW_RELEASE_BUILD=OFF', out)
        self.assertIn('-DSPARKLE_APPCAST_URL= ', out)  # Development builds never update.

    def test_staging_builds_only_read_a_loopback_feed(self):
        cmake = (ROOT / 'frontend/cmake/feature-winsparkle.cmake').read_text()
        self.assertIn('PIXELVIEW_UPDATE_STAGING cannot be combined with PIXELVIEW_RELEASE_BUILD', cmake)
        self.assertIn(r'^http://127\\.0\\.0\\.1:[0-9]+/appcast-x64\\.xml$', cmake)
        env = dict(os.environ, PIXELVIEW_UPDATE_STAGING_FEED='http://127.0.0.1:8731/appcast-x64.xml',
                   PIXELVIEW_STAGING_BUILD_NUMBER='9001')
        env.pop('PIXELVIEW_RELEASE_BUILD', None)
        out = subprocess.run([sys.executable, ROOT / 'cmake/windows/pixelview-build.py', '--print'],
                             capture_output=True, text=True, check=True, env=env).stdout
        for flag in ('-DPIXELVIEW_RELEASE_BUILD=OFF', '-DPIXELVIEW_UPDATE_STAGING=ON', '-DPIXELVIEW_STAGING_BUILD_NUMBER=9001',
                     '-DSPARKLE_APPCAST_URL=http://127.0.0.1:8731/appcast-x64.xml', 'build_x64_staging_9001'):
            self.assertIn(flag, out)
        release = release_module.Release()
        release.use_staging(9001, 8731)
        self.assertEqual(release.root, ROOT / 'dist/windows-staging')
        self.assertNotIn('downloads.pixelview.io', release.staging_feed)

    def test_validate_config(self):
        result = subprocess.run([sys.executable, ROOT / 'cmake/windows/pixelview-release.py', '--validate-config'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        release = release_module.Release()
        for broken in ({'winsparkle_public_key': ''}, {'signing': dict(release.config['signing'], method='')},
                       {'appcast_url': 'https://obsproject.com/update_studio/appcast.xml'}):
            release.config = dict(json.loads((ROOT / 'release/windows.json').read_text()), **broken)
            release.appcast_url = release.config['appcast_url']
            with self.assertRaises(release_module.ReleaseError):
                release.validate_config()

    def test_artifact_signing_metadata(self):
        config = json.loads((ROOT / 'release/windows.json').read_text())
        self.assertEqual(config['signing']['method'], 'trusted-signing')
        metadata = json.loads((ROOT / config['signing']['trusted_signing_metadata']).read_text())
        self.assertRegex(metadata['Endpoint'], r'^https://[a-z]+\.codesigning\.azure\.net/?$')
        self.assertTrue(metadata['CodeSigningAccountName'] and metadata['CertificateProfileName'])
        # Signing authenticates only through the operator's `az login` session.
        self.assertNotIn('AzureCliCredential', metadata['ExcludeCredentials'])
        self.assertIn('EnvironmentCredential', metadata['ExcludeCredentials'])

    def test_source_inventory_covers_every_submodule(self):
        sources = load('pixelview_sources', 'cmake/macos/pixelview_sources.py')
        config = json.loads((ROOT / 'release/windows.json').read_text())
        inventory = json.loads((ROOT / config['source_inventory']).read_text())
        sources.validate_inventory(inventory)
        gitlinks = {line.split('\t')[1]: line.split()[2] for line in subprocess.run(
            ['git', 'ls-tree', '-r', 'HEAD'], cwd=ROOT, capture_output=True, text=True, check=True).stdout.splitlines()
            if line.split()[1] == 'commit'}
        excluded = {entry['path']: entry['commit'] for entry in inventory['excluded_submodules']}
        self.assertEqual(excluded, gitlinks)
        # Every pinned repository file must match its committed blob, or --prepare stops at compliance.
        records = inventory['runtime_bindings'] + [record for component in inventory['components']
                                                   for key in ('notices', 'recipes', 'patches') for record in component[key]]
        for record in records:
            blob = subprocess.run(['git', 'show', f"HEAD:{record['path']}"], cwd=ROOT, capture_output=True, check=True).stdout
            self.assertEqual((hashlib.sha256(blob).hexdigest(), len(blob)), (record['sha256'], record['size']), record['path'])
        ids = {component['id'] for component in inventory['components']}
        self.assertTrue({'libdshowcapture', 'capture-device-support', 'winsparkle'} <= ids)


@unittest.skipUnless(shutil.which('openssl'), 'openssl is required')
class Appcast(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        key = self.dir / 'key.pem'
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'ed25519', '-out', key], check=True, capture_output=True)
        der = subprocess.run(['openssl', 'pkey', '-in', key, '-pubout', '-outform', 'DER'], check=True, capture_output=True).stdout
        self.release = release_module.Release()
        self.release.config = dict(self.release.config, winsparkle_public_key=base64.b64encode(der[-32:]).decode())
        self.release.appcast_path = self.dir / 'appcast-x64.xml'
        self.installer = self.dir / 'setup.exe'
        self.installer.write_bytes(b'MZ' + b'\0' * 4096)
        sig = self.dir / 'sig'
        subprocess.run(['openssl', 'pkeyutl', '-sign', '-inkey', key, '-rawin', '-in', self.installer, '-out', sig], check=True)
        self.signature = base64.b64encode(sig.read_bytes()).decode()

    def tearDown(self):
        self.tmp.cleanup()

    def test_signature_verification(self):
        self.release.verify_eddsa(self.installer, self.signature)
        self.installer.write_bytes(self.installer.read_bytes() + b'x')
        with self.assertRaises(release_module.ReleaseError):
            self.release.verify_eddsa(self.installer, self.signature)

    def feed(self, *builds):
        items = ''.join(f'<item><title>{b}</title><sparkle:version>{b}</sparkle:version><enclosure url="u{b}" length="1"/></item>' for b in builds)
        return f'<rss xmlns:sparkle="{SPARKLE}" version="2.0"><channel>{items}</channel></rss>'.encode()

    def test_first_release_and_item(self):
        self.release.write_appcast(None, self.signature, 4098)
        root = ET.fromstring(self.release.appcast_path.read_bytes())
        items = list(root.iter('item'))
        self.assertEqual(len(items), 1)
        enclosure = items[0].find('enclosure')
        self.assertEqual(items[0].findtext(f'{{{SPARKLE}}}version'), str(self.release.build))
        self.assertEqual(items[0].findtext(f'{{{SPARKLE}}}shortVersionString'), self.release.version)
        self.assertEqual(enclosure.get(f'{{{SPARKLE}}}os'), 'windows-x64')
        self.assertEqual(enclosure.get(f'{{{SPARKLE}}}edSignature'), self.signature)
        self.assertEqual(enclosure.get(f'{{{SPARKLE}}}installerArguments'), '/SILENT /SP- /NOCANCEL /NORESTART')
        self.assertTrue(enclosure.get('url').startswith('https://downloads.pixelview.io/desktop/windows/releases/'))

    def test_progression_and_history(self):
        older = self.feed(*range(self.release.build - 1, max(0, self.release.build - 13), -1))
        self.release.write_appcast(older, self.signature, 4098)
        builds = release_module.Release.feed_builds(self.release.appcast_path.read_bytes())
        self.assertEqual(builds[0], self.release.build)
        self.assertLessEqual(len(builds), release_module.MAX_FEED_ITEMS)
        for current in (self.feed(self.release.build), self.feed(self.release.build + 1), self.feed()):
            with self.assertRaises(release_module.ReleaseError):
                self.release.write_appcast(current, self.signature, 4098)


class Installer(unittest.TestCase):
    def test_installer_contract(self):
        iss = (ROOT / 'cmake/windows/pixelview-installer.iss').read_text()
        self.assertIn('PrivilegesRequired=lowest', iss)
        self.assertIn('AppId={{6F0B6C2E-8D4A-4E59-9B1F-6A1D2C7E9B30}', iss)
        self.assertIn(r'Subkey: "Software\Classes\pixelview\shell\open\command"', iss)
        self.assertIn('ArchitecturesAllowed=x64compatible', iss)
        self.assertNotIn('skipifsilent', iss)  # Silent updates relaunch the app.


class Launch(unittest.TestCase):
    """cmake/windows/pixelview-launch.py --check-only: unattended pairing stays in a separate root."""

    def run_launch(self, *args, **environment):
        env = {k: v for k, v in os.environ.items() if not k.startswith('PIXELVIEW_')}
        env.update(environment)
        return subprocess.run([sys.executable, ROOT / 'cmake/windows/pixelview-launch.py', '--check-only', *args],
                              env=env, capture_output=True, text=True)

    def test_pairing_passes_the_code_only_through_the_environment(self):
        for args, environment in (
                (('--app-config-dir', 'C:\\pv-test', '--pair-origin', 'http://localhost:8010', '--pair-code', 'ABC123'), {}),
                (('--app-config-dir', 'C:\\pv-test'), {'PIXELVIEW_PAIR_CODE': 'ABC123', 'PIXELVIEW_PAIR_ORIGIN': 'http://localhost:8010'})):
            with self.subTest(args=args):
                out = self.run_launch(*args, **environment)
                self.assertEqual(out.returncode, 0, out.stderr)
                self.assertNotIn('ABC123', out.stdout)
                plan = json.loads(out.stdout)
                self.assertTrue(plan['command'][0].endswith('Pixelview.exe'))
                self.assertEqual(plan['command'][1:], ['--multi', '--app-config-dir', 'C:\\pv-test'])
                self.assertEqual(plan['environment'], {'PIXELVIEW_LOCAL_DEVELOPMENT': '1', 'PIXELVIEW_PAIR_CODE': '<set>',
                                                       'PIXELVIEW_PAIR_ORIGIN': 'http://localhost:8010'})

    def test_never_pairs_the_default_settings_root(self):
        for args, environment in ((('--pair-code', 'ABC123'), {}), ((), {'PIXELVIEW_PAIR_CODE': 'ABC123'}),
                                  (('--app-config-dir', 'pv-test', '--pair-code', 'ABC123'), {}),
                                  (('--app-config-dir', 'C:\\pv-test', '--pair-origin', 'http://localhost:8010'), {})):
            with self.subTest(args=args, environment=environment):
                out = self.run_launch(*args, **environment)
                self.assertEqual(out.returncode, 2)
                self.assertTrue(out.stderr.startswith('error: '))

    def test_plain_launch_has_no_pairing_or_development_switch(self):
        out = self.run_launch()
        self.assertEqual(out.returncode, 0, out.stderr)
        plan = json.loads(out.stdout)
        self.assertEqual(len(plan['command']), 1)
        self.assertEqual(plan['environment'], {})
        plan = json.loads(self.run_launch('--local-development').stdout)
        self.assertEqual(plan['environment'], {'PIXELVIEW_LOCAL_DEVELOPMENT': '1'})


if __name__ == '__main__':
    unittest.main()
