"""Offline contracts for the signed-development build wrapper (no key access)."""
import importlib.util
from pathlib import Path
import plistlib
import subprocess
import tempfile
import os
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("signed_development", Path(__file__).with_name("pixelview-signed-development.py"))
assert SPEC is not None and SPEC.loader is not None
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)
TEAM = "MA47F3M8W9"
NAME = f"Developer ID Application: Cinecode OU ({TEAM})"
SHA = BUILD.DEFAULT_IDENTITY


class SignedDevelopment(unittest.TestCase):
    def test_exact_team_identity(self):
        self.assertEqual(BUILD.resolve_identity(f' 1) {SHA} "{NAME}"', TEAM), (SHA, NAME))

    def test_missing_wrong_team_and_adhoc_fail(self):
        for text in ("0 valid identities found", f'1) {SHA} "Apple Development: User ({TEAM})"', f'1) {SHA} "Developer ID Application: Other (ZZZZZZZZZZ)"'):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                BUILD.resolve_identity(text, TEAM)

    def test_ambiguous_fails_but_fingerprint_selects(self):
        text = f'1) {SHA} "{NAME}"\n2) {"B" * 40} "{NAME}"'
        with self.assertRaises(RuntimeError):
            BUILD.resolve_identity(text, TEAM)
        self.assertEqual(BUILD.resolve_identity(text, TEAM, SHA.lower()), (SHA, NAME))

    def test_release_environment_cannot_leak(self):
        with patch.dict("os.environ", {"PIXELVIEW_RELEASE_BUILD": "ON", "PIXELVIEW_BUILD_DIR": "build_macos", "PIXELVIEW_SPARKLE_APPCAST_URL": "https://example.com", "PIXELVIEW_ENABLE_UNIVERSAL_LINKS": "ON"}):
            env = BUILD.build_environment(SHA, TEAM, {"commit": "c" * 40})
        self.assertEqual(env["PIXELVIEW_RELEASE_BUILD"], "OFF")
        self.assertEqual(env["PIXELVIEW_BUILD_DIR"], str(BUILD.BUILD))
        self.assertEqual(env["PIXELVIEW_CODESIGN_IDENTITY"], SHA)
        self.assertEqual(env["PIXELVIEW_CODESIGN_TEAM"], TEAM)
        self.assertEqual(env["PIXELVIEW_BUILD_CONFIG"], "RelWithDebInfo")
        self.assertEqual(env["PIXELVIEW_SPARKLE_APPCAST_URL"], "")
        self.assertEqual(env["PIXELVIEW_ENABLE_UNIVERSAL_LINKS"], "OFF")

    def test_keychain_interactive_warns_unattended_fails(self):
        for result, status in [(0, 0), (-1, 0), (0, 2)]:
            BUILD.keychain_policy(result, status, False)
            with self.assertRaises(RuntimeError):
                BUILD.keychain_policy(result, status, True)
        BUILD.keychain_policy(0, 1, True)

    def test_canonical_defaults_and_normal_home(self):
        self.assertEqual(BUILD.APP, BUILD.ROOT / "build_macos/frontend/RelWithDebInfo/Pixelview Desktop.app")
        with patch.dict(os.environ, {"CFFIXED_USER_HOME": "/private/tmp/old"}):
            env = BUILD.build_environment(SHA, TEAM, {"commit": "abc"})
        self.assertNotIn("CFFIXED_USER_HOME", env)
        self.assertEqual(env["HOME"], BUILD.pwd.getpwuid(os.getuid()).pw_dir)
        with patch.dict(os.environ, {"HOME": "/private/tmp/old"}), self.assertRaises(RuntimeError):
            BUILD.normal_environment()

    def test_running_bundle_refused(self):
        with patch.object(BUILD, "output", return_value=f"123 {BUILD.APP}/Contents/MacOS/Pixelview Desktop"), self.assertRaisesRegex(RuntimeError, "in use"):
            BUILD.check_running()

    def test_signing_failure_has_no_fallback_or_verification(self):
        with patch("sys.argv", ["build", "--allow-dirty", "--source-settled"]), patch.object(BUILD.sys, "platform", "darwin"), patch.object(BUILD, "output", return_value=f'1) {SHA} "{NAME}"'), patch.object(BUILD, "check_keychain"), patch.object(BUILD, "source_state", return_value={"commit": "abc", "status": "dirty"}), patch.object(BUILD.shutil, "disk_usage") as disk, patch.object(BUILD, "run", side_effect=subprocess.CalledProcessError(1, "codesign")) as native, patch.object(BUILD, "verify") as verify, patch.object(BUILD, "invalidate_report") as invalidate:
            disk.return_value.free = 8 * 1024**3
            with self.assertRaises(subprocess.CalledProcessError):
                BUILD.main()
            invalidate.assert_called_once()
            self.assertEqual(native.call_count, 1)
            verify.assert_not_called()

    def test_interactive_locked_metadata_reaches_native_signer(self):
        def locked(**kwargs):
            BUILD.keychain_policy(0, 2, kwargs["unattended"])
        with patch("sys.argv", ["build", "--allow-dirty", "--source-settled"]), patch.object(BUILD.sys, "platform", "darwin"), patch.object(BUILD, "output", return_value=f'1) {SHA} "{NAME}"'), patch.object(BUILD, "check_keychain", side_effect=locked), patch.object(BUILD, "source_state", return_value={"commit": "abc", "status": "dirty"}), patch.object(BUILD.shutil, "disk_usage") as disk, patch.object(BUILD, "run", side_effect=subprocess.CalledProcessError(1, "codesign")) as native, patch.object(BUILD, "invalidate_report"):
            disk.return_value.free = 8 * 1024**3
            with self.assertRaises(subprocess.CalledProcessError):
                BUILD.main()
            self.assertEqual(native.call_count, 1)
            self.assertEqual(native.call_args.kwargs["env"]["PIXELVIEW_CODESIGN_IDENTITY"], SHA)

    def test_launcher_path_and_filtered_migration(self):
        spec = importlib.util.spec_from_file_location("launch", BUILD.ROOT / "cmake/macos/pixelview-launch.py")
        assert spec is not None and spec.loader is not None
        launch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(launch)
        self.assertEqual(launch.launch_command(Path("/tmp/config")), [str(BUILD.APP / "Contents/MacOS/Pixelview Desktop"), "--multi", "--app-config-dir", "/tmp/config"])
        with tempfile.TemporaryDirectory() as tmp:
            source, dest = Path(tmp) / "old", Path(tmp) / "new"
            profile = source / "obs-studio/basic/profiles/test/basic.ini"
            profile.parent.mkdir(parents=True)
            original = "[Video]\nBaseCX=1920\nPassword=do-not-copy\n[Pairing]\nToken=secret\n"
            profile.write_text(original)
            launch.migrate_settings(source, dest)
            copied = (dest / profile.relative_to(source)).read_text()
            self.assertIn("BaseCX = 1920", copied)
            self.assertNotIn("Password", copied)
            self.assertNotIn("secret", copied)
            self.assertNotIn("do-not-copy", (dest / "migration-settings-backup.json").read_text())
            self.assertEqual(profile.read_text(), original)
            with self.assertRaises(RuntimeError):
                launch.migrate_settings(source, dest)

    def test_existing_entitlements_are_parseable(self):
        with (BUILD.ROOT / "frontend/cmake/macos/entitlements.plist").open("rb") as handle:
            entitlements = plistlib.load(handle, fmt=plistlib.FMT_XML)
        self.assertTrue(entitlements["com.apple.security.device.camera"])
        self.assertNotIn("com.apple.security.get-task-allow", entitlements)

    def test_native_pipeline_preserves_explicit_signing(self):
        helper = (BUILD.ROOT / "cmake/macos/pixelview-build.sh").read_text()
        self.assertIn('"-DOBS_CODESIGN_IDENTITY=$identity" "-DOBS_CODESIGN_TEAM=$team"', helper)
        release = (BUILD.ROOT / "cmake/macos/pixelview-release.sh").read_text()
        self.assertIn('[[ -z "$(git status --porcelain)" ]] || die "working tree must be clean"', release)
        self.assertIn('tag $tag must exist at HEAD', release)

    def test_locked_preflight_never_builds(self):
        with patch("sys.argv", ["signed-development", "--allow-dirty", "--source-settled", "--unattended"]), patch.object(BUILD.sys, "platform", "darwin"), patch.object(BUILD, "output", return_value=f'1) {SHA} "{NAME}"'), patch.object(BUILD, "check_keychain", side_effect=RuntimeError("locked")), patch.object(BUILD, "run") as native:
            with self.assertRaisesRegex(RuntimeError, "locked"):
                BUILD.main()
            native.assert_not_called()

    def test_metadata_check_only_never_builds(self):
        with patch("sys.argv", ["signed-development", "--check-only"]), patch.object(BUILD.sys, "platform", "darwin"), patch.object(BUILD, "output", return_value=f'1) {SHA} "{NAME}"'), patch.object(BUILD, "check_keychain"), patch.object(BUILD.shutil, "disk_usage") as disk, patch.object(BUILD, "run") as native:
            disk.return_value.free = 8 * 1024**3
            BUILD.main()
            native.assert_not_called()


if __name__ == "__main__":
    unittest.main()
