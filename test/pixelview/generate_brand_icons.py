"""Regenerate Pixelview icons from the authentic wordmark. Requires Pillow; iconutil on macOS."""
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / 'frontend/data/images'
BACKGROUND = (29, 31, 38, 255)  # #1D1F26, sampled from the native OBS sidebar.

# Apple's macOS app icon grid: an 824 px plate centred on the 1024 px canvas with
# continuous ("squircle") corners and a soft drop shadow. A full-bleed square looks
# oversized and boxy next to every other icon in the Dock and the Cmd+Tab switcher.
MAC_CANVAS, MAC_PLATE, MAC_SUPERSAMPLE = 1024, 824, 4
MAC_CORNER_EXPONENT = 5.0  # superellipse that tracks Apple's continuous corner closely
MAC_SHADOW_OFFSET, MAC_SHADOW_BLUR, MAC_SHADOW_ALPHA = 12, 14, 0.5

def macos_plate_mask():
    size = MAC_CANVAS * MAC_SUPERSAMPLE
    half = MAC_PLATE * MAC_SUPERSAMPLE / 2
    centre = size / 2
    steps = 720
    points = []
    for quadrant, (sx, sy) in enumerate(((1, 1), (-1, 1), (-1, -1), (1, -1))):
        for i in range(steps):
            t = i / steps if quadrant % 2 == 0 else 1 - i / steps
            x = (1 - t ** MAC_CORNER_EXPONENT) ** (1 / MAC_CORNER_EXPONENT)
            points.append((centre + sx * half * x, centre + sy * half * t))
    mask = Image.new('L', (size, size), 0)
    ImageDraw.Draw(mask).polygon(points, fill=255)
    return mask.resize((MAC_CANVAS, MAC_CANVAS), Image.Resampling.LANCZOS)

def macos_icon(play):
    mask = macos_plate_mask()
    icon = Image.new('RGBA', (MAC_CANVAS, MAC_CANVAS))
    shadow = Image.new('L', (MAC_CANVAS, MAC_CANVAS), 0)
    shadow.paste(mask.point(lambda v: round(v * MAC_SHADOW_ALPHA)), (0, MAC_SHADOW_OFFSET))
    shadow = shadow.filter(ImageFilter.GaussianBlur(MAC_SHADOW_BLUR))
    icon.paste(Image.new('RGBA', icon.size, (0, 0, 0, 255)), (0, 0), shadow)
    plate = Image.new('RGBA', icon.size, BACKGROUND)
    # Same artwork-to-plate proportion as the full-bleed icon (697 / 1024).
    scale = 697 * MAC_PLATE / MAC_CANVAS / max(play.size)
    artwork = play.resize((round(play.width * scale), round(play.height * scale)), Image.Resampling.LANCZOS)
    plate.alpha_composite(artwork, ((MAC_CANVAS - artwork.width) // 2, (MAC_CANVAS - artwork.height) // 2))
    icon.paste(plate, (0, 0), mask)
    return icon

def generate():
    wordmark = Image.open(IMAGES / 'pixelview-wordmark.png').convert('RGBA')
    alpha = wordmark.getchannel('A')
    occupied = [x for x in range(wordmark.width) if alpha.crop((x, 0, x + 1, wordmark.height)).getbbox()]
    start = occupied[0]
    end = start
    while end + 1 in occupied:
        end += 1
    play = wordmark.crop((start, 0, end + 1, wordmark.height))
    play = play.crop(play.getbbox())
    play.save(IMAGES / 'pixelview-play.png')
    foreground = Image.new('RGBA', (1024, 1024))
    scale = 820 / max(play.size)
    artwork = play.resize((round(play.width * scale), round(play.height * scale)), Image.Resampling.LANCZOS)
    foreground.alpha_composite(artwork, ((1024 - artwork.width) // 2, (1024 - artwork.height) // 2))
    # Reduce only the app artwork by 15%; retain the original tray geometry.
    app_scale = 697 / max(play.size)  # 820 * 0.85
    app_artwork = play.resize((round(play.width * app_scale), round(play.height * app_scale)), Image.Resampling.LANCZOS)
    app = Image.new('RGBA', (1024, 1024), BACKGROUND)
    app.alpha_composite(app_artwork, ((1024 - app_artwork.width) // 2, (1024 - app_artwork.height) // 2))
    app.save(IMAGES / 'pixelview-app.png')
    app.save(IMAGES / 'pixelview-app.ico', sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
    for state in ('', '-active', '-paused'):
        for mask in (False, True):
            icon = foreground.copy()
            if mask:
                silhouette = Image.new('RGBA', icon.size, (0, 0, 0, 255))
                silhouette.putalpha(icon.getchannel('A'))
                icon = silhouette
            draw = ImageDraw.Draw(icon)
            if state:
                # Transparent separation preserves badge shape in template rendering.
                draw.ellipse((650, 650, 1010, 1010), fill=(0, 0, 0, 0))
                ink = (0, 0, 0, 255) if mask else ((240, 64, 70, 255) if state == '-active' else (255, 192, 64, 255))
                if state == '-active':
                    draw.ellipse((700, 700, 960, 960), fill=ink)
                else:
                    draw.rounded_rectangle((700, 700, 800, 960), radius=20, fill=ink)
                    draw.rounded_rectangle((860, 700, 960, 960), radius=20, fill=ink)
            icon.resize((128, 128), Image.Resampling.LANCZOS).save(IMAGES / f'pixelview-tray{state}{"-macos" if mask else ""}.png')
    if sys.platform == 'darwin':
        mac = macos_icon(play)
        with tempfile.TemporaryDirectory() as tmp:
            iconset = Path(tmp) / 'Pixelview.iconset'
            iconset.mkdir()
            for n in (16, 32, 128, 256, 512):
                for scale in (1, 2):
                    mac.resize((n * scale, n * scale), Image.Resampling.LANCZOS).save(iconset / f'icon_{n}x{n}{"@2x" if scale == 2 else ""}.png')
            subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o', str(IMAGES / 'pixelview-app.icns')], check=True)
    print('Generated authentic play crop, square app PNG/ICO, six distinct tray states, and the Apple-grid macOS ICNS when available.')

if __name__ == '__main__':
    generate()
