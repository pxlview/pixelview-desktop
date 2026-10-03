#!/usr/bin/env python3
"""Pixelview Desktop Windows release (x64). Operator-run only, never a side effect.

  python cmake/windows/pixelview-release.py --validate-config
  python cmake/windows/pixelview-release.py --prepare          # build, sign, package, appcast
  python cmake/windows/pixelview-release.py --publish          # R2 upload, appcast last, latest/
  python cmake/windows/pixelview-release.py --publish-latest   # re-point latest/ only

Run through release/pixelview-windows.ps1, which injects credentials with
`op run` only for the phase that needs them:
  --prepare: PIXELVIEW_WINSPARKLE_PRIVATE_KEY (EdDSA private key, PEM text) and,
             for trusted-signing, the Azure identity variables in the metadata.
  --publish: PIXELVIEW_R2_ENDPOINT, PIXELVIEW_R2_BUCKET, AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY.
Secret values are never printed or passed on a command line. The EdDSA key
exists on disk only inside a private temporary directory for the duration of
the winsparkle-tool call.

The flow mirrors cmake/macos/pixelview-release.sh: clean tagged tree, OBS base
ancestry, corresponding-source materials, fail-closed verification of every
artifact, immutable release assets uploaded before the appcast, and an atomic
(If-Match / If-None-Match) appcast write that can never roll builds back.
"""
import argparse
import base64
import contextlib
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPARKLE_NS = 'http://www.andymatuschak.org/xml-namespaces/sparkle'
EXPECTED_APPCAST = 'https://downloads.pixelview.io/desktop/windows/appcast-x64.xml'
EXPECTED_PREFIX = 'desktop/windows'
INSTALLER_ARGUMENTS = '/SILENT /SP- /NOCANCEL /NORESTART'
MAX_FEED_ITEMS = 10
NO_CACHE = 'no-cache, max-age=0, must-revalidate'
IMMUTABLE = 'public,max-age=31536000,immutable'
OBS_UPDATE_MARKERS = (b'obsproject.com/update_studio', b'obsproject.com/osx_update')

ET.register_namespace('sparkle', SPARKLE_NS)


class ReleaseError(Exception):
    pass


def die(message):
    raise ReleaseError(message)


def run(*command, **kwargs):
    kwargs.setdefault('check', True)
    return subprocess.run([str(c) for c in command], cwd=kwargs.pop('cwd', ROOT), **kwargs)


def git(*args):
    return run('git', *args, capture_output=True, text=True).stdout.strip()


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


