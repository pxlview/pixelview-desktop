#!/usr/bin/env python3
"""Bounded HD decode samples using the already isolated signed VT harness.
Not a level conformance, sustained throughput, RTP or OBS output test.
"""
import json, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'.test-build/decoder-profile'
OUT=ROOT/'.test-build/profile-offer/hd-decode'
OUT.mkdir(parents=True,exist_ok=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
env.update(GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(WORK/'runtime/lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(WORK/'runtime/libexec/gst-plugin-scanner'),GST_REGISTRY=str(WORK/'registry.bin'),DYLD_INSERT_LIBRARIES=str(WORK/'decoder-session-observer.dylib'))
rows=[]
for name,pix,codec,profile in [('main','yuv420p','hevc',0),('main10','yuv420p10le','hevc',0),('vp90','yuv420p','vp9',0),('vp92','yuv420p10le','vp9',2)]:
 f=OUT/(name+('.h265' if codec=='hevc' else '.ivf'))
 cmd=['ffmpeg','-v','error','-y','-f','lavfi','-i',f'testsrc2=size=1920x1080:rate=60,format={pix}','-frames:v','24']
 if codec=='hevc':cmd+=['-c:v','libx265','-preset','ultrafast','-x265-params','log-level=error:pools=1:frame-threads=1:bframes=0:level-idc=4.1:high-tier=0']
 else:cmd+=['-c:v','libvpx-vp9','-profile:v',str(profile),'-deadline','realtime','-cpu-used','8','-threads','2','-lag-in-frames','0']
 subprocess.run(cmd+[str(f)],check=True,capture_output=True,timeout=90)
 probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(f)]))
 r=subprocess.run([str(WORK/'decoder-profiles'),str(f),codec,'vtdec_hw','P010_10LE' if '10' in pix else 'auto'],env=env,text=True,capture_output=True,timeout=30)
 row=dict(name=name,probe=probe,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr);rows.append(row)
 (OUT/'results.json').write_text(json.dumps(rows,indent=2))
 assert r.returncode==0 and 'width=(int)1920' in r.stdout and 'hardware=true' in r.stderr,row
 print(name,r.stdout.strip(),r.stderr.strip())
print('HD sample results:',OUT/'results.json')
