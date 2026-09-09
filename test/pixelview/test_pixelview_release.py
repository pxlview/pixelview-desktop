"""Release contracts for Pixelview Desktop macOS artifacts."""
import hashlib
import importlib.util
import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class PixelviewReleaseMetadata(unittest.TestCase):
    def test_release_uses_renamed_bundle_and_executable_everywhere(self):
        script = (ROOT / 'cmake/macos/pixelview-release.sh').read_text()
        for expected in (
            'app_path="$build_dir/frontend/Release/Pixelview Desktop.app"',
            'verify_app "$mountpoint/Pixelview Desktop.app"',
            'ditto "$app_path" "$stage/Pixelview Desktop.app"',
            'lipo -archs "$app_to_verify/Contents/MacOS/Pixelview Desktop"',
            'strings "$app_to_verify/Contents/MacOS/Pixelview Desktop"',
        ):
            self.assertIn(expected, script)
        self.assertNotIn('Pixelview.app', script)
        self.assertNotIn('Contents/MacOS/Pixelview"', script)

    def test_product_version_and_obs_base_are_independent_and_pinned(self):
        metadata_path = ROOT / "version.json"
        self.assertTrue(metadata_path.is_file(), "version.json is the release source of truth")
        metadata = json.loads(metadata_path.read_text())
        self.assertEqual(metadata["pixelview_version"], "0.0.1")
        self.assertEqual(metadata["pixelview_build_number"], 1)
        self.assertEqual(metadata["obs_base_version"], "32.2.1")
        self.assertEqual(metadata["obs_base_describe"], "32.2.1-66-g6b3e55072")
        self.assertEqual(
            metadata["obs_base_commit"],
            "6b3e550729f125b6c5b3767df88c08f5aef9d264",
        )

        gitignore = (ROOT / ".gitignore").read_text()
        self.assertIn("!/release", gitignore)
        self.assertIn("!version.json", gitignore)
        for tracked_release_input in (
            "version.json",
            "release/macos.json",
            "docs/releases/0.0.1.html",
            "docs/releases/0.0.1.md",
        ):
            ignored = subprocess.run(
                ["git", "check-ignore", "-q", tracked_release_input],
                cwd=ROOT,
                check=False,
            )
            self.assertNotEqual(
                ignored.returncode,
                0,
                f"release input is ignored by git: {tracked_release_input}",
            )

        ignored_bytecode = subprocess.run(
            ["git", "check-ignore", "-q", "cmake/macos/__pycache__/validator.pyc"],
            cwd=ROOT,
            check=False,
        )
        self.assertEqual(ignored_bytecode.returncode, 0, "Python bytecode must stay out of releases")

        version_cmake = (ROOT / "cmake/common/pixelview-version.cmake").read_text()
        self.assertIn('file(READ "${CMAKE_SOURCE_DIR}/version.json"', version_cmake)
        self.assertIn("PIXELVIEW_VERSION", version_cmake)
        self.assertIn("PIXELVIEW_BUILD_NUMBER", version_cmake)
        self.assertIn("PIXELVIEW_OBS_BASE_VERSION", version_cmake)
        self.assertIn("PIXELVIEW_OBS_BASE_DESCRIBE", version_cmake)
        self.assertIn("PIXELVIEW_OBS_BASE_COMMIT", version_cmake)

        helpers = (ROOT / "cmake/macos/helpers.cmake").read_text()
        application_block = helpers.split("if(target STREQUAL obs-studio)", 1)[1].split(
            "elseif(${target} STREQUAL mac-camera-extension)", 1
        )[0]
        self.assertIn("CURRENT_PROJECT_VERSION ${PIXELVIEW_BUILD_NUMBER}", application_block)
        self.assertIn("MARKETING_VERSION ${PIXELVIEW_VERSION}", application_block)
        self.assertNotIn("CURRENT_PROJECT_VERSION ${OBS_BUILD_NUMBER}", application_block)
        self.assertNotIn("MARKETING_VERSION ${OBS_VERSION_CANONICAL}", application_block)

        frontend_cmake = (ROOT / "frontend/CMakeLists.txt").read_text()
        self.assertIn('PIXELVIEW_VERSION=\\"${PIXELVIEW_VERSION}\\"', frontend_cmake)
        self.assertIn('PIXELVIEW_OBS_BASE_VERSION=\\"${PIXELVIEW_OBS_BASE_VERSION}\\"', frontend_cmake)
        self.assertIn('PIXELVIEW_OBS_BASE_DESCRIBE=\\"${PIXELVIEW_OBS_BASE_DESCRIBE}\\"', frontend_cmake)
        main = (ROOT / "frontend/obs-main.cpp").read_text()
        self.assertIn('Pixelview Desktop - " << PIXELVIEW_VERSION', main)
        self.assertNotIn('std::cout << "OBS Studio - "', main)

        plist = (ROOT / "frontend/cmake/macos/Info.plist.in").read_text()
        for key in (
            "PixelviewSourceCommit",
            "PixelviewSourceTag",
            "PixelviewOBSBaseVersion",
            "PixelviewOBSBaseDescribe",
            "PixelviewOBSBaseCommit",
        ):
            self.assertIn(f"<key>{key}</key>", plist)


