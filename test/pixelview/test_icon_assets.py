"""Binary asset regressions; standard-library PNG header test plus generator integration contract."""
from pathlib import Path
import struct
import unittest
ROOT = Path(__file__).resolve().parents[2]

class IconAssets(unittest.TestCase):
    def test_app_artwork_is_reduced_exactly_fifteen_percent(self):
        from PIL import Image, ImageChops
        images = ROOT / 'frontend/data/images'
        play = Image.open(images / 'pixelview-play.png').convert('RGBA')
        # The previous longest dimension was 820; 820 * 85 / 100 = 697.
        scale = 697 / max(play.size)
        artwork = play.resize((round(play.width * scale), round(play.height * scale)),
                              Image.Resampling.LANCZOS)
        self.assertEqual(max(artwork.size), 697)
        expected = Image.new('RGBA', (1024, 1024), (29, 31, 38, 255))
        expected.alpha_composite(artwork, ((1024 - artwork.width) // 2,
                                          (1024 - artwork.height) // 2))
        actual = Image.open(images / 'pixelview-app.png').convert('RGBA')
        self.assertIsNone(ImageChops.difference(actual, expected).convert('RGB').getbbox())
        from PIL.IcoImagePlugin import IcoImageFile
        with Image.open(images / 'pixelview-app.icns') as icns:
            self.assertEqual(icns.size, (1024, 1024))
            self.assertIsNone(ImageChops.difference(icns.convert('RGBA'), expected).convert('RGB').getbbox())
        with IcoImageFile(images / 'pixelview-app.ico') as ico:
            self.assertEqual(ico.ico.sizes(), {(n, n) for n in (16, 24, 32, 48, 64, 128, 256)})
            for size in ico.ico.sizes():
                frame = ico.ico.getimage(size).convert('RGBA')
                reference = expected.resize(size, Image.Resampling.LANCZOS)
                self.assertIsNone(ImageChops.difference(frame, reference).convert('RGB').getbbox())

    def test_playmark_is_explicitly_upscaled_not_thumbnail_only(self):
        source = (ROOT / 'test/pixelview/generate_brand_icons.py').read_text()
        self.assertIn('artwork = play.resize(', source,
                      'Pillow thumbnail never enlarges the 319x411 source; it leaves the Dock mark tiny')
        self.assertNotIn('artwork.thumbnail(', source)

    def test_app_background_is_sidebar_gray_but_tray_stays_transparent(self):
        import zlib
        def first_pixel(name):
            data = (ROOT / 'frontend/data/images' / name).read_bytes()
            self.assertEqual(data[24:26], bytes([8, 6]))  # RGBA8
            offset, payload = 8, b''
            while offset < len(data):
                length = struct.unpack('>I', data[offset:offset + 4])[0]
                if data[offset + 4:offset + 8] == b'IDAT':
                    payload += data[offset + 8:offset + 8 + length]
                offset += length + 12
            raw = zlib.decompress(payload)
            # First pixel has zero left/above predictors for every PNG filter.
            return tuple(raw[1:5])
        self.assertEqual(first_pixel('pixelview-app.png'), (29, 31, 38, 255))
        self.assertEqual(first_pixel('pixelview-tray-macos.png')[3], 0)

    def test_icon_dimensions_and_distinct_template_states(self):
        images = ROOT / 'frontend/data/images'
        for name, size in [('pixelview-app', 1024), ('pixelview-tray-macos', 128),
                           ('pixelview-tray-active-macos', 128), ('pixelview-tray-paused-macos', 128)]:
            data = (images / (name + '.png')).read_bytes()
            self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
            self.assertEqual(struct.unpack('>II', data[16:24]), (size, size))
        self.assertEqual(len({(images / (n + '.png')).read_bytes() for n in
                              ['pixelview-tray-macos', 'pixelview-tray-active-macos', 'pixelview-tray-paused-macos']}), 3)
