#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline real VT + actual linked WHIP conversion audit; NOT RTP/owner admission."""
import csv
import hashlib
import json
import os
import re
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TESTS = Path(__file__).resolve().parent
APPROVED = os.environ.get('PV_VT_APPROVED_RATES') == '1'
OUT = ROOT / ('plugins/pixelview-whep/.test-build/obs-vt-approved' if APPROVED else 'plugins/pixelview-whep/.test-build/obs-vt-cadence')
OUT.mkdir(parents=True, exist_ok=True)
FRAMEWORKS = ROOT / 'build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app/Contents/Frameworks'
PLUGIN = ROOT / 'build_macos_native422/plugins/mac-videotoolbox/RelWithDebInfo/mac-videotoolbox.plugin'
DEPS = ROOT / '.deps/obs-deps-2026-08-26-universal'
ENV = {k: v for k, v in os.environ.items() if not k.startswith(('GST_', 'DYLD_'))}
ENV.update(HOME=str(OUT), CFFIXED_USER_HOME=str(OUT), DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',
           DYLD_LIBRARY_PATH=str(FRAMEWORKS), DYLD_FRAMEWORK_PATH=str(FRAMEWORKS))
commands = []
def run(command, **kw):
    command = list(map(str, command)); commands.append(command)
    (OUT / 'commands.json').write_text(json.dumps(commands, indent=2))
    return subprocess.run(command, env=ENV, timeout=120, check=True, **kw)

def main():
    (OUT / 'result.json').unlink(missing_ok=True)
    run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'libobs'),
         '-I'+str(DEPS/'include'), '-I'+str(ROOT/'build_macos_native422/config'), '-F'+str(FRAMEWORKS),
         '-framework', 'libobs', '-framework', 'Cocoa', '-Wl,-rpath,'+str(FRAMEWORKS),
         TESTS/'obs-vt-cadence.cpp', '-o', OUT/'capture'])
    # Compile the real WHIP duration conversion statements against its actual
    # libdatachannel dependency, rather than emulating an assumed floor/round.
    source = (ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
    conversion = source[source.index('\tauto elapsed_seconds ='):source.index('\n#if RTC_VERSION_MAJOR', source.index('\tauto elapsed_seconds ='))]
    assert 'secondsToTimestamp' in conversion and 'rtp_config->timestamp + elapsed_timestamp' in conversion
    generated = '''#include <rtc/rtppacketizationconfig.hpp>
#include <cstdio>
#include <cstdint>
int main() {
 rtc::RtpPacketizationConfig config(1,"offline",96,90000); auto *rtp_config=&config;
 rtp_config->timestamp=0; long long input;
 while(scanf("%lld",&input)==1) { int64_t duration=input;
''' + conversion + '''
 printf("%u,%u\\n",elapsed_timestamp,rtp_config->timestamp);
 }
}
'''
    (OUT/'whip-conversion.cpp').write_text(generated)
    run(['xcrun','clang++','-std=c++17','-DRTC_ENABLE_MEDIA=1','-I'+str(DEPS/'include'), OUT/'whip-conversion.cpp',
         FRAMEWORKS/'libdatachannel.dylib','-Wl,-rpath,'+str(FRAMEWORKS),'-o',OUT/'whip-conversion'])
    results = []
    for num, den in ([(24000,1001),(24,1),(25,1),(30000,1001)] if APPROVED else [(30,1),(30000,1001),(29999,1000)]):
        tag = f'{num}-{den}'; video = OUT/(tag+'.hevc')
        with (OUT/(tag+'-capture.log')).open('w') as log:
            run([OUT/'capture',PLUGIN,video,num,den],stdout=log,stderr=subprocess.STDOUT)
        rows = list(csv.DictReader(Path(str(video)+'.csv').open()))
        assert len(rows) >= 170
        assert all((int(r['timebase_num']),int(r['timebase_den']))==(den,num) for r in rows)
        assert all(int(r['pts'])==int(r['dts']) for r in rows)
        assert all(int(b['dts'])-int(a['dts'])==den for a,b in zip(rows,rows[1:]))
        deltas=[int(b['dts_usec'])-int(a['dts_usec']) for a,b in zip(rows,rows[1:])]
        measured = run([OUT/'whip-conversion'], input=''.join(f'{d}\n' for d in deltas), capture_output=True,text=True).stdout
        (OUT/(tag+'-rtp-conversion.csv')).write_text('increment,cumulative\n'+measured)
        ticks=[int(line.split(',')[0]) for line in measured.splitlines()]
        probe=json.loads(run(['ffprobe','-v','error','-show_streams','-of','json',video],capture_output=True,text=True).stdout)['streams'][0]
        for key,value in {'width':1920,'height':1080,'level':120,'pix_fmt':'yuv422p10le','color_range':'tv','color_space':'bt709','color_transfer':'bt709','color_primaries':'bt709'}.items():
            assert probe[key]==value,(key,probe)
        trace=run(['ffmpeg','-v','verbose','-i',video,'-c','copy','-bsf:v','trace_headers','-f','null','-'],capture_output=True,text=True).stderr
        (OUT/(tag+'-headers.log')).write_text(trace)
        values={}
        for key in ['general_profile_idc','general_tier_flag','general_level_idc','chroma_format_idc','bit_depth_luma_minus8','bit_depth_chroma_minus8','vps_timing_info_present_flag','vui_timing_info_present_flag']:
            found={int(x) for x in re.findall(r'\b'+key+r'\s+[01]+\s+=\s+(\d+)',trace)}
            assert len(found)==1,(key,found); values[key]=found.pop()
        assert values==dict(zip(values,[4,0,120,2,2,2,0,0])),values
        hashes=run(['ffmpeg','-v','error','-c:v','hevc','-i',video,'-f','framemd5','-'],capture_output=True,text=True).stdout
        (OUT/(tag+'-decoded.framemd5')).write_text(hashes)
        frame_hashes=[line.rsplit(',',1)[1].strip() for line in hashes.splitlines() if line and not line.startswith('#')]
        assert len(frame_hashes)==len(rows) and len(set(frame_hashes))==len(rows)
        # Native captured packets versus unchanged WHIP conversion, no network
        # or arrival clock is involved in this drift measurement.
        expected_num=len(ticks)*90000*den
        results.append(dict(fps_num=num,fps_den=den,packets=len(rows),distinct_decoded_frames=len(set(frame_hashes)),
                            header_fields=values,dts_usec_deltas=dict(Counter(deltas)),rtp_increments=dict(Counter(ticks)),
                            measured_rtp_ticks=sum(ticks),ideal_rtp_ticks_numerator=expected_num,ideal_rtp_ticks_denominator=num,
                            tick_error=float(sum(ticks)-expected_num/num),sha256=hashlib.sha256(video.read_bytes()).hexdigest()))
    binary_paths=[PLUGIN/'Contents/MacOS/mac-videotoolbox', FRAMEWORKS/'libobs.framework/libobs', FRAMEWORKS/'libdatachannel.dylib']
    result=dict(audit_passed=True,shipping_admission=False,actual_whip_network=False,actual_vt_encoder=True,
                rates=results,binaries={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in binary_paths},
                conversion_source_sha256=hashlib.sha256(source.encode()).hexdigest())
    (OUT/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))

if __name__=='__main__': main()
