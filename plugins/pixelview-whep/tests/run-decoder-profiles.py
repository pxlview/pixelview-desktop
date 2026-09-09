#!/usr/bin/env python3
"""Isolated receive-only VT profile matrix; synthetic public-tool fixtures only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
SDK = REPO / '.deps/gstreamer-upstream-1.28.3/sdk'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=REPO / '.deps/pixelview-gstreamer')
    parser.add_argument('--work', type=Path, default=ROOT / '.test-build/decoder-profile')
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    stage = work / 'runtime'
    if stage.exists():
        shutil.rmtree(stage)
    shutil.copytree(args.runtime, stage)
    # Sign only this private copy. Never stage into an application or shared runtime.
    binaries = [p for p in stage.rglob('*') if p.is_file() and (p.suffix == '.dylib' or p.name in ('gst-inspect-1.0', 'gst-plugin-scanner'))]
    for p in binaries:
        subprocess.run(['codesign', '--force', '--sign', '-', str(p)], check=True, capture_output=True)
        subprocess.run(['codesign', '--verify', '--strict', str(p)], check=True, capture_output=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('GST_', 'DYLD_'))}
    env.update(GST_PLUGIN_SYSTEM_PATH_1_0='', GST_PLUGIN_PATH_1_0=str(stage/'lib/gstreamer-1.0'),
               GST_PLUGIN_SCANNER=str(stage/'libexec/gst-plugin-scanner'), GST_REGISTRY=str(work/'registry.bin'))
    includes = ['-I'+str(SDK/p) for p in ('include/gstreamer-1.0', 'include/glib-2.0', 'lib/glib-2.0/include')]
    libs = ['-l'+n for n in ('gstapp-1.0.0','gstvideo-1.0.0','gstbase-1.0.0','gstreamer-1.0.0','gobject-2.0.0','glib-2.0.0')]
    exe = work/'decoder-profiles'
    subprocess.run(['clang','-arch','arm64','-Wall','-Wextra','-Werror',*includes,str(ROOT/'tests/decoder-profiles.c'),'-o',str(exe),'-L'+str(stage/'lib'),'-Wl,-rpath,'+str(stage/'lib'),*libs,'-framework','VideoToolbox','-framework','CoreMedia','-framework','CoreFoundation'],check=True)
    subprocess.run(['codesign','--force','--sign','-',str(exe)],check=True,capture_output=True)
    hook = work/'decoder-session-observer.dylib'
    subprocess.run(['clang','-arch','arm64','-Wall','-Wextra','-Werror',*includes,'-DDECODER_INTERPOSE_ONLY','-dynamiclib',str(ROOT/'tests/decoder-profiles.c'),'-o',str(hook),'-framework','VideoToolbox','-framework','CoreMedia','-framework','CoreFoundation'],check=True)
    subprocess.run(['codesign','--force','--sign','-',str(hook)],check=True,capture_output=True)
    env['DYLD_INSERT_LIBRARIES'] = str(hook)
    fixtures = [('hevc-main8','hevc','yuv420p',0),('hevc-main10','hevc','yuv420p10le',0),('hevc-main42210','hevc','yuv422p10le',0),
                ('vp9-p0','vp9','yuv420p',0),('vp9-p1-422','vp9','yuv422p',1),('vp9-p1-444','vp9','yuv444p',1),
                ('vp9-p2','vp9','yuv420p10le',2),('vp9-p3-422','vp9','yuv422p10le',3),('vp9-p3-444','vp9','yuv444p10le',3)]
    results = {'machine':subprocess.check_output(['sysctl','-n','machdep.cpu.brand_string'],text=True).strip(),
               'os':subprocess.check_output(['sw_vers'],text=True).strip(), 'runtime':str(args.runtime),
               'signed_binaries_verified':len(binaries),
               'applemedia_sha256':hashlib.sha256((stage/'lib/gstreamer-1.0/libgstapplemedia.dylib').read_bytes()).hexdigest(),
               'ffmpeg_version':subprocess.check_output(['ffmpeg','-version'],text=True).splitlines()[0],
               'fixtures':[], 'runs':[]}
    for name, codec, pix, profile in fixtures:
        fixture = work/(name+('.h265' if codec=='hevc' else '.ivf'))
        cmd = ['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',f'testsrc2=size=192x128:rate=24,format={pix}', '-frames:v','12','-color_range','tv','-colorspace','bt709','-color_trc','bt709','-color_primaries','bt709']
        if codec=='hevc':
            cmd += ['-c:v','libx265','-preset','ultrafast','-x265-params','log-level=error:pools=1:frame-threads=1:bframes=0:keyint=12:colorprim=bt709:transfer=bt709:colormatrix=bt709']
        else:
            cmd += ['-c:v','libvpx-vp9','-profile:v',str(profile),'-deadline','realtime','-cpu-used','8','-threads','2','-lag-in-frames','0']
        cmd += [str(fixture)]
        subprocess.run(cmd,check=True,capture_output=True)
        probe = json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(fixture)],text=True))
        results['fixtures'].append({'name':name,'command':cmd,'sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'ffprobe':probe})
        for decoder in ('vtdec','vtdec_hw'):
            formats = ('auto','P010_10LE','AYUV64') if codec=='hevc' else ('auto','P010_10LE')
            if name == 'hevc-main42210':
                formats += ('I422_10LE','Y210')
            for fmt in formats:
                dump = work/f'{name}-{decoder}-{fmt}.raw'
                result = subprocess.run([str(exe),str(fixture),codec,decoder,fmt],env={**env,'DECODER_FIRST_FRAME':str(dump)},text=True,capture_output=True,timeout=25)
                row = {'fixture':name,'decoder':decoder,'requested_format':fmt,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr}
                assert result.returncode in (0, 1, 3), row
                if result.returncode == 0:
                    # This short probe sometimes emits 11/12 frames; cause unisolated.
                    # retain actual counts, do not claim a lossless/full-drain test.
                    assert 'RESULT frames=0 ' not in result.stdout, 'No decoded frames'
                    assert 'HW_SESSION create=0 query=0 hardware=' in result.stderr, 'Hardware observer did not execute'
                    if fmt == 'P010_10LE':
                        import struct
                        values = struct.unpack('<'+str(192*128)+'H',dump.read_bytes()[:192*128*2])
                        row['p010_luma_effective_low2_nonzero'] = sum(bool((v >> 6) & 3) for v in values)
                        row['p010_luma_unique_values'] = len(set(values))
                results['runs'].append(row)
                print(name,decoder,fmt,result.returncode,result.stdout.strip().replace('\n',' | '),result.stderr.strip(),flush=True)
                (work/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    # Actual empty/malformed compressed input must never count as a pass.
    invalid=work/'invalid.h265';invalid.write_bytes(b'not a coded picture')
    negative=subprocess.run([str(exe),str(invalid),'hevc','vtdec','auto'],env=env,text=True,capture_output=True,timeout=25)
    assert negative.returncode != 0 and 'RESULT frames=0' in negative.stdout, negative
    results['negative_control']={'returncode':negative.returncode,'stdout':negative.stdout,'stderr':negative.stderr}
    for factory in ('vtdec','vtdec_hw','vp9dec','avdec_vp9','avdec_h265'):
        r=subprocess.run([str(stage/'bin/gst-inspect-1.0'),factory],env=env,text=True,capture_output=True)
        (work/(factory+'.inspect.txt')).write_text(r.stdout+r.stderr)
    inventory = {}
    for name in ('vpx','libav'):
        p = SDK/'lib/gstreamer-1.0'/f'libgst{name}.dylib'
        strings = subprocess.check_output(['strings',str(p)],text=True)
        inventory[name] = {'path':str(p.relative_to(REPO)),
            'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),
            'linkage':subprocess.check_output(['otool','-arch','arm64','-L',str(p)],text=True),
            'relevant_strings':sorted(set(line for line in strings.splitlines()
                if any(term in line for term in ('vp9dec','avdec_','I420_10LE','I422_10LE','Y444_10LE','LGPL')) and len(line)<600))}
    (work/'sdk-software-inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')
    (work/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('RESULTS',work/'results.json')


if __name__ == '__main__':
    main()