class PixelviewUpdater(unittest.TestCase):
    def test_arm64_updater_is_pixelview_owned_stable_and_fail_closed(self):
        release = json.loads((ROOT / "release/macos.json").read_text())
        self.assertEqual(release["architecture"], "arm64")
        self.assertEqual(
            release["appcast_url"],
            "https://downloads.pixelview.io/desktop/macos/appcast-arm64.xml",
        )
        self.assertEqual(
            release["sparkle_public_key"],
            "k1+OJc59i2HxlsfpR/lS8Yv4iU1RttFDMYzahG/N0lw=",
        )
        self.assertEqual(release["sparkle_key_account"], "com.pixelview.desktop")

        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        macos = next(item for item in presets["configurePresets"] if item["name"] == "macos")
        self.assertEqual(macos["cacheVariables"]["SPARKLE_APPCAST_URL"]["value"], "")
        self.assertEqual(macos["cacheVariables"]["SPARKLE_PUBLIC_KEY"]["value"], "")

        feature = (ROOT / "frontend/cmake/feature-sparkle.cmake").read_text()
        self.assertIn("PIXELVIEW_RELEASE_BUILD", feature)
        self.assertIn("downloads.pixelview.io", feature)
        self.assertIn("utility/PixelviewSparkle.mm", feature)
        self.assertNotIn("MacUpdateThread", feature)
        self.assertNotIn("feature-macos-update", feature)

        updater = (ROOT / "frontend/widgets/OBSBasic_Updater.cpp").read_text()
        self.assertIn("PixelviewSparkle", updater)
        self.assertNotIn("MacUpdateThread", updater)
        self.assertNotIn("MacBranchesFetched", updater)
        self.assertNotIn("X-OBS2-GUID", updater)

        sparkle = (ROOT / "frontend/utility/PixelviewSparkle.mm").read_text()
        self.assertNotIn("obsproject.com", sparkle)
        self.assertNotIn("feedUrl", sparkle)

        basic = (ROOT / "frontend/widgets/OBSBasic.cpp").read_text()
        pixelview_init = basic.split("void OBSBasic::InitPixelview()", 1)[1].split(
            "void OBSBasic::OnFirstLoad()", 1
        )[0]
        self.assertIn("ui->actionCheckForUpdates", pixelview_init)
        self.assertIn("TimedCheckForUpdates();", basic)

        plist = (ROOT / "frontend/cmake/macos/Info.plist.in").read_text()
        for key in (
            "SUEnableAutomaticChecks",
            "SUAutomaticallyUpdate",
            "SUAllowsAutomaticUpdates",
            "SUVerifyUpdateBeforeExtraction",
            "SURequireSignedFeed",
        ):
            self.assertIn(f"<key>{key}</key>", plist)
        self.assertIn("<key>SUAllowsAutomaticUpdates</key>\n\t<false/>", plist)

        for retired in (
            "MacUpdateThread.cpp",
            "MacUpdateThread.hpp",
            "OBSSparkle.mm",
            "OBSSparkle.hpp",
            "OBSUpdateDelegate.mm",
            "OBSUpdateDelegate.h",
        ):
            self.assertFalse((ROOT / "frontend/utility" / retired).exists(), retired)


