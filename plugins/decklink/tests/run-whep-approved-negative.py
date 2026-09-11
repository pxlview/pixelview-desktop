#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual VT HD RTP/Opus -> WHEP -> production finite-rate gate -> real owner refusal.
Tests excluded30 and an unsupported nearby rate in the excluded3000-tick class.
Uses an existing isolated runtime/server and the prior audit's actual VT payloads.
No WHIP HTTP or live engine; the actual libdatachannel packetizer and extracted
WHIP timestamp conversion replay recorded packets into the loopback Pion sender.
"""
import importlib.util
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
TESTS=ROOT/'plugins/pixelview-whep/tests'
CAPTURE=ROOT/'plugins/pixelview-whep/.test-build/obs-vt-cadence'
OUT=ROOT/'plugins/pixelview-whep/.test-build/finite-rate/negative'
STALL=os.environ.get('PV_TEST_DOWNSTREAM_STALL')=='1'
CHANGE=os.environ.get('PV_TEST_RATE_CHANGE')=='1'
CONFLICT=os.environ.get('PV_TEST_CAPS_CONFLICT')=='1'
assert sum([STALL,CHANGE,CONFLICT])<=1
if STALL or CHANGE or CONFLICT:
    CAPTURE=ROOT/'plugins/pixelview-whep/.test-build/obs-vt-approved'
    OUT=ROOT/'plugins/pixelview-whep/.test-build/finite-rate/matrix-rca'/('24-stall' if STALL else '24-change' if CHANGE else '24-conflict')
OUT.mkdir(parents=True,exist_ok=True)
NATIVE=ROOT/'plugins/pixelview-whep/.test-build/native-422'
SERVER=ROOT/'plugins/pixelview-whep/.test-build/whep-loopback'
FRAMEWORKS=ROOT/'build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app/Contents/Frameworks'
ENV={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
ENV.update(HOME=str(OUT),CFFIXED_USER_HOME=str(OUT),DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',
           PV_DECKLINK_WHEP_CADENCE_AUDIT='1',PV_WHEP_APPROVED='1',PV_WHEP_PREVIEW='normal',GST_PLUGIN_SYSTEM_PATH_1_0='',
           GST_PLUGIN_PATH_1_0=str(NATIVE/'runtime/lib/gstreamer-1.0'),
           GST_PLUGIN_SCANNER=str(NATIVE/'runtime/libexec/gst-plugin-scanner'),GST_REGISTRY=str(OUT/'whep-registry.bin'))
def run(cmd,**kw):return subprocess.run(list(map(str,cmd)),check=True,env=ENV,timeout=120,**kw)
def main():
    (OUT/'whep-result.json').unlink(missing_ok=True)
    assert (CAPTURE/'result.json').is_file(),'Run run-obs-vt-cadence.py first'
    source=(ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
    conversion=source[source.index('\tauto elapsed_seconds ='):source.index('\n#if RTC_VERSION_MAJOR',source.index('\tauto elapsed_seconds ='))]
    (OUT/'whip-timing.inc').write_text(conversion)
    run(['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-DRTC_ENABLE_MEDIA=1',
         '-I'+str(ROOT/'.deps/obs-deps-2026-08-26-universal/include'),'-I'+str(OUT),
         TESTS/'obs-vt-rtp-replay.cpp',FRAMEWORKS/'libdatachannel.dylib','-Wl,-rpath,'+str(FRAMEWORKS),'-o',OUT/'replay'])
    cases=[]
    for tag,increment in ([('24-1',3750)] if STALL or CHANGE or CONFLICT else [('30-1',3000),('29999-1000',3000)]):
        case=OUT/('whep-'+tag);case.mkdir(exist_ok=True)
        for stale in ['ready.json','offer.sdp','answer.sdp','bound.json']:(case/stale).unlink(missing_ok=True)
        shutil.copyfile(SERVER/'fixtures.json',case/'fixtures.json')
        children=[];logs=[]
        def spawn(cmd,name,**kw):
            log=(case/name).open('w');logs.append(log)
            p=subprocess.Popen(list(map(str,cmd)),env=ENV,stdout=log,stderr=subprocess.STDOUT,**kw);children.append(p);return p
        try:
            server=spawn([SERVER/'server','hevc-10bit-422'],'server.log',cwd=case)
            deadline=time.monotonic()+10
            while not (case/'ready.json').exists():
                assert server.poll() is None and time.monotonic()<deadline
                time.sleep(.02)
            info=json.loads((case/'ready.json').read_text())
            rx=spawn([ROOT/'plugins/decklink/.test-build/receive/whep-cadence-reject',info['endpoint']],'receiver.log')
            deadline=time.monotonic()+15
            while 'CONNECTED' not in (case/'server.log').read_text():
                assert rx.poll() is None and server.poll() is None and time.monotonic()<deadline
                time.sleep(.02)
            audio=spawn(['ffmpeg','-v','error','-re','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-ac','2','-c:a','libopus','-application','lowdelay','-f','rtp',f'rtp://127.0.0.1:{info["audio"]}?pkt_size=1200&localaddr=127.0.0.1'],'audio.log')
            # Ensure real Opus crosses the receiver before video fail-closes.
            time.sleep(.3)
            replay=spawn([OUT/'replay',CAPTURE/(tag+'.hevc'),info['video'],tag],'replay.log')
            code=rx.wait(timeout=20)
            text=(case/'receiver.log').read_text();assert code==0,(code,text)
            assert 'ACTUAL_WHEP_VT_HD_OWNER_REFUSED' in text
            match=re.search(r'VT_CADENCE_REFUSAL native=(\d+) opus=(\d+) error=1',text);assert match and int(match[2])>0,text
            if STALL:
                assert int(match[1])>=9
                brackets=re.findall(r'TEST_DOWNSTREAM_STALL (?:enter|exit)=(\d+)',text)
                assert len(brackets)==2 and int(brackets[1])-int(brackets[0])>=650000000
                assert 'Native 422 finite-rate refusal: ordered-rtp-gap' in text
                clocks=re.search(r'last_rtp=(\d+) failure_now=(\d+).*rate=24/1',text)
                assert clocks and int(clocks[2])-int(clocks[1])>500000000
                delivery=re.search(r'previous_delivery_ns=(\d+)',text)
                assert delivery and int(delivery[1])>=650000000
            elif CHANGE: assert int(match[1])>=9
            else: assert int(match[1])==0
            if CONFLICT: assert "TEST_CAPS_CONFLICT injected=25/1" in text
            parsed=re.findall(r'VT_PARSED pts=(\d+) duration=(\d+) caps=(.*)',text);assert parsed,text
            assert all(int(p)<2**64-1 and int(d)==2**64-1 and 'framerate=' not in c for p,d,c in parsed),parsed
            assert all('width=(int)1920' in c and 'height=(int)1080' in c for _,_,c in parsed)
            markers=[int(t) for t in re.findall(r'RTP_MARKER video/H265 timestamp=(\d+)',(case/'server.log').read_text())]
            deltas=[(b-a)%2**32 for a,b in zip(markers,markers[1:])]
            if CHANGE: assert len(deltas)>=70 and deltas[:69]==[3750]*69 and all(x==3600 for x in deltas[69:]),deltas
            else: assert deltas and all(x==increment for x in deltas),markers
            bound=json.loads((case/'bound.json').read_text());assert bound['selected']=='hevc-10bit-422'
            for name in ['offer.sdp','answer.sdp']:
                candidates=re.findall(r'a=candidate:[^\r\n]+',(case/name).read_text());assert candidates and all(' 127.0.0.1 ' in c for c in candidates)
            cases.append(dict(rate=tag,rtp_marker_count=len(markers),increment=increment,opus_sample_frames=int(match[2]),
                              parsed_buffers=len(parsed),parsed_duration_missing=True,parsed_framerate_missing=True,
                              native_frames=int(match[1]),owner_refused=True,level=120,deliberate_downstream_stall=STALL,wire_rate_change=CHANGE,caps_conflict=CONFLICT))
        finally:
            for p in reversed(children):
                if p.poll() is None:p.terminate()
            for p in children:
                try:p.wait(timeout=5)
                except subprocess.TimeoutExpired:p.kill();p.wait()
            for log in logs:log.close()
    result=dict(negative_audit_passed=True,compatibility=False,shipping_admission=False,physical_card=False,actual_whip_http=False,
                actual_whep_rtp_opus=True,cases=cases)
    (OUT/'whep-result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
