"""Offline resource contracts; no build, GUI or network required."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class PixelviewLicenseResources(unittest.TestCase):
    def test_company_contact_is_separate_from_upstream_license(self):
        source = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        notice = source.split('void OBSBasic::ShowPixelviewLicense()', 1)[1].split('notice->setTextFormat', 1)[0]
        for text in ('Pixelview is a brand of Cinecode OÜ.', 'Ahtri 12',
                     '10151 Tallinn', 'Estonia', 'pixelview.io'):
            self.assertIn(text, notice)
            self.assertIn(text, (ROOT / 'PIXELVIEW.md').read_text())
        self.assertNotIn('©', notice)
        self.assertIn('OBS Studio is copyright its respective OBS Project contributors', notice)

    def test_bundled_copying_is_the_complete_unmodified_root_license(self):
        bundled = ROOT / 'frontend/data/license/COPYING'
        self.assertTrue(bundled.is_file(), 'Bundle the full root COPYING, including its appendix')
        self.assertEqual(bundled.read_bytes(), (ROOT / 'COPYING').read_bytes())
        self.assertIn(b'How to Apply These Terms to Your New Programs', bundled.read_bytes())
        self.assertTrue((ROOT / 'frontend/data/license/gplv2.txt').is_file())
        self.assertTrue((ROOT / 'AUTHORS').is_file())
        helpers = (ROOT / 'cmake/macos/helpers.cmake').read_text()
        packaging = helpers.split('function(target_install_resources target)', 1)[1].split('endfunction()', 1)[0]
        self.assertIn('file(GLOB_RECURSE data_files "${CMAKE_CURRENT_SOURCE_DIR}/data/*")', packaging)
        self.assertIn('MACOSX_PACKAGE_LOCATION "Resources/${relative_path}"', packaging)


if __name__ == '__main__':
    unittest.main()
