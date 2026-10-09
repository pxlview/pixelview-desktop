"""The app loads only the modules in its own bundle, never third-party OBS plugins."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class BundledPluginsOnly(unittest.TestCase):
    def test_frontend_adds_no_module_search_paths(self):
        for path in (ROOT / 'frontend').rglob('*'):
            if path.suffix in ('.c', '.cpp', '.h', '.hpp', '.m', '.mm', '.inc'):
                text = path.read_text(errors='replace')
                self.assertFalse('obs_add_module_path' in text, path)
                self.assertFalse('getenv("OBS_PLUGINS' in text, path)
                self.assertIsNone(re.search(r'"obs-studio/plugins', text), path)

    def test_macos_libobs_searches_only_the_app_bundle(self):
        cocoa = (ROOT / 'libobs/obs-cocoa.m').read_text()
        body = cocoa.split('void add_default_module_paths(void)', 1)[1].split('\n}', 1)[0]
        self.assertEqual(body.count('obs_add_module_path('), 1)
        self.assertIn('builtInPlugInsURL', body)


if __name__ == '__main__':
    unittest.main()
