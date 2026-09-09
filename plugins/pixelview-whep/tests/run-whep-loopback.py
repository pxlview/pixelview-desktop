#!/usr/bin/env python3
"""Bounded synthetic-only WHEP acceptance; no app/staging/credentials touched."""
import hashlib,importlib.util,json,os,re,shutil,subprocess,time,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
ENGINE=Path(os.environ.get('PIXELVIEW_ENGINE_SOURCE','/Users/max/src/pv-engine'))
WORK=ROOT/'.test-build/whep-loopback';WORK.mkdir(parents=True,exist_ok=True);WORK.chmod(0o700)
SDK=REPO/'.deps/gstreamer-upstream-1.28.3/sdk';RUNTIME=WORK/'runtime'
if not RUNTIME.exists():shutil.copytree(ROOT/'.test-build/decoder-profile/runtime',RUNTIME)
env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
env.update(GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(RUNTIME/'lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(RUNTIME/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(WORK/'registry.bin'),GOPROXY='off',GOSUMDB='off',GOTOOLCHAIN='local',HOME=str(WORK/'home'),CFFIXED_USER_HOME=str(WORK/'home'))
# Preserve offline Go cache location while isolating Foundation/OBS preferences.
env['GOPATH']=subprocess.check_output(['go','env','GOPATH'],text=True).strip();env['GOCACHE']=subprocess.check_output(['go','env','GOCACHE'],text=True).strip()
Path(env['HOME']).mkdir(exist_ok=True)
FW=REPO/'build_macos/libobs/RelWithDebInfo';DEPS=sorted((REPO/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
cmd=['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-mmacosx-version-min=14.0']
cmd+=['-I'+str(p) for p in [REPO/'libobs',REPO/'build_macos/config',DEPS/'include',SDK/'include',SDK/'include/gstreamer-1.0',SDK/'include/glib-2.0',SDK/'lib/glib-2.0/include']]
cmd+=['-F'+str(FW),'-framework','libobs','-Wl,-rpath,'+str(FW),'-Wl,-rpath,'+str(DEPS/'lib'),'-L'+str(RUNTIME/'lib'),'-Wl,-rpath,'+str(RUNTIME/'lib')]
cmd+=[str(ROOT/p) for p in ['tests/whep-loopback.c','video-format.c','profile-offer.c','capability-probe.c']]
cmd+=['-l'+x for x in ['nice.10','gstwebrtc-1.0.0','gstsdp-1.0.0','gstapp-1.0.0','gstvideo-1.0.0','gstaudio-1.0.0','gstbase-1.0.0','gstreamer-1.0.0','gobject-2.0.0','glib-2.0.0']]
subprocess.run(cmd+['-o',str(WORK/'receiver')],check=True)
spec=importlib.util.spec_from_file_location('selection',ROOT/'tests/engine-profile-selection.py');selection=importlib.util.module_from_spec(spec);spec.loader.exec_module(selection)
server=(ENGINE/'internal/av/whep/server.go').read_text();mapper=(ENGINE/'internal/av/codec/mapper.go').read_text()
source='\n'.join(selection.extract(server,start) for start in ['func parseSupportedCodecs(','func (s *Server) findBestCodecMatch('])
source+='\n'+(ENGINE/'internal/av/codec/negotiation.go').read_text().split(')\n',1)[1]
source=selection.remove_logs(source).replace('codec.MatchesReceiver','MatchesReceiver').replace('codec.Codec','Codec').replace('codec.WebRTCConfig','WebRTCConfig')
binding=(ENGINE/'internal/av/whep/h264_binding.go').read_text()
source+='\n'+selection.extract(binding,'func codecForOffer(')
source+='\n'+binding[binding.index('// receiverBindingTrack adapts'):]
registration=selection.extract(server,'func newWHEPMediaEngine(')
registration=selection.remove_logs(registration).replace('newWHEPMediaEngine(codecMap *codec.Mapper','register(fixtures []Codec').replace('codecMap.Codecs','fixtures').replace('codec.MediaTypeVideo','1').replace('codec.IsABRFormat','isABR')
source+='\n'+registration
source=source.replace('codec.MatchesReceiver','MatchesReceiver').replace('codec.Codec','Codec').replace('codec.WebRTCConfig','WebRTCConfig')
(WORK/'provenance.json').write_text(json.dumps({str(p.relative_to(ENGINE)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ENGINE/'internal/av/whep/server.go',ENGINE/'internal/av/codec/mapper.go',ENGINE/'internal/av/codec/negotiation.go',ENGINE/'internal/av/whep/h264_binding.go',ENGINE/'go.mod',ENGINE/'go.sum']},indent=2))
(WORK/'main.go').write_text((ROOT/'tests/whep-loopback.go.in').read_text()+source)
fixtures=[]
for entry in re.split(r'FormatID:\s*"',mapper)[1:]:
    fmt=entry.split('"',1)[0];web=re.search(r'WebRTC: &WebRTCConfig\{(.*?)\n\s*\},\n\s*Type:',entry,re.S)
    if not web:continue
    w=web[1];mime=re.search(r'MimeType:\s*webrtc.MimeType(\w+)',w)
    if not mime:continue
    preferred=re.search(r'PreferredOutputCodec: \[\]string\{(.*?)\}',entry,re.S)
    primary=re.search(r'SDPFmtpLine:\s*"([^"]*)"',w)[1];variants=re.search(r'SDPFmtpLines: \[\]string\{(.*?)\}',w,re.S)
    name=re.search(r'Name:\s*"([^"]*)"',entry)[1]
    fixtures.append(dict(FormatID=fmt,Type=2 if mime[1]=='Opus' else 1,Profile={'Name':name},PreferredOutputCodec=re.findall(r'"([^"]*)"',preferred[1]) if preferred else [],WebRTC=dict(MimeType=('audio/' if mime[1]=='Opus' else 'video/')+mime[1],ClockRate=int(re.search(r'ClockRate:\s*(\d+)',w)[1]),Channels=int(re.search(r'Channels:\s*(\d+)',w)[1]),PayloadType=int(re.search(r'PayloadType:\s*(\d+)',w)[1]),SDPFmtpLine=primary,SDPFmtpLines=re.findall(r'"([^"]*)"',variants[1]) if variants else ([primary] if primary else []))))
(WORK/'fixtures.json').write_text(json.dumps(fixtures))
(WORK/'go.mod').write_text(re.sub(r'^module .*','module loopback-acceptance',(ENGINE/'go.mod').read_text(),count=1));shutil.copyfile(ENGINE/'go.sum',WORK/'go.sum')
subprocess.run(['go','build','-mod=mod','-o','server','.'],cwd=WORK,env=env,check=True,timeout=90)
cases=[('h264-8bit-420','h264','8',97),('hevc-8bit-420','main','8',96),('hevc-10bit-420','main10','10',120),('vp9-8bit-420','vp9_0','8',98),('vp9-10bit-420','vp9_2','10',121),('negative','negative','negative',None)]
results=[]
for fmt,name,depth,pt in cases:
    if os.environ.get('PV_LOOPBACK_CASE') and name!=os.environ['PV_LOOPBACK_CASE']:continue
    case=WORK/name;case.mkdir(exist_ok=True);case.chmod(0o700);shutil.copyfile(WORK/'fixtures.json',case/'fixtures.json')
    for stale in ['ready.json','offer.sdp','answer.sdp','bound.json']:
        (case/stale).unlink(missing_ok=True)
    children=[]
    try:
        log=(case/'server.log').open('w');p=subprocess.Popen([str(WORK/'server'),fmt],cwd=case,env=env,stdout=log,stderr=subprocess.STDOUT);children.append(p)
        for _ in range(100):
            if (case/'ready.json').exists():break
            if p.poll() is not None:raise RuntimeError((case/'server.log').read_text())
            time.sleep(.05)
        info=json.loads((case/'ready.json').read_text());assert urllib.request.urlopen(info['endpoint'],timeout=2).read()==b'ready'
        if pt is not None:
            file=case/('video.ivf' if name.startswith('vp9') else 'video.'+('h264' if name=='h264' else 'hevc'))
            # VP9 encodes a single BT709 color-space enum, not separate AVC/HEVC VUI fields.
            expected_color={'color_range':'tv','color_space':'bt709'}
            if not name.startswith('vp9'):expected_color.update(color_transfer='bt709',color_primaries='bt709')
            cached=[]
            if file.exists() and file.stat().st_size:
                cached=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(file)],text=True)).get('streams',[])
            color_ok=bool(cached) and all(cached[0].get(k)==v for k,v in expected_color.items())
            if not color_ok:
                pixel='yuv420p10le' if depth=='10' else 'yuv420p';ramp='64+876*X/W' if depth=='10' else '16+219*X/W';chroma='512' if depth=='10' else '128'
                gen=['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',f'nullsrc=s=1024x128:r=30,format={pixel},geq=lum={ramp}:cb={chroma}:cr={chroma}','-frames:v','300','-color_range','tv','-colorspace','bt709','-color_trc','bt709','-color_primaries','bt709']
                if name=='h264':gen+=['-c:v','libx264','-preset','ultrafast','-profile:v','baseline','-level:v','4.2','-x264-params','keyint=30:bframes=0:threads=1:colorprim=bt709:transfer=bt709:colormatrix=bt709','-crf','10']
                elif name.startswith('main'):gen+=['-c:v','libx265','-preset','ultrafast','-profile:v',name,'-x265-params','crf=1:level-idc=4.1:high-tier=0:bframes=0:keyint=30:log-level=error:pools=1:frame-threads=1:colorprim=bt709:transfer=bt709:colormatrix=bt709']
                else:gen+=['-c:v','libvpx-vp9','-profile:v',name[-1],'-lossless','1','-deadline','realtime','-cpu-used','8','-threads','1','-lag-in-frames','0','-g','30']
                generated=subprocess.run(gen+[str(file)],capture_output=True,text=True,timeout=60)
                assert generated.returncode==0,generated.stderr
            probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(file)],text=True));(case/'encoded.json').write_text(json.dumps(probe))
            assert all(probe['streams'][0].get(k)==v for k,v in expected_color.items()),probe
            receiver=subprocess.Popen([str(WORK/'receiver'),info['endpoint'],depth],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);children.append(receiver)
            for _ in range(160):
                if 'CONNECTED' in (case/'server.log').read_text() or receiver.poll() is not None:break
                time.sleep(.05)
            for medium,command in [('video',['-re','-stream_loop','-1','-i',str(file),'-an','-c:v','copy','-strict','experimental']),('audio',['-re','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-ac','2','-c:a','libopus','-application','lowdelay'])]:
                out=(case/(medium+'.log')).open('w');children.append(subprocess.Popen(['ffmpeg','-hide_banner','-loglevel','error',*command,'-f','rtp',f'rtp://127.0.0.1:{info[medium]}?pkt_size=1200&localaddr=127.0.0.1'],stdout=out,stderr=subprocess.STDOUT))
        if pt is None:
            receiver=subprocess.Popen([str(WORK/'receiver'),info['endpoint'],depth],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);children.append(receiver)
        stdout,stderr=receiver.communicate(timeout=25)
        result=subprocess.CompletedProcess(receiver.args,receiver.returncode,stdout,stderr)
        (case/'receiver.log').write_text(result.stdout+result.stderr)
        lines=[l for l in result.stdout.splitlines() if l.startswith('LOOPBACK_RESULT')];print(name,lines,flush=True)
        row={'name':name,'result':lines,'passed':result.returncode==0}
        if pt is None:row['passed']=row['passed'] and (case/'offer.sdp').exists() and 'jitter=50' in '\n'.join(lines)
        if not row['passed']:
            results.append(row);(case/'result.json').write_text(json.dumps(row,indent=2));(WORK/'results.json').write_text(json.dumps(results,indent=2));continue
        if pt is not None:
            bound=json.loads((case/'bound.json').read_text());assert bound['selected']==fmt and bound['video']['PayloadType']==pt,bound
            assert bound['audio']['MimeType'].lower()=='audio/opus',bound
            for sdp in ['offer.sdp','answer.sdp']:
                candidates=re.findall(r'a=candidate:[^\r\n]+',(case/sdp).read_text());assert candidates and all(' 127.0.0.1 ' in c for c in candidates),candidates
            row['bound']=bound
        results.append(row);(case/'result.json').write_text(json.dumps(row,indent=2));(WORK/'results.json').write_text(json.dumps(results,indent=2))
    finally:
        for child in reversed(children):
            if child.poll() is None:child.terminate()
        for child in children:
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:child.kill();child.wait()
results=json.loads((WORK/'results.json').read_text())
assert len(results)==(1 if os.environ.get('PV_LOOPBACK_CASE') else 6)
assert all(r['passed'] for r in results),[r['name'] for r in results if not r['passed']]
print('PASS requested compressed WHEP loopback cases')