class PixelviewLocalRelease(unittest.TestCase):
    def test_compliance_is_generated_before_signing_and_published_with_assets(self):
        script = (ROOT / 'cmake/macos/pixelview-release.sh').read_text()
        prepare = script.split('prepare_release() {', 1)[1].split('\n}\n', 1)[0]
        self.assertIn('prepare_compliance', prepare)
        self.assertLess(prepare.index('prepare_compliance'), prepare.index('resolve_identity_sha1'))
        self.assertIn('PIXELVIEW_LICENSE_DATA_DIR=', prepare)
        self.assertIn('rebuild_compliance_bindings', script.split('expected_release_json() {', 1)[1].split('\n}\n', 1)[0])
        publish = script.split('upload_release_assets() {', 1)[1].split('\n}\n', 1)[0]
        for suffix in ('sources.tar.gz', 'NOTICES.txt', 'source-inventory.json'):
            self.assertIn(suffix, publish)
        self.assertIn('license/source-manifest.json', script)
        self.assertIn('license/third-party-notices.txt', script)
        self.assertIn('cmp -s "$compliance_stage/license/$license_asset"', script)
        self.assertIn('prepare_compliance', script.split('verify_prepared_release() {', 1)[1].split('\n}\n', 1)[0])
        self.assertIn('-DPIXELVIEW_LICENSE_DATA_DIR=', (ROOT / 'cmake/macos/pixelview-build.sh').read_text())

    def test_release_script_is_local_fail_closed_and_r2_appcast_last(self):
        script_path = ROOT / "cmake/macos/pixelview-release.sh"
        self.assertTrue(script_path.is_file())
        script = script_path.read_text()
        for required in (
            "codesign --verify --deep --strict",
            "xcrun notarytool submit",
            "xcrun stapler staple",
            "xcrun stapler validate",
            "spctl --assess",
            "hdiutil create",
            "generate_appcast",
            "AWS_ACCESS_KEY_ID",
            "AWS_SECRET_ACCESS_KEY",
            "appcast-arm64.xml",
            "Cache-Control:",
            "git ls-remote",
            "git merge-base --is-ancestor",
        ):
            self.assertIn(required, script)
        self.assertIn('upload_release_assets', script)
        self.assertIn('upload_appcast', script)
        self.assertIn('-o "$release_dir/appcast-arm64.xml"', script)
        self.assertNotIn("--output appcast-arm64.xml", script)
        self.assertLess(script.index("upload_release_assets\n"), script.index("upload_appcast\n"))
        self.assertNotIn("OBS-Codesign-Password", script)
        self.assertNotIn("obsproject.com", script)

        build = (ROOT / "cmake/macos/pixelview-build.sh").read_text()
        for required in (
            "version.json",
            "PIXELVIEW_RELEASE_BUILD",
            "PIXELVIEW_BUILD_CONFIG",
            "CMAKE_OSX_ARCHITECTURES=arm64",
            "CMAKE_C_COMPILER=$c_compiler",
            "CMAKE_CXX_COMPILER=$cxx_compiler",
            "CMAKE_OSX_SYSROOT=$macos_sdk",
            "ENABLE_WHATSNEW=OFF",
        ):
            self.assertIn(required, build)
        self.assertNotIn("-DOBS_VERSION_OVERRIDE=32.1.0", build)
        self.assertIn("git describe --exact-match --tags", build)
        self.assertNotIn('source_tag="${PIXELVIEW_SOURCE_TAG:-v', build)

    def test_release_config_validation_runs_without_credentials(self):
        import subprocess

        result = subprocess.run(
            ["bash", "cmake/macos/pixelview-release.sh", "--validate-config"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Pixelview Desktop 0.0.1 (build 1)", result.stdout)
        self.assertIn("OBS 32.2.1-66-g6b3e55072 @ 6b3e550729f125b6c5b3767df88c08f5aef9d264", result.stdout)
        self.assertIn("arm64", result.stdout)
    def test_production_release_never_accepts_a_dirty_checkout(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertNotIn("PIXELVIEW_ALLOW_DIRTY", script)
        self.assertIn('"$sparkle_generate_keys" --account', script)
        prepare_body = script.split("prepare_release() {", 1)[1].split("\n}\n", 1)[0]
        self.assertIn('rm -rf -- "$build_dir"', prepare_body)
        self.assertLess(prepare_body.index('rm -rf -- "$build_dir"'), prepare_body.index('bash cmake/macos/pixelview-build.sh'))
    def test_immutable_artifacts_are_build_qualified(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertIn('release_id="$version-$build_number"', script)
        self.assertIn('releases/$release_id', script)
        self.assertIn('Pixelview-Desktop-$version-build$build_number-arm64.dmg', script)
        self.assertNotIn('release_dir="$release_root/releases/$version"', script)
    def test_signing_identity_is_verified_by_fingerprint_and_team(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        config = json.loads((ROOT / "release/macos.json").read_text())
        self.assertEqual(config.get("apple_team_id"), "MA47F3M8W9")
        self.assertIn("resolve_identity_sha1", script)
        self.assertIn("--extract-certificates", script)
        self.assertNotIn('Authority=$identity', script)
    def test_cached_sparkle_tools_are_checksum_verified(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertIn("verify_tool_sha256", script)
        self.assertIn('sparkle_sign_update="$sign_update"', script)
        self.assertIn('"$sparkle_sign_update" --account', script)
    def test_publish_revalidates_prepared_release_before_any_upload(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        publish_body = script.split("upload_release_assets() {", 1)[1].split(
            "upload_appcast() {", 1
        )[0]
        self.assertIn("verify_prepared_release", publish_body)
        self.assertLess(
            publish_body.index("verify_prepared_release"),
            publish_body.index("prepare_appcast_precondition"),
        )
        for required in (
            "pixelview_release_validate.py",
            "verify_sparkle_signatures",
            "hdiutil attach",
            "xcrun stapler validate",
            "spctl --assess",
        ):
            self.assertIn(required, script)
    def test_release_requires_authenticated_monotonic_current_feed(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertIn("verify_build_progression", script)
        self.assertIn("--build-progression", script)
        self.assertIn("current appcast signature is invalid", script)
        self.assertIn('"$http_status" == 404', script)
        self.assertIn('"$http_status" == 200', script)
        self.assertNotIn("PIXELVIEW_FIRST_RELEASE", script)
    def test_r2_objects_are_conflict_checked_before_any_write(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        publish_body = script.split("upload_release_assets() {", 1)[1].split(
            "upload_appcast() {", 1
        )[0]
        self.assertIn("preflight_immutable_object", publish_body)
        self.assertLess(
            publish_body.index("preflight_immutable_object"),
            publish_body.index("r2_conditional_put"),
        )
        self.assertIn("s3api head-object", script)
        self.assertIn("immutable R2 object differs", script)
        self.assertIn('releases/$release_id', publish_body)
        self.assertNotIn('releases/$version/', publish_body)
    def test_temporary_appcast_name_is_randomized_on_macos(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertIn("mktemp -d /tmp/pixelview-current-appcast.XXXXXX", script)
        self.assertIn('current_appcast="$current_appcast_dir/appcast.xml"', script)
        self.assertNotIn('current_appcast="$(mktemp /tmp/pixelview-current-appcast.', script)
        self.assertNotIn("XXXXXX.xml", script)
    def test_r2_endpoint_cannot_redirect_credentials_off_cloudflare(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertIn("^https://[0-9a-fA-F]{32}\\.r2\\.cloudflarestorage\\.com$", script)
        self.assertNotIn('== https://*.r2.cloudflarestorage.com', script)
    def test_signed_release_notes_are_cryptographically_verified(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertIn("releaseNotesLink", script)
        self.assertIn('"$sparkle_sign_update" --account "$sparkle_account" --verify "$notes_path" "$notes_signature"', script)
    def test_local_release_workspace_is_exclusively_locked(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertIn("acquire_release_lock", script)
        self.assertIn('mkdir "$release_lock_dir"', script)
        self.assertIn('rmdir "$release_lock_dir"', script)
        self.assertIn("release_lock_owned=1", script)
    def test_r2_writes_use_server_side_preconditions(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertIn("r2_conditional_put", script)
        self.assertIn("--aws-sigv4", script)
        self.assertIn("If-None-Match: *", script)
        self.assertIn("If-Match:", script)
        appcast_body = script.split("upload_appcast() {", 1)[1].split("\n}\n", 1)[0]
        self.assertNotIn('s3 cp "$appcast_path"', appcast_body)
    def test_remote_tag_is_verified_on_the_canonical_public_repository(self):
        script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        config = json.loads((ROOT / "release/macos.json").read_text())
        canonical = "https://github.com/pxlview/pixelview-desktop"
        self.assertEqual(config.get("source_repository"), canonical)
        self.assertIn('git ls-remote --tags "$source_repository"', script)
        verify_remote_body = script.split("verify_remote_tag() {", 1)[1].split("\n}\n", 1)[0]
        self.assertNotIn("ls-remote --tags origin", verify_remote_body)

    def test_release_entrypoint_uses_1password_for_r2_and_notarization(self):
        wrapper_path = ROOT / "release/pixelview-macos.sh"
        config_path = ROOT / "release/macos.env"
        r2_path = ROOT / "release/macos-r2.1password.env"
        notary_path = ROOT / "release/macos-notary.1password.env"
        for required in (wrapper_path, config_path, r2_path, notary_path):
            self.assertTrue(required.is_file())
        wrapper = wrapper_path.read_text()
        config_file = config_path.read_text()
        r2_file = r2_path.read_text()
        notary_file = notary_path.read_text()
        self.assertIn("op run", wrapper)
        self.assertIn("--env-file", wrapper)
        self.assertIn("cmake/macos/pixelview-release.sh", wrapper)
        for reference in (
            "op://pixelview-prod/pixelview-desktop-releases-r2-bucket/endpoint",
            "op://pixelview-prod/pixelview-desktop-releases-r2-bucket/access_key_id",
            "op://pixelview-prod/pixelview-desktop-releases-r2-bucket/secret_access_key",
        ):
            self.assertIn(reference, r2_file)
            self.assertNotIn(reference, notary_file)
        for reference in (
            "op://pixelview-prod/pixelview-desktop-notarization/key_id",
            "op://pixelview-prod/pixelview-desktop-notarization/issuer_id",
            "op://pixelview-prod/pixelview-desktop-notarization/private_key",
        ):
            self.assertIn(reference, notary_file)
            self.assertNotIn(reference, r2_file)
        self.assertIn("PIXELVIEW_R2_BUCKET=pixelview-desktop-releases", config_file)
        self.assertIn('PIXELVIEW_CODESIGN_IDENTITY="Developer ID Application: Cinecode OU (MA47F3M8W9)"', config_file)
        self.assertIn("PIXELVIEW_CODESIGN_TEAM=MA47F3M8W9", config_file)
        prepare_case = wrapper.split("--prepare)", 1)[1].split(";;", 1)[0]
        publish_case = wrapper.split("--publish)", 1)[1].split(";;", 1)[0]
        self.assertIn('notary_file="$root/release/macos-notary.1password.env"', wrapper)
        self.assertIn('r2_file="$root/release/macos-r2.1password.env"', wrapper)
        self.assertIn('"$notary_file"', prepare_case)
        self.assertNotIn('"$r2_file"', prepare_case)
        self.assertIn('"$r2_file"', publish_case)
        self.assertNotIn('"$notary_file"', publish_case)
        release_script = (ROOT / "cmake/macos/pixelview-release.sh").read_text()
        for variable in (
            "PIXELVIEW_NOTARY_KEY_ID",
            "PIXELVIEW_NOTARY_ISSUER_ID",
            "PIXELVIEW_NOTARY_PRIVATE_KEY",
        ):
            self.assertIn(variable, release_script)
        self.assertIn('--key "$notary_key_file"', release_script)
        self.assertIn('--key-id "$notary_key_id"', release_script)
        self.assertIn('--issuer "$notary_issuer_id"', release_script)
        self.assertNotIn("secret_access_key=", wrapper)

    def test_obs_readme_is_unchanged_and_release_flow_is_top_level(self):
        self.assertEqual(
            hashlib.sha256((ROOT / "README.rst").read_bytes()).hexdigest(),
            "be2dff0124620dcd9d8d83ef9084b12d1e61e896d3295232501cb3ebd3f6a318",
        )
        guide_path = ROOT / "PIXELVIEW_RELEASE.md"
        self.assertTrue(guide_path.is_file())
        guide = guide_path.read_text()
        for step in (
            "Create and push `v0.0.1`",
            "Create the GitHub release",
            "--prepare",
            "clean/quarantined Mac",
            "--publish",
            "Sparkle replacement and relaunch",
        ):
            self.assertIn(step, guide)
        ignored = subprocess.run(
            ["git", "check-ignore", "-q", "PIXELVIEW_RELEASE.md"],
            cwd=ROOT,
            check=False,
        )
        self.assertNotEqual(ignored.returncode, 0)


class PreparedReleaseValidation(unittest.TestCase):
    def _load_validator(self):
        path = ROOT / "cmake/macos/pixelview_release_validate.py"
        spec = importlib.util.spec_from_file_location("pixelview_release_validate", path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def _fixture(self, root):
        release_dir = root / "releases/0.0.1-1"
        release_dir.mkdir(parents=True)
        dmg_name = "Pixelview-Desktop-0.0.1-build1-arm64.dmg"
        dmg = release_dir / dmg_name
        dmg.write_bytes(b"signed-notarized-dmg-fixture")
        digest = hashlib.sha256(dmg.read_bytes()).hexdigest()
        (release_dir / f"{dmg_name}.sha256").write_text(f"{digest}  {dmg_name}\n")
        expected = {
            "product": "Pixelview Desktop",
            "version": "0.0.1",
            "build_number": 1,
            "release_id": "0.0.1-1",
            "architecture": "arm64",
            "source_commit": "a" * 40,
            "source_tag": "v0.0.1",
            "source_url": "https://github.com/pxlview/pixelview-desktop/tree/v0.0.1",
            "obs_base_version": "32.2.1",
            "obs_base_describe": "32.2.1-66-g6b3e55072",
            "obs_base_commit": "6b3e550729f125b6c5b3767df88c08f5aef9d264",
            "apple_team_id": "MA47F3M8W9",
            "artifact": dmg_name,
            "sha256": digest,
        }
        compliance = {}
        for role, suffix, data in (
            ('sources', '-sources.tar.gz', b'source-archive-fixture'),
            ('notices', '-NOTICES.txt', b'fixture notices'),
            ('inventory', '-source-inventory.json', json.dumps({
                'source_commit': expected['source_commit'], 'source_tag': expected['source_tag'],
                'release_id': expected['release_id'],
                'inventory': {'review': {'status': 'approved', 'blockers': [], 'evidence': {'path': 'test-only'}}},
            }).encode()),
        ):
            name = f"Pixelview-Desktop-{expected['release_id']}{suffix}"
            (release_dir / name).write_bytes(data)
            compliance[role] = {'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                                'url': f"https://downloads.pixelview.io/desktop/macos/releases/{expected['release_id']}/{name}"}
        expected['compliance'] = compliance
        (release_dir / "release-manifest.json").write_text(json.dumps(expected))
        notes_name = "Pixelview-Desktop-0.0.1-build1-arm64.html"
        (release_dir / notes_name).write_text("<p>notes</p>")
        appcast = root / "appcast-arm64.xml"
        appcast.write_text(
            '<?xml version="1.0"?><rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"><channel><item>'
            '<sparkle:version>1</sparkle:version><sparkle:shortVersionString>0.0.1</sparkle:shortVersionString>'
            '<sparkle:releaseNotesLink sparkle:edSignature="notes-fixture" sparkle:length="12">'
            'https://downloads.pixelview.io/desktop/macos/releases/0.0.1-1/'
            'Pixelview-Desktop-0.0.1-build1-arm64.html</sparkle:releaseNotesLink>'
            '<enclosure url="https://downloads.pixelview.io/desktop/macos/releases/0.0.1-1/'
            f'{dmg_name}" length="{dmg.stat().st_size}" type="application/octet-stream" '
            'sparkle:edSignature="fixture" />'
            '</item></channel><!-- sparkle:edSignature="feed-fixture" --></rss>'
        )
        return release_dir, appcast, expected

    def test_missing_corresponding_source_is_rejected(self):
        validator = self._load_validator()
        with tempfile.TemporaryDirectory() as temp:
            release_dir, appcast, expected = self._fixture(pathlib.Path(temp))
            del expected['compliance']
            (release_dir / 'release-manifest.json').write_text(json.dumps(expected))
            with self.assertRaisesRegex(ValueError, 'compliance'):
                validator.validate_prepared_release(release_dir, appcast, expected, 'https://downloads.pixelview.io/desktop/macos')

    def test_compliance_artifacts_fail_closed_on_missing_tampered_or_unsafe_inputs(self):
        validator = self._load_validator()
        for case in ('missing', 'tampered', 'symlink', 'size', 'url', 'name', 'review'):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temp:
                release_dir, appcast, expected = self._fixture(pathlib.Path(temp))
                record = expected['compliance']['sources']
                path = release_dir / record['name']
                if case == 'missing':
                    path.unlink()
                elif case == 'tampered':
                    path.write_bytes(b'x' * record['size'])
                elif case == 'symlink':
                    target = pathlib.Path(temp) / 'outside'
                    path.rename(target)
                    path.symlink_to(target)
                elif case == 'size':
                    record['size'] += 1
                elif case == 'url':
                    record['url'] = 'https://example.org/moving-source.tar.gz'
                elif case == 'name':
                    record['name'] = '../outside.tar.gz'
                elif case == 'review':
                    record = expected['compliance']['inventory']
                    path = release_dir / record['name']
                    resolved = json.loads(path.read_text())
                    resolved['inventory']['review']['status'] = 'blocked'
                    path.write_text(json.dumps(resolved))
                    record.update(size=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                (release_dir / 'release-manifest.json').write_text(json.dumps(expected))
                with self.assertRaisesRegex(ValueError, 'compliance'):
                    validator.validate_prepared_release(release_dir, appcast, expected, 'https://downloads.pixelview.io/desktop/macos')

    def test_valid_prepared_release_metadata_is_accepted(self):
        validator = self._load_validator()
        with tempfile.TemporaryDirectory() as temp:
            release_dir, appcast, expected = self._fixture(pathlib.Path(temp))
            try:
                validator.validate_prepared_release(
                    release_dir,
                    appcast,
                    expected,
                    "https://downloads.pixelview.io/desktop/macos",
                )
            except ValueError as error:
                self.fail(f"valid Sparkle appcast was rejected: {error}")
    def test_modified_dmg_is_rejected(self):
        validator = self._load_validator()
        with tempfile.TemporaryDirectory() as temp:
            release_dir, appcast, expected = self._fixture(pathlib.Path(temp))
            (release_dir / expected["artifact"]).write_bytes(b"modified")
            with self.assertRaisesRegex(ValueError, "checksum"):
                validator.validate_prepared_release(
                    release_dir,
                    appcast,
                    expected,
                    "https://downloads.pixelview.io/desktop/macos",
                )
    def test_manifest_provenance_mismatch_is_rejected(self):
        validator = self._load_validator()
        with tempfile.TemporaryDirectory() as temp:
            release_dir, appcast, expected = self._fixture(pathlib.Path(temp))
            manifest_path = release_dir / "release-manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["source_commit"] = "b" * 40
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "source_commit"):
                validator.validate_prepared_release(
                    release_dir,
                    appcast,
                    expected,
                    "https://downloads.pixelview.io/desktop/macos",
                )
    def test_appcast_pointing_at_a_different_artifact_is_rejected(self):
        validator = self._load_validator()
        with tempfile.TemporaryDirectory() as temp:
            release_dir, appcast, expected = self._fixture(pathlib.Path(temp))
            appcast.write_text(appcast.read_text().replace(expected["artifact"], "other.dmg"))
            with self.assertRaisesRegex(ValueError, "appcast.*URL"):
                validator.validate_prepared_release(
                    release_dir,
                    appcast,
                    expected,
                    "https://downloads.pixelview.io/desktop/macos",
                )
    def test_validator_cli_reports_success_for_a_valid_fixture(self):
        with tempfile.TemporaryDirectory() as temp:
            release_dir, appcast, expected = self._fixture(pathlib.Path(temp))
            result = subprocess.run(
                [
                    "python3",
                    str(ROOT / "cmake/macos/pixelview_release_validate.py"),
                    str(release_dir),
                    str(appcast),
                    "https://downloads.pixelview.io/desktop/macos",
                    json.dumps(expected),
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "prepared release metadata validated")
    def test_build_progression_validator_exists(self):
        validator = self._load_validator()
        self.assertTrue(
            hasattr(validator, "validate_build_progression"),
            "release validation must compare against the authenticated current appcast",
        )
    def test_non_increasing_build_is_rejected(self):
        validator = self._load_validator()
        with tempfile.TemporaryDirectory() as temp:
            _, staged, _ = self._fixture(pathlib.Path(temp))
            staged_text = staged.read_text()
            current = pathlib.Path(temp) / "current.xml"
            current.write_text(staged_text.replace("feed-fixture", "published-feed"))
            staged.write_text(
                staged_text.replace(
                    "<sparkle:shortVersionString>0.0.1</sparkle:shortVersionString>",
                    "<sparkle:shortVersionString>0.0.2</sparkle:shortVersionString>",
                )
            )
            with self.assertRaisesRegex(ValueError, "newer"):
                validator.validate_build_progression(current, staged, "0.0.2", 1)
    def test_build_progression_cli_rejects_equal_different_feed(self):
        with tempfile.TemporaryDirectory() as temp:
            _, staged, _ = self._fixture(pathlib.Path(temp))
            staged_text = staged.read_text()
            current = pathlib.Path(temp) / "current.xml"
            current.write_text(staged_text.replace("feed-fixture", "published-feed"))
            staged.write_text(
                staged_text.replace(
                    "<sparkle:shortVersionString>0.0.1</sparkle:shortVersionString>",
                    "<sparkle:shortVersionString>0.0.2</sparkle:shortVersionString>",
                )
            )
            result = subprocess.run(
                [
                    "python3",
                    str(ROOT / "cmake/macos/pixelview_release_validate.py"),
                    "--build-progression",
                    str(current),
                    str(staged),
                    "0.0.2",
                    "1",
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("newer", result.stderr)
    def test_modified_release_notes_are_rejected(self):
        validator = self._load_validator()
        with tempfile.TemporaryDirectory() as temp:
            release_dir, appcast, expected = self._fixture(pathlib.Path(temp))
            notes = release_dir / "Pixelview-Desktop-0.0.1-build1-arm64.html"
            notes.write_text(notes.read_text() + "tampered")
            with self.assertRaisesRegex(ValueError, "release notes length"):
                validator.validate_prepared_release(
                    release_dir,
                    appcast,
                    expected,
                    "https://downloads.pixelview.io/desktop/macos",
                )
    def test_same_marketing_version_cannot_be_republished_with_new_build(self):
        validator = self._load_validator()
        with tempfile.TemporaryDirectory() as temp:
            _, current, _ = self._fixture(pathlib.Path(temp))
            staged = pathlib.Path(temp) / "build2.xml"
            staged.write_text(
                current.read_text().replace(
                    "<sparkle:version>1</sparkle:version>",
                    "<sparkle:version>2</sparkle:version>",
                )
            )
            with self.assertRaisesRegex(ValueError, "marketing version"):
                validator.validate_build_progression(current, staged, "0.0.1", 2)


class PixelviewCIAndTelemetry(unittest.TestCase):
    def test_public_ci_cannot_sign_publish_or_call_obs_release_automation(self):
        workflows = sorted(path.name for path in (ROOT / ".github/workflows").glob("*.yaml"))
        self.assertEqual(workflows, ["ci.yaml"])
        ci = (ROOT / ".github/workflows/ci.yaml").read_text()
        self.assertIn("permissions:\n  contents: read", ci)
        self.assertIn("test_pixelview_release.py", ci)
        for forbidden in (
            "secrets.",
            "notary",
            "codesign",
            "publish",
            "upload-artifact",
            "obsproject",
        ):
            self.assertNotIn(forbidden, ci.lower())

        for dangling in ("build-macos", "package-macos", "build-ubuntu", "package-ubuntu"):
            path = ROOT / ".github/scripts" / dangling
            self.assertFalse(path.exists() or path.is_symlink(), str(path))

        for retired in (
            ROOT / ".github/scripts/.package.zsh",
            ROOT / ".github/actions/sparkle-appcast/action.yaml",
            ROOT / ".github/actions/package-obs/action.yaml",
        ):
            self.assertFalse(retired.exists(), str(retired))

        build = (ROOT / "cmake/macos/pixelview-build.sh").read_text()
        self.assertIn("-DENABLE_BROWSER=OFF", build)
        self.assertIn("-DENABLE_WHATSNEW=OFF", build)

        telemetry_sources = "\n".join(
            (ROOT / path).read_text()
            for path in (
                "frontend/utility/WhatsNewInfoThread.cpp",
                "frontend/utility/CrashHandler.cpp",
                "frontend/widgets/OBSBasic_MainControls.cpp",
                "frontend/dialogs/OBSAbout.cpp",
                "frontend/dialogs/LogUploadDialog.cpp",
                "frontend/oauth/AuthListener.cpp",
            )
        )
        for forbidden in (
            "X-OBS2-GUID",
            "obsproject.com/logs/upload",
            "obsproject.com/patreon/about-box.json",
            "obsproject.com/tools/analyzer",
            "obsproject.com/assets/images/new_icon_small-r.png",
        ):
            self.assertNotIn(forbidden, telemetry_sources)


if __name__ == "__main__":
    unittest.main()