class Release:
    def __init__(self):
        self.config = json.loads((ROOT / 'release/windows.json').read_text(encoding='utf-8'))
        versions = json.loads((ROOT / 'version.json').read_text(encoding='utf-8'))
        self.version = versions['pixelview_version']
        self.build = int(versions['pixelview_build_number'])
        self.obs_base_version = versions['obs_base_version']
        self.obs_base_describe = versions['obs_base_describe']
        self.obs_base_commit = versions['obs_base_commit']
        self.release_id = f'{self.version}-{self.build}'
        self.tag = f'v{self.version}'
        self.base_url = self.config['download_base_url']
        self.appcast_url = self.config['appcast_url']
        self.prefix = self.config['r2_prefix']
        self.root = ROOT / 'dist/windows'
        self.release_dir = self.root / 'releases' / self.release_id
        self.appcast_path = self.root / 'appcast-x64.xml'
        self.installer_name = f'Pixelview-Desktop-{self.version}-build{self.build}-x64-setup.exe'
        self.installer = self.release_dir / self.installer_name
        self.notes_source = ROOT / f'docs/releases/{self.version}.html'
        self.notes_name = f'Pixelview-Desktop-{self.version}-build{self.build}-x64.html'
        self.build_dir = ROOT / f'build_x64_release_{self.version}_{self.build}'
        self.rundir = self.build_dir / 'rundir' / 'Release'
        self.source_cache = pathlib.Path(os.environ.get('PIXELVIEW_SOURCE_CACHE', pathlib.Path.home() / '.cache/pixelview-sources'))

    # ------------------------------------------------------------------ config
    def validate_config(self):
        c = self.config
        if c.get('architecture') != 'x64': die('unexpected architecture')
        if self.appcast_url != EXPECTED_APPCAST: die('unexpected appcast URL')
        if self.prefix != EXPECTED_PREFIX: die('unexpected R2 prefix')
        if 'obsproject.com' in self.appcast_url + self.base_url: die('Pixelview must never use OBS update infrastructure')
        if not re.fullmatch(r'[0-9a-f]{64}', c.get('winsparkle_sha256', '')): die('winsparkle_sha256 must be a SHA-256')
        try:
            key = base64.b64decode(c.get('winsparkle_public_key', ''), validate=True)
        except ValueError:
            key = b''
        if len(key) != 32: die('winsparkle_public_key must be a base64 Ed25519 public key (run winsparkle-tool generate-key once)')
        signing = c.get('signing', {})
        if signing.get('method') == 'certificate':
            if not re.fullmatch(r'[0-9A-Fa-f]{40}', signing.get('certificate_sha1', '')): die('signing.certificate_sha1 must be a SHA-1 thumbprint')
        elif signing.get('method') == 'trusted-signing':
            if not signing.get('trusted_signing_metadata'): die('signing.trusted_signing_metadata must name the Trusted Signing metadata JSON')
        else:
            die('signing.method must be "certificate" or "trusted-signing" (Authenticode is required for release)')
        if not signing.get('timestamp_url', '').startswith('http'): die('signing.timestamp_url is required')
        if not (ROOT / c['source_inventory']).is_file():
            die(f"{c['source_inventory']} is missing: Windows corresponding-source materials must be reviewed before release")
        print(f'Configuration OK: {self.version} build {self.build}, feed {self.appcast_url}')

    # ------------------------------------------------------------------ tools
    @staticmethod
    def tool(name, extra_dirs=()):
        found = shutil.which(name)
        if found: return found
        for directory in extra_dirs:
            for candidate in sorted(pathlib.Path(directory).glob(f'**/{name}'), reverse=True):
                return str(candidate)
        die(f'{name} was not found')

    def signtool(self):
        kits = pathlib.Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Windows Kits/10/bin'
        return self.tool('signtool.exe', [kits / f'10.0.26100.0/x64', kits])

    def iscc(self):
        programs = [pathlib.Path(os.environ.get(v, '')) / 'Inno Setup 6' for v in ('ProgramFiles(x86)', 'ProgramFiles', 'LOCALAPPDATA')]
        return self.tool('ISCC.exe', [p for p in programs if p.parts])

    def sign_command(self, path_placeholder):
        signing = self.config['signing']
        command = [self.signtool(), 'sign', '/fd', 'SHA256', '/tr', signing['timestamp_url'], '/td', 'SHA256']
        if signing['method'] == 'certificate':
            command += ['/sha1', signing['certificate_sha1']]
        else:
            dlib = os.environ.get('PIXELVIEW_TRUSTED_SIGNING_DLIB')
            if not dlib or not pathlib.Path(dlib).is_file():
                die('PIXELVIEW_TRUSTED_SIGNING_DLIB must point at Azure.CodeSigning.Dlib.dll')
            command += ['/dlib', dlib, '/dmdf', str(ROOT / signing['trusted_signing_metadata'])]
        return command + [path_placeholder]

    def authenticode_valid(self, path):
        return run(self.signtool(), 'verify', '/pa', '/q', path, check=False, capture_output=True).returncode == 0

    # ------------------------------------------------------------------ prepare
    def preflight(self):
        if platform.system() != 'Windows': die('Windows releases are prepared on Windows')
        if git('status', '--porcelain'): die('the working tree must be clean')
        head = git('rev-parse', 'HEAD')
        tagged = run('git', 'rev-parse', f'{self.tag}^{{commit}}', capture_output=True, text=True, check=False).stdout.strip()
        if tagged != head: die(f'{self.tag} must point at HEAD')
        if run('git', 'merge-base', '--is-ancestor', self.obs_base_commit, 'HEAD', check=False).returncode != 0:
            die('the OBS base commit is not an ancestor of HEAD')
        if not self.notes_source.is_file(): die(f'missing release notes: {self.notes_source.relative_to(ROOT)}')
        if not os.environ.get('PIXELVIEW_WINSPARKLE_PRIVATE_KEY'): die('PIXELVIEW_WINSPARKLE_PRIVATE_KEY is not set (run through release/pixelview-windows.ps1)')
        for tool in ('openssl', 'curl'):
            if not shutil.which(tool): die(f'{tool} is required')
        self.signtool(); self.iscc()
        self.source_commit = head

    def compliance(self, stage):
        result = run(sys.executable, ROOT / 'cmake/macos/pixelview_sources.py', '--root', ROOT, '--tag', self.tag,
                     '--inventory', self.config['source_inventory'], '--cache', self.source_cache, '--output', stage,
                     '--release-id', self.release_id, '--base-url', self.base_url, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            sys.stderr.write(result.stderr)
            die('corresponding-source review/materials incomplete')
        return json.loads(result.stdout)

    def build_app(self, license_dir):
        if self.build_dir.exists(): shutil.rmtree(self.build_dir)
        env = dict(os.environ, PIXELVIEW_RELEASE_BUILD='ON', PIXELVIEW_BUILD_CONFIG='Release',
                   PIXELVIEW_SOURCE_COMMIT=self.source_commit, PIXELVIEW_SOURCE_TAG=self.tag,
                   PIXELVIEW_LICENSE_DATA_DIR=str(license_dir))
        # Credentials are not needed (or passed) to compile.
        for secret in ('PIXELVIEW_WINSPARKLE_PRIVATE_KEY', 'AWS_ACCESS_KEY_ID', 'AWS_SECRET_ACCESS_KEY'):
            env.pop(secret, None)
        run(sys.executable, ROOT / 'cmake/windows/pixelview-build.py', '--config', 'Release', env=env)
        if not (self.rundir / 'bin/64bit/Pixelview.exe').is_file(): die('release build produced no Pixelview.exe')
        if not (self.rundir / 'bin/64bit/WinSparkle.dll').is_file(): die('release build is missing WinSparkle.dll')

    def sign_binaries(self):
        binaries = sorted(p for p in self.rundir.rglob('*') if p.suffix.lower() in ('.exe', '.dll'))
        unsigned = [p for p in binaries if not self.authenticode_valid(p)]
        print(f'Signing {len(unsigned)} of {len(binaries)} binaries (vendor-signed ones are kept)')
        for start in range(0, len(unsigned), 50):
            command = self.sign_command('')[:-1] + [str(p) for p in unsigned[start:start + 50]]
            run(*command)
        for path in binaries:
            if not self.authenticode_valid(path): die(f'unsigned after signing: {path.relative_to(self.rundir)}')
            data = path.read_bytes()
            for marker in OBS_UPDATE_MARKERS:
                if marker in data: die(f'{path.name} embeds OBS update infrastructure ({marker.decode()})')

    def stage_licenses(self, compliance_stage, stage):
        stage.mkdir(parents=True)
        for name in ('COPYING', 'AUTHORS'):
            shutil.copy2(ROOT / name, stage / name)
        shutil.copy2(compliance_stage / f'Pixelview-Desktop-{self.release_id}-NOTICES.txt', stage / 'THIRD-PARTY-NOTICES.txt')
        winsparkle_license = ROOT / '.deps' / f"winsparkle-{self.config['winsparkle_version']}" / 'COPYING'
        if not winsparkle_license.is_file(): die('the staged WinSparkle package has no COPYING')
        shutil.copy2(winsparkle_license, stage / 'WinSparkle-COPYING.txt')
        (stage / 'RELEASE.txt').write_text(
            f'Pixelview Desktop {self.version} (build {self.build})\nArchitecture: x64\n'
            f"Source: {self.config['source_repository']}/tree/{self.tag}\nSource commit: {self.source_commit}\n"
            f'OBS base: {self.obs_base_describe} ({self.obs_base_commit})\n', encoding='utf-8')

    def build_installer(self, licenses):
        # Inno Setup substitutes $f with the quoted file and $q with a quote.
        sign = subprocess.list2cmdline(self.sign_command('$f')).replace('"', '$q')
        run(self.iscc(), f'/DAppVersion={self.version}', f'/DBuildNumber={self.build}', f'/DSourceDir={self.rundir}',
            f'/DStageDir={licenses}', f"/DIconFile={ROOT / 'frontend/data/images/pixelview-app.ico'}",
            f'/DOutputDir={self.release_dir}', f'/DOutputName={self.installer.stem}', '/DSign',
            f'/Spixelview={sign}', ROOT / 'cmake/windows/pixelview-installer.iss')
        if not self.installer.is_file(): die('Inno Setup produced no installer')
        if not self.authenticode_valid(self.installer): die('installer Authenticode signature is invalid')

    def eddsa_sign(self, path):
        """Returns (signature, length). The private key is on disk only inside a private temp dir."""
        tool = ROOT / '.deps' / f"winsparkle-{self.config['winsparkle_version']}" / 'bin/winsparkle-tool.exe'
        if not tool.is_file(): die('winsparkle-tool.exe is not staged; run the release build first')
        with tempfile.TemporaryDirectory(prefix='pixelview-eddsa-') as private:
            key = pathlib.Path(private) / 'key.pem'
            fd = os.open(key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, 'w', encoding='ascii') as handle:
                handle.write(os.environ['PIXELVIEW_WINSPARKLE_PRIVATE_KEY'])
            try:
                output = run(tool, 'sign', '--private-key-file', key, path, capture_output=True, text=True).stdout
            finally:
                key.unlink(missing_ok=True)
        match = re.search(r'sparkle:edSignature="([A-Za-z0-9+/=]+)"\s+length="(\d+)"', output)
        if not match: die('winsparkle-tool did not return an EdDSA signature')
        signature, length = match.group(1), int(match.group(2))
        if length != path.stat().st_size: die('EdDSA signature length does not match the installer')
        self.verify_eddsa(path, signature)
        return signature, length

    def verify_eddsa(self, path, signature):
        """Independent check with OpenSSL against the public key compiled into the app."""
        public = base64.b64decode(self.config['winsparkle_public_key'])
        der = bytes.fromhex('302a300506032b6570032100') + public
        with tempfile.TemporaryDirectory(prefix='pixelview-verify-') as scratch:
            pem = pathlib.Path(scratch) / 'public.pem'
            pem.write_text('-----BEGIN PUBLIC KEY-----\n' + base64.b64encode(der).decode() + '\n-----END PUBLIC KEY-----\n')
            sig = pathlib.Path(scratch) / 'signature.bin'
            sig.write_bytes(base64.b64decode(signature))
            result = run('openssl', 'pkeyutl', '-verify', '-pubin', '-inkey', pem, '-rawin', '-in', path, '-sigfile', sig,
                         check=False, capture_output=True, text=True)
        if result.returncode != 0 or 'Verified Successfully' not in result.stdout:
            die('installer EdDSA signature does not verify against winsparkle_public_key')

    # ------------------------------------------------------------------ appcast
    @staticmethod
    def feed_builds(xml_bytes):
        builds = []
        for item in ET.fromstring(xml_bytes).iter('item'):
            enclosure = item.find('enclosure')
            version = item.findtext(f'{{{SPARKLE_NS}}}version') or (enclosure.get(f'{{{SPARKLE_NS}}}version') if enclosure is not None else None)
            if version is None or not version.isdigit(): die('published appcast item has no numeric sparkle:version')
            builds.append(int(version))
        return builds

    def fetch_current_appcast(self):
        try:
            with urllib.request.urlopen(urllib.request.Request(self.appcast_url, headers={'Cache-Control': 'no-cache'}), timeout=60) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            if error.code == 404: return None
            die(f'current appcast returned HTTP {error.code}')
        except urllib.error.URLError:
            die('current appcast could not be fetched; refusing to infer an empty feed')

    def check_progression(self, current):
        builds = self.feed_builds(current) if current else []
        if builds and self.build <= max(builds): die(f'build {self.build} would not advance the feed (latest is {max(builds)})')
        if not builds and current is not None: die('the published appcast is empty; refusing to reinitialize it')

    def write_appcast(self, current, signature, length):
        self.check_progression(current)
        rss = ET.Element('rss', {'version': '2.0'})
        channel = ET.SubElement(rss, 'channel')
        ET.SubElement(channel, 'title').text = 'Pixelview Desktop for Windows'
        ET.SubElement(channel, 'link').text = self.appcast_url
        item = ET.SubElement(channel, 'item')
        ET.SubElement(item, 'title').text = f'Pixelview Desktop {self.version}'
        ET.SubElement(item, f'{{{SPARKLE_NS}}}version').text = str(self.build)
        ET.SubElement(item, f'{{{SPARKLE_NS}}}shortVersionString').text = self.version
        ET.SubElement(item, f'{{{SPARKLE_NS}}}releaseNotesLink').text = f'{self.base_url}/releases/{self.release_id}/{self.notes_name}'
        ET.SubElement(item, f'{{{SPARKLE_NS}}}minimumSystemVersion').text = self.config['minimum_windows']
        ET.SubElement(item, 'link').text = f"{self.config['source_repository']}/releases/tag/{self.tag}"
        ET.SubElement(item, 'enclosure', {
            'url': f'{self.base_url}/releases/{self.release_id}/{self.installer_name}',
            'length': str(length), 'type': 'application/octet-stream',
            f'{{{SPARKLE_NS}}}os': 'windows-x64',
            f'{{{SPARKLE_NS}}}installerArguments': INSTALLER_ARGUMENTS,
            f'{{{SPARKLE_NS}}}edSignature': signature,
        })
        if current:
            for previous in list(ET.fromstring(current).iter('item'))[:MAX_FEED_ITEMS - 1]:
                channel.append(previous)
        ET.indent(rss)
        self.appcast_path.write_bytes(b'<?xml version="1.0" encoding="utf-8"?>\n' + ET.tostring(rss, encoding='utf-8'))

    # ------------------------------------------------------------------ manifest
    def manifest(self, compliance):
        return {
            'compliance': compliance, 'product': 'Pixelview Desktop', 'version': self.version, 'build_number': self.build,
            'release_id': self.release_id, 'architecture': 'x64', 'platform': 'windows',
            'source_commit': self.source_commit, 'source_tag': self.tag,
            'source_url': f"{self.config['source_repository']}/tree/{self.tag}",
            'obs_base_version': self.obs_base_version, 'obs_base_describe': self.obs_base_describe,
            'obs_base_commit': self.obs_base_commit, 'artifact': self.installer_name, 'sha256': sha256(self.installer),
        }

    def prepare(self):
        self.validate_config()
        self.preflight()
        if self.release_dir.exists(): shutil.rmtree(self.release_dir)
        self.appcast_path.unlink(missing_ok=True)
        self.release_dir.mkdir(parents=True)
        with tempfile.TemporaryDirectory(prefix='pixelview-compliance-') as compliance_dir, \
             tempfile.TemporaryDirectory(prefix='pixelview-licenses-') as licenses_dir:
            compliance_stage = pathlib.Path(compliance_dir)
            compliance = self.compliance(compliance_stage)
            self.build_app(compliance_stage / 'license')
            self.sign_binaries()
            licenses = pathlib.Path(licenses_dir) / 'Licenses'
            self.stage_licenses(compliance_stage, licenses)
            self.build_installer(licenses)
            signature, length = self.eddsa_sign(self.installer)
            self.write_appcast(self.fetch_current_appcast(), signature, length)
            shutil.copy2(self.notes_source, self.release_dir / self.notes_name)
            (self.release_dir / f'{self.installer_name}.sha256').write_text(f'{sha256(self.installer)}  {self.installer_name}\n')
            for suffix in ('sources.tar.gz', 'NOTICES.txt', 'source-inventory.json'):
                shutil.copy2(compliance_stage / f'Pixelview-Desktop-{self.release_id}-{suffix}', self.release_dir)
            (self.release_dir / 'release-manifest.json').write_text(json.dumps(self.manifest(compliance), sort_keys=True, indent=2) + '\n')
        self.verify_prepared()
        print(f'Prepared {self.installer}\nStaged appcast: {self.appcast_path}')

    def release_files(self):
        files = [(self.installer, 'application/vnd.microsoft.portable-executable'),
                 (self.release_dir / f'{self.installer_name}.sha256', 'text/plain'),
                 (self.release_dir / self.notes_name, 'text/html'),
                 (self.release_dir / 'release-manifest.json', 'application/json')]
        for suffix, kind in (('sources.tar.gz', 'application/gzip'), ('NOTICES.txt', 'text/plain; charset=utf-8'),
                             ('source-inventory.json', 'application/json')):
            files.append((self.release_dir / f'Pixelview-Desktop-{self.release_id}-{suffix}', kind))
        return files

    def verify_prepared(self):
        for path, _ in self.release_files():
            if not path.is_file(): die(f'prepared release is incomplete: {path.name}')
        if not self.appcast_path.is_file(): die('missing staged appcast')
        manifest = json.loads((self.release_dir / 'release-manifest.json').read_text())
        if manifest['sha256'] != sha256(self.installer) or manifest['release_id'] != self.release_id:
            die('installer does not match its manifest')
        feed = ET.fromstring(self.appcast_path.read_bytes())
        first = next(feed.iter('item'), None)
        enclosure = first.find('enclosure') if first is not None else None
        if enclosure is None or enclosure.get('url') != f'{self.base_url}/releases/{self.release_id}/{self.installer_name}':
            die('appcast does not lead with this release')
        if int(enclosure.get('length')) != self.installer.stat().st_size: die('appcast length does not match the installer')
        self.verify_eddsa(self.installer, enclosure.get(f'{{{SPARKLE_NS}}}edSignature'))
        if platform.system() == 'Windows' and not self.authenticode_valid(self.installer):
            die('installer Authenticode signature is invalid')

    # ------------------------------------------------------------------ publish
    def r2_credentials(self):
        endpoint, bucket = os.environ.get('PIXELVIEW_R2_ENDPOINT', ''), os.environ.get('PIXELVIEW_R2_BUCKET', '')
        if not re.fullmatch(r'https://[0-9a-fA-F]{32}\.r2\.cloudflarestorage\.com', endpoint): die('PIXELVIEW_R2_ENDPOINT is invalid')
        if not re.fullmatch(r'[a-z0-9][a-z0-9.-]{1,61}[a-z0-9]', bucket): die('PIXELVIEW_R2_BUCKET is invalid')
        if not re.fullmatch(r'[A-Za-z0-9]{16,64}', os.environ.get('AWS_ACCESS_KEY_ID', '')): die('AWS_ACCESS_KEY_ID is invalid')
        if not re.fullmatch(r'[A-Za-z0-9+/=_-]{32,128}', os.environ.get('AWS_SECRET_ACCESS_KEY', '')): die('AWS_SECRET_ACCESS_KEY is invalid')
        return endpoint, bucket

    def r2(self, method, key, *, upload=None, headers=(), output=None):
        """Signed request through curl; credentials go through stdin config, never argv."""
        endpoint, bucket = self.r2_credentials()
        with tempfile.TemporaryDirectory(prefix='pixelview-r2-') as scratch:
            body = pathlib.Path(scratch) / 'body'
            head = pathlib.Path(scratch) / 'headers'
            command = ['curl', '--silent', '--show-error', '--config', '-', '--aws-sigv4', 'aws:amz:auto:s3',
                       '--request', method, '--dump-header', str(head), '--output', str(output or body), '--write-out', '%{http_code}']
            for header in headers: command += ['--header', header]
            if upload: command += ['--upload-file', str(upload)]
            command.append(f'{endpoint}/{bucket}/{key}')
            config = 'user = "{}:{}"\n'.format(os.environ['AWS_ACCESS_KEY_ID'], os.environ['AWS_SECRET_ACCESS_KEY'])
            result = subprocess.run(command, input=config, capture_output=True, text=True)
            if result.returncode != 0: die(f'R2 {method} transport failed for {key}')
            status = int(result.stdout.strip() or 0)
            response_headers = {}
            if head.is_file():
                for line in head.read_text(errors='replace').splitlines():
                    name, _, value = line.partition(':')
                    if value: response_headers[name.strip().lower()] = value.strip()
            text = body.read_text(errors='replace') if body.is_file() and not output else ''
            return status, response_headers, text

    def put(self, key, path, content_type, cache_control, precondition, extra=()):
        status, _, text = self.r2('PUT', key, upload=path, headers=[f'Content-Type: {content_type}', f'Cache-Control: {cache_control}', precondition, *extra])
        if status not in (200, 201, 204):
            sys.stderr.write(text[:2000] + '\n')
            die(f'R2 rejected the write of {key} (HTTP {status})')

    def preflight_immutable(self, key, path):
        status, _, _ = self.r2('HEAD', key)
        if status == 404: return 'missing'
        if status != 200: die(f'could not prove absence of {key} (HTTP {status})')
        with tempfile.TemporaryDirectory(prefix='pixelview-r2-existing-') as scratch:
            existing = pathlib.Path(scratch) / 'object'
            status, _, _ = self.r2('GET', key, output=existing)
            if status != 200 or sha256(existing) != sha256(path): die(f'immutable R2 object differs from the prepared asset: {key}')
        return 'existing'

    def public_matches(self, url, path):
        with tempfile.TemporaryDirectory(prefix='pixelview-public-') as scratch:
            target = pathlib.Path(scratch) / 'download'
            run('curl', '-fsSL', '--retry', '3', '-H', 'Cache-Control: no-cache', '-o', target, url)
            return sha256(target) == sha256(path)

    def verify_remote_tag(self):
        source = self.config['source_repository']
        lines = git('ls-remote', '--tags', source, f'refs/tags/{self.tag}', f'refs/tags/{self.tag}^{{}}').splitlines()
        refs = dict(reversed(line.split('\t')) for line in lines if '\t' in line)
        remote = refs.get(f'refs/tags/{self.tag}^{{}}') or refs.get(f'refs/tags/{self.tag}')
        if remote != self.source_commit: die(f'push {self.tag} to {source} before publishing')

    def publish(self):
        self.validate_config()
        self.source_commit = json.loads((self.release_dir / 'release-manifest.json').read_text())['source_commit']
        if git('rev-parse', f'{self.tag}^{{commit}}') != self.source_commit: die('the prepared release does not match the local tag')
        self.verify_remote_tag()
        self.verify_prepared()
        self.r2_credentials()
        key_prefix = f'{self.prefix}/releases/{self.release_id}'
        files = self.release_files()
        states = [self.preflight_immutable(f'{key_prefix}/{path.name}', path) for path, _ in files]
        # Atomic feed precondition, established before any write.
        appcast_key = f'{self.prefix}/appcast-x64.xml'
        status, headers, _ = self.r2('HEAD', appcast_key)
        if status == 200:
            if not headers.get('etag'): die('existing appcast has no usable R2 ETag')
            with tempfile.TemporaryDirectory(prefix='pixelview-feed-') as scratch:
                current = pathlib.Path(scratch) / 'appcast.xml'
                self.r2('GET', appcast_key, output=current)
                self.check_progression(current.read_bytes())
            precondition = f"If-Match: {headers['etag']}"
        elif status == 404:
            precondition = 'If-None-Match: *'
        else:
            die(f'could not establish an atomic appcast precondition (HTTP {status})')
        for (path, kind), state in zip(files, states):
            if state == 'missing':
                self.put(f'{key_prefix}/{path.name}', path, kind, IMMUTABLE, 'If-None-Match: *')
        for path, _ in files:
            if not self.public_matches(f'{self.base_url}/releases/{self.release_id}/{path.name}', path):
                die(f'public R2 object differs after upload: {path.name}')
        self.put(appcast_key, self.appcast_path, 'application/xml', NO_CACHE, precondition)
        if not self.public_matches(self.appcast_url, self.appcast_path): die('public appcast differs from the staged appcast')
        print(f'Published {self.appcast_url} after verifying immutable release assets.')
        self.publish_latest()

    def publish_latest(self):
        manifest = json.loads((self.release_dir / 'release-manifest.json').read_text())
        if manifest['release_id'] != self.release_id or manifest['sha256'] != sha256(self.installer):
            die('prepared installer does not match its manifest')
        if not self.public_matches(f'{self.base_url}/releases/{self.release_id}/{self.installer_name}', self.installer):
            die('release installer is not public yet; publish the release first')
        endpoint, bucket = self.r2_credentials()
        latest_key = f'{self.prefix}/latest/Pixelview-Desktop-x64-setup.exe'
        status, _, text = self.r2('PUT', latest_key, headers=[
            f'x-amz-copy-source: /{bucket}/{self.prefix}/releases/{self.release_id}/{self.installer_name}',
            'x-amz-metadata-directive: REPLACE', f'x-amz-meta-pixelview-release: {self.tag}',
            'Content-Type: application/vnd.microsoft.portable-executable', f'Cache-Control: {NO_CACHE}'])
        if status != 200 or '<CopyObjectResult' not in text: die('R2 rejected the latest/ copy')
        with tempfile.TemporaryDirectory(prefix='pixelview-latest-') as scratch:
            latest_json = pathlib.Path(scratch) / 'latest.json'
            latest_json.write_text(json.dumps({
                'version': self.version, 'build': self.build, 'release_id': self.release_id, 'tag': manifest['source_tag'],
                'commit': manifest['source_commit'], 'installer': f'{self.base_url}/releases/{self.release_id}/{self.installer_name}',
                'latest_installer': f'{self.base_url}/latest/Pixelview-Desktop-x64-setup.exe', 'sha256': manifest['sha256'],
                'size': self.installer.stat().st_size, 'notes': f'{self.base_url}/releases/{self.release_id}/{self.notes_name}',
                'appcast': self.appcast_url, 'minimum_windows': self.config['minimum_windows'], 'architecture': 'x64',
            }, indent=2) + '\n')
            self.put(f'{self.prefix}/latest/latest.json', latest_json, 'application/json', NO_CACHE,
                     f'x-amz-meta-pixelview-release: {self.tag}')
            if not self.public_matches(f'{self.base_url}/latest/latest.json', latest_json): die('public latest.json differs')
        if not self.public_matches(f'{self.base_url}/latest/Pixelview-Desktop-x64-setup.exe', self.installer):
            die('public latest installer differs from the release installer')
        print(f'Latest pointer: {self.base_url}/latest/Pixelview-Desktop-x64-setup.exe -> {self.release_id}')


@contextlib.contextmanager
def release_lock(root):
    root.mkdir(parents=True, exist_ok=True)
    lock = root / '.release.lock'
    try:
        fd = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL)
    except FileExistsError:
        die(f'another release run holds {lock}')
    try:
        os.write(fd, str(os.getpid()).encode()); os.close(fd)
        yield
    finally:
        lock.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    for flag in ('--validate-config', '--prepare', '--publish', '--publish-latest'):
        mode.add_argument(flag, action='store_true')
    args = parser.parse_args()
    try:
        release = Release()
        if args.validate_config:
            release.validate_config(); return 0
        with release_lock(release.root):
            if args.prepare: release.prepare()
            elif args.publish: release.publish()
            else:
                release.validate_config()
                release.source_commit = json.loads((release.release_dir / 'release-manifest.json').read_text())['source_commit']
                release.publish_latest()
    except ReleaseError as error:
        print(f'error: {error}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
