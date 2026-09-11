#!/usr/bin/env python3
"""Offline real bundled GStreamer offer tests. --legacy is an intentional RED control."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy', action='store_true')
    args = parser.parse_args()
    work = ROOT / '.test-build/profile-offer'
    work.mkdir(parents=True, exist_ok=True)
    work.chmod(0o700)  # local SDP may include host ICE candidates
    runtime = work / 'runtime'
    if not runtime.exists():
        shutil.copytree(REPO / '.deps/pixelview-gstreamer-upstream', runtime)
    sdk = REPO / '.deps/gstreamer-upstream-1.28.3/sdk'
    binary = work / 'profile-offer'
    cmd = ['clang', '-Wall', '-Wextra', '-Werror', '-mmacosx-version-min=14.0']
    cmd += ['-I'+str(sdk/p) for p in ('include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include')]
    cmd += [str(ROOT/'tests/profile-offer.c')]
    cmd += ['-DPROFILE_POLICY_LEGACY'] if args.legacy else [str(ROOT/'profile-offer.c')]
    cmd += ['-L'+str(runtime/'lib'), '-Wl,-rpath,'+str(runtime/'lib')]
    cmd += ['-l'+s for s in ('gstwebrtc-1.0.0','gstsdp-1.0.0','gstreamer-1.0.0','gobject-2.0.0','glib-2.0.0')]
    subprocess.run(cmd+['-o',str(binary)], check=True)
    env = {k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
    env.update(GST_PLUGIN_SYSTEM_PATH_1_0=str(runtime/'lib/gstreamer-1.0'), GST_PLUGIN_PATH_1_0='', GST_REGISTRY=str(work/'registry.bin'), GST_REGISTRY_FORK='no')
    result = subprocess.run([str(binary)],env=env, capture_output=True,text=True,timeout=30)
    print(result.stdout, result.stderr)
    assert result.returncode == 0, 'Unprobed profiles must not be advertised'
    result = subprocess.run([str(binary),'3','123'],env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode == 0, result.stderr
    (work/'main.sdp').write_text(result.stdout)
    assert 'level-id=123;profile-id=1;tier-flag=0;tx-mode=SRST' in result.stdout, result.stdout
    assert 'packetization-mode=1' in result.stdout, result.stdout
    # Derive the advertised profile/constraints/level from the actual probe SPS.
    import re
    header = (ROOT/'capability-fixtures.h').read_text()
    packet = header.split('fixture_h264_0[] = {',1)[1].split('};',1)[0]
    data = bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',packet))
    nals = re.split(b'\x00\x00\x00?\x01',data)
    sps = next(n for n in nals if n and n[0] & 31 == 7)
    profile_level = sps[1:4].hex()
    assert profile_level == '42c02a', profile_level
    assert 'profile-level-id='+profile_level in result.stdout, result.stdout
    assert 'OPUS/48000/2' in result.stdout, result.stdout
    print('PASS real Main + H264 mode1 + stereo Opus offer')
    result = subprocess.run([str(binary),'31','123'],env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode == 0, result.stderr
    (work/'alternatives.sdp').write_text(result.stdout)
    assert 'level-id=123;profile-id=2;tier-flag=0;tx-mode=SRST' in result.stdout, result.stdout
    assert result.stdout.count('H265/90000') == 3, result.stdout
    assert result.stdout.count('VP9/90000') == 2, result.stdout
    assert 'profile-id=0' in result.stdout and 'profile-id=2' in result.stdout
    import re
    payloads = re.findall(r'a=rtpmap:(\d+)', result.stdout)
    assert len(payloads) == len(set(payloads)) == 7, payloads
    print('PASS separate Main/Main10/Main422/VP9-0/VP9-2/H264 payloads + Opus')
    for mask, level, hevc, vp9 in [(31,153,3,2),(31,0,0,2),(31,124,0,2),(5,123,2,0),(9,0,0,1),(17,0,0,1)]:
        result = subprocess.run([str(binary),str(mask),str(level)],env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode == 0, result.stderr
        assert result.stdout.count('H265/90000') == hevc, result.stdout
        assert result.stdout.count('VP9/90000') == vp9, result.stdout
        if mask == 5:
            assert 'profile-id=1;' not in result.stdout
            assert 'profile-id=2;' in result.stdout
    print('PASS level gate and independent Main10/VP9 profile gates')
    for mask, label in [(0,'raw-whep'),(31,'policy-whep')]:
        result = subprocess.run([str(binary),str(mask),'123','whep'],env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode == 0, result.stderr
        (work/(label+'.sdp')).write_text(result.stdout)
        (work/(label+'.caps')).write_text(result.stderr)
        payloads = re.findall(r'a=rtpmap:(\d+)', result.stdout)
        assert len(payloads) == len(set(payloads)), (label, result.stdout)
        if mask:
            assert 'profile-level-id='+profile_level in result.stdout,result.stdout
            assert result.stdout.count('H265/90000') == 3, result.stdout
            assert result.stdout.count('VP9/90000') == 2, result.stdout
            assert 'OPUS/48000/2' in result.stdout, result.stdout
        print('PASS captured actual rswebrtc raw-output',label)

if __name__ == '__main__':
    main()
