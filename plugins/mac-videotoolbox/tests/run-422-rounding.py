#!/usr/bin/env python3
"""Hardware HEVC Main 4:2:2 10 and Main 4:4:4 10: noisy 16-bit input must still encode exact ten-bit codes.

Encodes 128 flat patches with VideoToolbox (hardware required) at 40 Mbit/s and
decodes them with ffmpeg. Clean code << 6 input must come back exact; input
12/64 of a code below (the libobs float canvas noise) loses a code on some
patches unless it is rounded first, which is what encoder.c does. Needs ffmpeg
on PATH for the decode only.
"""
import array
from collections import Counter
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
W, H = 1920, 1080


def errors(work, name, offset, rounded, full=False):
    stream, raw = work / (name + '.h265'), work / (name + '.yuv')
    subprocess.run([str(work / 'encode'), str(offset), str(stream)] + (['round'] if rounded else []) +
                   (['444'] if full else []), check=True, capture_output=True, timeout=60)
    probe = subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                                     'stream=pix_fmt', '-of', 'csv=p=0', str(stream)], text=True).strip()
    # The stream itself must carry the sampling asked for (an unset profile silently encodes 4:2:0).
    assert probe == ('yuv444p10le' if full else 'yuv422p10le'), probe
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(stream), '-frames:v', '12',
                    '-pix_fmt', probe, '-f', 'rawvideo', str(raw)], check=True, timeout=60)
    chroma_width = W if full else W // 2
    size = W * H * 2 + chroma_width * H * 4
    frame = raw.read_bytes()[size * 11:size * 12]
    assert len(frame) == size, 'expected twelve decoded frames'
    y, cb, cr = array.array('H'), array.array('H'), array.array('H')
    y.frombytes(frame[:W * H * 2]); cb.frombytes(frame[W * H * 2:W * H * 2 + chroma_width * H * 2])
    cr.frombytes(frame[W * H * 2 + chroma_width * H * 2:])
    luma, chroma = Counter(), Counter()
    for patch in range(128):
        x, row = (patch % 16) * 120 + 60, (patch // 16) * 135 + 67
        luma[y[row * W + x] - (64 + patch * 6 + patch % 7)] += 1
        chroma[cb[row * chroma_width + x * chroma_width // W] - (620 - patch)] += 1
        chroma[cr[row * chroma_width + x * chroma_width // W] - (400 + patch)] += 1
    print(f'{name}: luma errors {dict(sorted(luma.items()))} chroma errors {dict(sorted(chroma.items()))}', flush=True)
    return luma, chroma


def main():
    with tempfile.TemporaryDirectory(prefix='pixelview-422-rounding-') as tmp:
        work = Path(tmp)
        subprocess.run(['xcrun', 'clang', '-fobjc-arc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                        str(HERE / 'test-422-rounding.m'), '-framework', 'Foundation', '-framework', 'VideoToolbox',
                        '-framework', 'CoreVideo', '-framework', 'CoreMedia', '-o', str(work / 'encode')], check=True)
        exact = (Counter({0: 128}), Counter({0: 256}))
        assert errors(work, 'clean', 0, False) == exact
        assert errors(work, 'noise-above', 12, False) == exact
        below = errors(work, 'noise-below-unrounded', -12, False)
        # Not asserted exact: this is the truncation the rounding exists for.
        print('unrounded noise below a code loses', sum(n for e, n in below[0].items() if e), 'of 128 luma patches')
        assert errors(work, 'noise-below-rounded', -12, True) == exact
        assert errors(work, 'noise-above-rounded', 12, True) == exact
        # The same for Main 4:4:4 10 from the P416 canvas output.
        assert errors(work, '444-clean', 0, False, True) == exact
        below = errors(work, '444-noise-below-unrounded', -12, False, True)
        print('4:4:4 unrounded noise below a code loses', sum(n for e, n in below[0].items() if e), 'of 128 luma patches')
        assert errors(work, '444-noise-below-rounded', -12, True, True) == exact
        assert errors(work, '444-noise-above-rounded', 12, True, True) == exact
    print('PASS hardware HEVC 4:2:2 10 and 4:4:4 10: rounded 16-bit input encodes exact ten-bit codes')


if __name__ == '__main__':
    main()
