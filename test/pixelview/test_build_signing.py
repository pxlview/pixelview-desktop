"""Execute the real build helper with a recording CMake boundary (no signing)."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildSigningTests(unittest.TestCase):
    def run_helper(self, **settings):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            cmake = tmp / "cmake"
            cmake.write_text(f'#!{sys.executable}\n' + 'import json, os, sys\nwith open(os.environ["CMAKE_CALLS"], "a") as f:\n    f.write(json.dumps(sys.argv[1:]) + "\\n")\n')
            cmake.chmod(0o755)
            # Packaging has its own native/closure tests. This shell-policy fixture
            # must not download/rebuild the SDK, rewrite the shared staged runtime,
            # or depend on Homebrew. Fail closed on future unlisted Python helpers.
            python = tmp / "python3"
            python.write_text(f'#!{sys.executable}\nimport os, sys\n'
                              'if len(sys.argv) > 1 and sys.argv[1].endswith(("fetch-gstreamer.py", "build-rswebrtc.py")):\n'
                              '    assert len(sys.argv) == 2\n'
                              '    sys.exit(0)\n'
                              'if len(sys.argv) > 1 and sys.argv[1].endswith("bundle-runtime.py"):\n'
                              '    assert sys.argv[2:] == ["stage", ".deps/pixelview-gstreamer"]\n'
                              '    sys.exit(0)\n'
                              'if len(sys.argv) > 1 and sys.argv[1] != "-":\n'
                              '    sys.exit("Unstubbed build script: " + sys.argv[1])\n'
                              f'os.execv({sys.executable!r}, [{sys.executable!r}, *sys.argv[1:]])\n')
            python.chmod(0o755)
            env = {k: v for k, v in os.environ.items() if not k.startswith("PIXELVIEW_")}
            env.update(PATH=f'{tmp}:{env["PATH"]}', CMAKE_CALLS=str(tmp / "calls"))
            env.update(PIXELVIEW_LOCAL_SIGNING_VERIFIED="1", PIXELVIEW_BUILD_DIR=str(ROOT / "build_macos"))
            env.update(settings)
            result = subprocess.run(["bash", str(ROOT / "cmake/macos/pixelview-build.sh")], env=env, capture_output=True, text=True)
            calls = [json.loads(line) for line in (tmp / "calls").read_text().splitlines()] if (tmp / "calls").exists() else []
            return result, calls

    def test_explicit_identity_team_use_canonical_output(self):
        identity = "Developer ID Application: Example Company (ABCDEFGHIJ)"
        result, calls = self.run_helper(PIXELVIEW_CODESIGN_IDENTITY=identity, PIXELVIEW_CODESIGN_TEAM="ABCDEFGHIJ")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"-DOBS_CODESIGN_IDENTITY={identity}", calls[0])
        self.assertIn("-DOBS_CODESIGN_TEAM=ABCDEFGHIJ", calls[0])
        self.assertEqual(calls[0][calls[0].index("-B") + 1], str(ROOT / "build_macos"))
        self.assertEqual(calls[1][1], str(ROOT / "build_macos"))

    def test_rejects_partial_or_non_developer_id_signing(self):
        for settings in (
            {"PIXELVIEW_CODESIGN_TEAM": "ABCDEFGHIJ"},
            {"PIXELVIEW_CODESIGN_IDENTITY": "Developer ID Application: Example"},
            {"PIXELVIEW_CODESIGN_IDENTITY": "Apple Development", "PIXELVIEW_CODESIGN_TEAM": "ABCDEFGHIJ"},
            {"PIXELVIEW_CODESIGN_IDENTITY": "A" * 40, "PIXELVIEW_CODESIGN_TEAM": "invalid"},
        ):
            with self.subTest(settings=settings):
                result, calls = self.run_helper(**settings)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(calls, [])

    def test_native_boundary_rejects_adhoc_even_with_inherited_preset(self):
        result, calls = self.run_helper(CODESIGN_IDENT="unexpected", CODESIGN_TEAM="ABCDEFGHIJ", PROVISIONING_PROFILE="unexpected")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_local_custom_directory_rejected(self):
        result, calls = self.run_helper(PIXELVIEW_CODESIGN_IDENTITY="A" * 40, PIXELVIEW_CODESIGN_TEAM="ABCDEFGHIJ", PIXELVIEW_BUILD_DIR="build signing space")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_release_custom_directory_still_quoted_and_explicit(self):
        result, calls = self.run_helper(PIXELVIEW_CODESIGN_IDENTITY="A" * 40, PIXELVIEW_CODESIGN_TEAM="ABCDEFGHIJ", PIXELVIEW_BUILD_DIR="build signing space", PIXELVIEW_RELEASE_BUILD="ON", PIXELVIEW_BUILD_CONFIG="Release", PIXELVIEW_SPARKLE_APPCAST_URL="https://example.com/appcast.xml", PIXELVIEW_SPARKLE_PUBLIC_KEY="test-public-key")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls[0][calls[0].index("-B") + 1], "build signing space")
        self.assertEqual(calls[1][1], "build signing space")
        self.assertIn("-DPIXELVIEW_RELEASE_BUILD=ON", calls[0])
        self.assertIn("-DOBS_CODESIGN_IDENTITY=" + "A" * 40, calls[0])

    def test_normal_entrypoint_dispatches_signed_wrapper(self):
        result, calls = self.run_helper(PIXELVIEW_LOCAL_SIGNING_VERIFIED="")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unstubbed build script: cmake/macos/pixelview-signed-development.py", result.stderr)
        self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main()
