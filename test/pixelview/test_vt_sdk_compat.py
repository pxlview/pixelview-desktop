"""Compile the real VT compatibility path at the production deployment target."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == "darwin", "requires macOS VideoToolbox SDK")
class VideoToolboxSDKCompatibility(unittest.TestCase):
    def test_spatial_aq_with_macos_13_deployment_target(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = pathlib.Path(temp) / "test-spatial-aq"
            result = subprocess.run([
                "xcrun", "clang", "-mmacosx-version-min=13.0", "-std=c11",
                "-Wall", "-Wextra", "-Werror", "-Werror=unguarded-availability",
                str(ROOT / "plugins/mac-videotoolbox/tests/test-spatial-aq.c"),
                "-framework", "VideoToolbox", "-framework", "CoreFoundation",
                "-o", str(binary),
            ], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("PASS:", result.stdout)


if __name__ == "__main__":
    unittest.main()
