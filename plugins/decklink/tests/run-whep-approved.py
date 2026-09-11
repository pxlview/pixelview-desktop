#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual Apple VT HD payloads -> loopback WHEP/Opus -> production native/owner.
Offer bit remains TEST-ONLY. Native finite-rate admission is production code.
"""
import hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'plugins/pixelview-whep/.test-build/obs-vt-approved'
NATIVE=ROOT/'plugins/pixelview-whep/.test-build/native-422'
SERVER=ROOT/'plugins/pixelview-whep/.test-build/whep-loopback'
FW=ROOT/'build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app/Contents/Frameworks'
ENV={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
ENV.update(HOME=str(OUT),CFFIXED_USER_HOME=str(OUT),DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',
 PV_WHEP_APPROVED='1',PV_WHEP_PREVIEW='normal',GST_PLUGIN_SYSTEM_PATH_1_0='',
 GST_PLUGIN_PATH_1_0=str(NATIVE/'runtime/lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(NATIVE/'runtime/libexec/gst-plugin-scanner'),GST_REGISTRY=str(OUT/'registry.bin'))
def run(cmd,**kw):return subprocess.run(list(map(str,cmd)),check=True,env=ENV,timeout=180,**kw)
def main():
 evidence=Path(os.environ.get('PV_APPROVED_EVIDENCE',str(OUT)))
 if evidence!=OUT:evidence.mkdir(parents=True,exist_ok=False)
 reuse=os.environ.get('PV_APPROVED_REUSE_REFERENCES')=='1'
 assert not any(os.environ.get(k) for k in ['PV_TEST_RATE_CHANGE','PV_TEST_CAPS_CONFLICT','PV_TEST_DOWNSTREAM_STALL','PV_TEST_DECODER_CONSTRUCTION']), 'negative fault enabled in positive runner'
 (evidence/'whep-result.json').unlink(missing_ok=True)
 source=(ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
 start=source.index('\tauto elapsed_seconds =')
 (OUT/'whip-timing.inc').write_text(source[start:source.index('\n#if RTC_VERSION_MAJOR',start)])
 run(['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-DRTC_ENABLE_MEDIA=1',
  '-I'+str(ROOT/'.deps/obs-deps-2026-08-26-universal/include'),'-I'+str(OUT),
  ROOT/'plugins/pixelview-whep/tests/obs-vt-rtp-replay.cpp',FW/'libdatachannel.dylib','-Wl,-rpath,'+str(FW),'-o',OUT/'replay'])
 # HD observer is test-only scalar extraction; optimize it rather than putting
 # millions of unoptimized instrumentation operations in each decode callback.
 run(['xcrun','clang','-O2','-Wall','-Wextra','-Werror','-dynamiclib',ROOT/'plugins/pixelview-whep/tests/native-422-observer.c','-framework','CoreVideo','-lcompression','-o',OUT/'observer.dylib'])
 rates=json.loads((OUT/'result.json').read_text())['rates'];assert len(rates)==4
 results=[]
 for rate in rates:
  n,d=rate['fps_num'],rate['fps_den'];tag=f'{n}-{d}'
  if os.environ.get('PV_APPROVED_CASE') and os.environ['PV_APPROVED_CASE']!=tag:continue
  case=evidence/('whep-'+tag);case.mkdir(exist_ok=True);video=OUT/(tag+'.hevc')
  (case/'result.json').unlink(missing_ok=True)
  # Reuse independently reverified historical references READ-ONLY. The native
  # observer still owns bounded compressed copies before receiver main starts.
  refdir=OUT/('whep-'+tag) if reuse else case
  reference=refdir/'reference.v210';raw_reference=refdir/'reference.yuv'
  if not reuse:
   needed=rate['packets']*(5120*1080+1920*1080*4)+512*1024*1024
   assert shutil.disk_usage(case).free>needed,'insufficient disk for reference fixtures'
   run(['ffmpeg','-v','error','-y','-c:v','hevc','-i',video,'-c:v','v210','-f','rawvideo',reference])
   run(['ffmpeg','-v','error','-y','-c:v','hevc','-i',video,'-pix_fmt','yuv422p10le','-f','rawvideo',raw_reference])
  hashes={}
  proc=subprocess.Popen(['ffmpeg','-v','error','-c:v','hevc','-i',str(video),'-pix_fmt','yuv422p10le','-f','rawvideo','-'],stdout=subprocess.PIPE,env=ENV)
  assert proc.stdout is not None
  frame_size=1920*1080*4
  with raw_reference.open('rb') as existing:
   while True:
    frame=proc.stdout.read(frame_size)
    if not frame:break
    assert len(frame)==frame_size and existing.read(frame_size)==frame
    hashes[hashlib.sha256(frame).hexdigest()]=len(hashes)
   assert not existing.read(1)
  assert proc.wait()==0 and len(hashes)==rate['packets']
  if reuse:
   proc=subprocess.Popen(['ffmpeg','-v','error','-c:v','hevc','-i',str(video),'-c:v','v210','-f','rawvideo','-'],stdout=subprocess.PIPE,env=ENV)
   assert proc.stdout is not None
   with reference.open('rb') as existing:
    while True:
     chunk=proc.stdout.read(5120*1080)
     if not chunk:break
     assert existing.read(len(chunk))==chunk
    assert not existing.read(1)
   assert proc.wait()==0
  # New registry for the actual receiver (and its real scanner) at every rate.
  ENV['GST_REGISTRY']=str(case/'fresh-registry.bin')
  assert not Path(ENV['GST_REGISTRY']).exists()
  children=[];logs=[]
  def spawn(cmd,name,cwd=case,extra=None):
   log=(cwd/name).open('w');logs.append(log)
   p=subprocess.Popen(list(map(str,cmd)),cwd=cwd,env={**ENV,**(extra or {})},stdout=log,stderr=subprocess.STDOUT);children.append(p);return p
  try:
   servers=[]
   for i in range(2):
    dest=case/str(i);dest.mkdir(exist_ok=True)
    for name in ['ready.json','offer.sdp','answer.sdp','bound.json']:(dest/name).unlink(missing_ok=True)
    shutil.copyfile(SERVER/'fixtures.json',dest/'fixtures.json');server=spawn([SERVER/'server','hevc-10bit-422'],'server.log',dest)
    deadline=time.monotonic()+10
    while not (dest/'ready.json').exists():
     assert server.poll() is None and time.monotonic()<deadline;time.sleep(.02)
    servers.append((json.loads((dest/'ready.json').read_text()),dest))
   dump=case/'native.sha256';dump.unlink(missing_ok=True)
   rx=spawn([ROOT/'plugins/decklink/.test-build/receive/whep-approved',servers[0][0]['endpoint'],reference,n,d,servers[1][0]['endpoint']],
    'receiver.log',extra={'DYLD_INSERT_LIBRARIES':str(OUT/'observer.dylib'),'PV_NATIVE422_X422_HASHES':str(dump),'PV_NATIVE422_OBSERVER_EXECUTABLE':str(ROOT/'plugins/decklink/.test-build/receive/whep-approved'),'PV_NATIVE422_REFERENCE':str(raw_reference),'PV_NATIVE422_REFERENCE_FRAME_BYTES':str(frame_size)})
   for info,dest in servers:
    deadline=time.monotonic()+15
    while 'CONNECTED' not in (dest/'server.log').read_text():
     assert rx.poll() is None and time.monotonic()<deadline,(case,(case/'receiver.log').read_text());time.sleep(.02)
    spawn(['ffmpeg','-v','error','-re','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-ac','2','-c:a','libopus','-application','lowdelay','-f','rtp',f'rtp://127.0.0.1:{info["audio"]}?pkt_size=1200&localaddr=127.0.0.1'],'audio.log',dest)
    spawn([OUT/'replay',video,info['video'],tag],'replay.log',dest)
   if os.environ.get('PV_MATRIX_TRACE'):
    # Preserve one live stack at the first native stall; never retry the run.
    deadline=time.monotonic()+25; last_count=0; changed=time.monotonic(); sampled=False
    while rx.poll() is None and time.monotonic()<deadline:
     current=(case/'receiver.log').read_text(); count=current.count('FEED V pts=')
     if count!=last_count: last_count=count;changed=time.monotonic()
     if count and not sampled and time.monotonic()-changed>.1 and 'APPROVED_SCHEDULE' in current and current.rfind('APPROVED_OWNER')<current.rfind('APPROVED_SCHEDULE'):
      sampled=True
      subprocess.run(['sample',str(rx.pid),'0.2','1','-file',str(case/'stall-sample.txt')],timeout=5)
     time.sleep(.02)
   code=rx.wait(timeout=25);text=(case/'receiver.log').read_text();assert code==0,(code,text)
   native_ids=[]
   for digest in dump.read_text().splitlines():
    assert re.fullmatch('[0-9a-f]{64}',digest) and digest in hashes
    native_ids.append(hashes[digest])
   audit=re.findall(r'NATIVE_OBSERVER locks=(\d+) pairs=(\d+) pending=0 exact=(\d+)',text)
   assert audit==[(str(2*len(native_ids)),str(len(native_ids)),str(len(native_ids)))],audit
   assert len(native_ids)>=90
   resets=[i for i in range(1,len(native_ids)) if native_ids[i]<=native_ids[i-1]]
   assert len(resets)==1 and native_ids[resets[0]]<native_ids[resets[0]-1], native_ids
   # Exactly two receive generations; strict increasing identity in each.
   native_generations=[native_ids[:resets[0]],native_ids[resets[0]:]]
   assert all(all(b==a+1 for a,b in zip(ids,ids[1:])) for ids in native_generations), native_generations
   summaries=re.findall(r'APPROVED_OWNER attempt=(\d+) num=(\d+) den=(\d+) compared=(\d+) distinct=(\d+)',text)
   assert len(summaries)==2 and all(int(x[3])==45 and int(x[4])>=35 for x in summaries)
   for info,dest in servers:
    assert json.loads((dest/'bound.json').read_text())['selected']=='hevc-10bit-422'
    for name in ['offer.sdp','answer.sdp']:
     candidates=re.findall(r'a=candidate:[^\r\n]+',(dest/name).read_text());assert candidates and all(' 127.0.0.1 ' in c for c in candidates)
   result=dict(rate=tag,native_reference_matches=len(native_ids),owner_reference_matches=90,owner_summaries=summaries,
    native_generations=native_generations,native_exact_memcmp=True,observer_bounded_records=4096,
    actual_vt_payload_sha256=rate['sha256'],same_source_owner_reconnect=True,physical_card=False,shipping_advertisement=False)
   (case/'result.json').write_text(json.dumps(result,indent=2));results.append(result);print(json.dumps(result),flush=True)
  finally:
   for p in reversed(children):
    if p.poll() is None:p.terminate()
   for p in children:
    try:p.wait(timeout=5)
    except subprocess.TimeoutExpired:p.kill();p.wait()
   for f in logs:f.close()
 if not os.environ.get('PV_APPROVED_CASE'):
  assert len(results)==4;(evidence/'whep-result.json').write_text(json.dumps(results,indent=2))
if __name__=='__main__':main()
