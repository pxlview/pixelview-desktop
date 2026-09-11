#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test-only profile4 admission, real WHEP/source/owner, loopback and fake SDK."""
import json, os, re, shutil, subprocess, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
MODE=os.environ.get('PV_WHEP_PREVIEW','normal')
assert MODE in ['normal','nv12','disabled','blocked']
OUT=ROOT/'plugins/decklink/.test-build/whep'/MODE
OUT.mkdir(parents=True,exist_ok=True);OUT.chmod(0o700)
NATIVE=ROOT/'plugins/pixelview-whep/.test-build/native-422'
SERVER=ROOT/'plugins/pixelview-whep/.test-build/whep-loopback'
ENV={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
ENV.update(HOME=str(OUT),CFFIXED_USER_HOME=str(OUT),DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
def run(cmd,**kw): return subprocess.run([str(x) for x in cmd],check=True,env=ENV,timeout=120,**kw)
if os.environ.get('PV_WHEP_SKIP_BUILD')!='1':
    run(['python3',ROOT/'plugins/decklink/tests/test-whep-pacing.py'])
    with (OUT/'build.log').open('w') as log:
        subprocess.run(['python3',str(ROOT/'plugins/decklink/tests/run-receive.py')],env={**ENV,'PV_DECKLINK_WHEP_BUILD':'1'},stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
        subprocess.run(['python3',str(ROOT/'plugins/pixelview-whep/tests/run-whep-loopback.py')],env={**ENV,'PV_LOOPBACK_BUILD_ONLY':'1','PIXELVIEW_ENGINE_SOURCE':os.environ.get('PIXELVIEW_ENGINE_SOURCE',str(ROOT.parent/'pv-engine'))},stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
shutil.copyfile(SERVER/'fixtures.json',OUT/'fixtures.json')
# 300 temporally distinct, independently decoded lossless images. The first Y
# code is a unique frame identity; all other ramp/row-detail content cycles through
# the separately verified three-frame native fixture. Still level255, NOT admission.
frame_bytes=1024*64*4
raw=OUT/'temporal-input.yuv'
templates=(NATIVE/'limited.yuv').read_bytes()
packed_templates=(NATIVE/'limited.v210').read_bytes()
raw_frames=[];packed_frames=[]
for index in range(300):
    # Sparse motion isolates ownership/order from UDP/decoder throughput. Opt-in
    # stress changes every spatial pattern as well; neither is HD admission.
    pattern=index%3 if os.environ.get('PV_WHEP_TEMPORAL_STRESS')=='1' else 0
    image=bytearray(templates[pattern*frame_bytes:(pattern+1)*frame_bytes])
    image[:2]=(64+index).to_bytes(2,'little');raw_frames.append(bytes(image))
    packed=bytearray(packed_templates[pattern*2816*64:(pattern+1)*2816*64])
    word=int.from_bytes(packed[:4],'little')
    packed[:4]=((word & ~(1023<<10)) | ((64+index)<<10)).to_bytes(4,'little')
    packed_frames.append(bytes(packed))
raw.write_bytes(b''.join(raw_frames))
video=OUT/'temporal-video.hevc'
tags=['-color_range','tv','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709']
run(['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','yuv422p10le','-video_size','1024x64','-framerate','30000/1001',*tags,'-i',raw,'-frames:v','300','-c:v','libx265','-preset','ultrafast','-x265-params','lossless=1:log-level=error:pools=1:frame-threads=1:bframes=0:keyint=30:colorprim=bt709:transfer=bt709:colormatrix=bt709',*tags,video],capture_output=True)
run(['ffmpeg','-v','error','-y','-c:v','hevc','-i',video,'-pix_fmt','yuv422p10le','-f','rawvideo',OUT/'temporal-reference.yuv'])
assert (OUT/'temporal-reference.yuv').read_bytes()==raw.read_bytes()
reference=OUT/'temporal-reference.v210';reference.write_bytes(b''.join(packed_frames))
run(['ffmpeg','-v','error','-y','-f','v210','-video_size','1024x64','-i',reference,'-pix_fmt','yuv422p10le','-f','rawvideo',OUT/'temporal-v210-reference.yuv'])
assert (OUT/'temporal-v210-reference.yuv').read_bytes()==raw.read_bytes()
runtime=NATIVE/'runtime'
ENV.update(GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(runtime/'lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(runtime/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(OUT/'registry.bin'))
children=[]
for stale in ['ready.json','offer.sdp','answer.sdp','bound.json','result.json']:(OUT/stale).unlink(missing_ok=True)
try:
    servers=[]
    for index in range(3):
        case=OUT if not index else OUT/('reconnect' if index==1 else 'negative')
        case.mkdir(exist_ok=True);shutil.copyfile(SERVER/'fixtures.json',case/'fixtures.json')
        for stale in ['ready.json','offer.sdp','answer.sdp','bound.json']:(case/stale).unlink(missing_ok=True)
        log=(case/'server.log').open('w');server=subprocess.Popen([str(SERVER/'server'),'negative' if index==2 else 'hevc-10bit-422'],cwd=case,env=ENV,stdout=log,stderr=subprocess.STDOUT);children.append(server)
        for _ in range(100):
            if (case/'ready.json').exists():break
            assert server.poll() is None
            time.sleep(.05)
        servers.append((json.loads((case/'ready.json').read_text()),case))
    info=servers[0][0]
    dump=OUT/'native.yuv';dump.unlink(missing_ok=True)
    rxenv={**ENV,'DYLD_INSERT_LIBRARIES':str(NATIVE/'native-422-observer.dylib'),'PV_NATIVE422_X422_DUMP':str(dump),'PV_NATIVE422_OBSERVER_EXECUTABLE':str(ROOT/'plugins/decklink/.test-build/receive/whep-receive')}
    log=(OUT/'receiver.log').open('w');rx=subprocess.Popen([str(ROOT/'plugins/decklink/.test-build/receive/whep-receive'),info['endpoint'],str(reference),'1024','64',servers[1][0]['endpoint'],servers[2][0]['endpoint']],env=rxenv,stdout=log,stderr=subprocess.STDOUT);children.append(rx)
    for info,case in servers[:2]:
        for _ in range(240):
            if 'CONNECTED' in (case/'server.log').read_text() or rx.poll() is not None:break
            time.sleep(.05)
        for medium,args in [('video',['-re','-r','30000/1001','-i',str(video),'-an','-c:v','copy','-strict','experimental']),('audio',['-re','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-ac','2','-c:a','libopus','-application','lowdelay'])]:
            log=(case/(medium+'.log')).open('w');children.append(subprocess.Popen(['ffmpeg','-v','error',*args,'-f','rtp',f'rtp://127.0.0.1:{info[medium]}?pkt_size=1200&localaddr=127.0.0.1'],env=ENV,stdout=log,stderr=subprocess.STDOUT))
    code=rx.wait(timeout=25)
    result={'passed':False,'returncode':code,'shipping_admission':False,'physical_card':False,'fixture_level':255,'preview':MODE}
    (OUT/'result.json').write_text(json.dumps(result,indent=2))
    print((OUT/'receiver.log').read_text());assert code==0,result
    for info,case in servers[:2]:
        bound=json.loads((case/'bound.json').read_text());assert bound['selected']=='hevc-10bit-422';assert bound['audio']['MimeType'].lower()=='audio/opus'
        assert 'profile-id=4' in (case/'offer.sdp').read_text()
        timestamps=[int(x) for x in re.findall(r'RTP_MARKER video/H265 timestamp=(\d+)',(case/'server.log').read_text())]
        assert len(timestamps)>=30
        assert all((b-a) % (1<<32)==3003 for a,b in zip(timestamps,timestamps[1:]))
        for sdp in ['offer.sdp','answer.sdp']:
            candidates=re.findall(r'a=candidate:[^\r\n]+',(case/sdp).read_text());assert candidates and all(' 127.0.0.1 ' in x for x in candidates)
    assert (servers[2][1]/'offer.sdp').exists()
    observed=dump.read_bytes();assert len(observed)%(frame_bytes*2)==0
    frames=len(observed)//(frame_bytes*2);assert frames>=30
    identities=[]
    for offset in range(0,len(observed),frame_bytes*2):
        first=observed[offset:offset+frame_bytes]
        second=observed[offset+frame_bytes:offset+frame_bytes*2]
        # Native pack lock and optional preview-copy lock independently observe
        # the same CV image, hence exactly two observations per filter frame.
        identity=int.from_bytes(first[:2],'little')-64
        assert 0<=identity<300 and first==second==raw_frames[identity]
        identities.append(identity)
    resets=[i for i,(a,b) in enumerate(zip(identities,identities[1:]),1) if b<a]
    assert len(resets)==1 and identities[0]==0 and identities[resets[0]]==0
    assert all(b==a+1 or (i+1)==resets[0] for i,(a,b) in enumerate(zip(identities,identities[1:])))
    text=(OUT/'receiver.log').read_text()
    rows=[(a,int(b),int(c),int(d),int(e)) for a,b,c,d,e in re.findall(r'FEED ([AV]) pts=(\d+) duration=(\d+) count=(\d+) token=(\d+)',text)]
    for kind,duration in [('V',33366666),('A',20000000)]:
        media=[x for x in rows if x[0]==kind];assert len(media)>=30
        assert all(x[2]==duration for x in media)
        assert all(y[1]>=x[1] for x,y in zip(media,media[1:])),kind
    summaries=re.findall(r'MAIN422_OWNER compared=(\d+)',text);assert len(summaries)==2 and all(int(x)==45 for x in summaries)
    result.update(passed=True,native_frames_compared=frames,source_timestamps_observed=True,source_durations_observed=True,owner_frames_compared=sum(map(int,summaries)),reconnect_same_source_owner=True,http406_failed_closed=True)
    (OUT/'result.json').write_text(json.dumps(result,indent=2))
finally:
    for p in reversed(children):
        if p.poll() is None:p.terminate()
    for p in children:
        try:p.wait(timeout=5)
        except subprocess.TimeoutExpired:p.kill();p.wait()
