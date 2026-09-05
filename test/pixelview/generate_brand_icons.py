"""Regenerate Pixelview icons from the authentic wordmark. Requires Pillow; iconutil on macOS."""
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / 'frontend/data/images'
BACKGROUND = (29, 31, 38, 255)  # #1D1F26, sampled from the native OBS sidebar.

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
        with tempfile.TemporaryDirectory() as tmp:
            iconset = Path(tmp) / 'Pixelview.iconset'
            iconset.mkdir()
            for n in (16, 32, 128, 256, 512):
                for scale in (1, 2):
                    app.resize((n * scale, n * scale), Image.Resampling.LANCZOS).save(iconset / f'icon_{n}x{n}{"@2x" if scale == 2 else ""}.png')
            subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o', str(IMAGES / 'pixelview-app.icns')], check=True)
    print('Generated authentic play crop, app PNG/ICO, six distinct tray states, and macOS ICNS when available.')

if __name__ == '__main__':
    generate()
