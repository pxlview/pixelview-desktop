#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Declared-level HD fixture through production native decoder, not admission."""
import array, hashlib, json, os, re, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];PLUGIN=ROOT/'plugins/pixelview-whep'
OUT=PLUGIN/'.test-build/native-422-envelope';OUT.mkdir(parents=True,exist_ok=True)
NATIVE=PLUGIN/'.test-build/native-422';SDK=ROOT/'.deps/gstreamer-upstream-1.28.3/sdk';runtime=NATIVE/'runtime'
ENV={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
ENV.update(HOME=str(OUT),CFFIXED_USER_HOME=str(OUT),DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(runtime/'lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(runtime/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(OUT/'registry.bin'))
commands=[]
def run(cmd,**kw):
    commands.append([str(x) for x in cmd]);(OUT/'commands.json').write_text(json.dumps(commands,indent=2))
    return subprocess.run(cmd,check=True,env=ENV,timeout=120,**kw)
(OUT/'result.json').unlink(missing_ok=True)
raw=OUT/'input.yuv';data=array.array('H')
for f in range(3):
    data.extend(64+(x+f*7)%877 for y in range(1080) for x in range(1920))
    for c in range(2):data.extend(64+((x*(13+4*c)+f*23) if y%2 else 896-x*(13+4*c)-f*23)%897 for y in range(1080) for x in range(960))
raw.write_bytes(data.tobytes())
tags=['-color_range','tv','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709']
fixture=OUT/'hd2997-level120.hevc'
run(['ffmpeg','-v','error','-y','-f','rawvideo','-pixel_format','yuv422p10le','-video_size','1920x1080','-framerate','30000/1001',*tags,'-i',str(raw),'-frames:v','3','-c:v','libx265','-preset','ultrafast','-profile:v','main422-10','-x265-params','crf=18:level-idc=4:high-tier=0:vbv-maxrate=12000:vbv-bufsize=12000:bframes=0:keyint=30:log-level=error:pools=1:frame-threads=1:colorprim=bt709:transfer=bt709:colormatrix=bt709',*tags,str(fixture)],capture_output=True)
probe=json.loads(run(['ffprobe','-v','error','-show_streams','-of','json',str(fixture)],capture_output=True,text=True).stdout)['streams'][0]
for key,value in {'width':1920,'height':1080,'level':120,'pix_fmt':'yuv422p10le','r_frame_rate':'30000/1001','color_range':'tv','color_space':'bt709','color_transfer':'bt709','color_primaries':'bt709'}.items():assert probe[key]==value,(key,probe)
trace=run(['ffmpeg','-v','verbose','-i',str(fixture),'-c','copy','-bsf:v','trace_headers','-f','null','-'],capture_output=True,text=True).stderr
(OUT/'headers.log').write_text(trace)
for key,value in {'general_profile_idc':4,'general_level_idc':120,'general_tier_flag':0,'chroma_format_idc':2,'bit_depth_luma_minus8':2,'bit_depth_chroma_minus8':2,'vui_num_units_in_tick':1001,'vui_time_scale':30000}.items():
    values=re.findall(r'\b'+key+r'\s+[01]+\s+=\s+(\d+)',trace);assert values and all(int(x)==value for x in values),(key,values)
inc=['-I'+str(SDK/p) for p in ['include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include']]
libs=['-L'+str(runtime/'lib'),'-Wl,-rpath,'+str(runtime/'lib')]+['-l'+x+'-1.0.0' for x in ['gstapp','gstvideo','gstbase','gstreamer','gstcodecparsers']]+['-lglib-2.0.0','-lgobject-2.0.0']
run(['xcrun','clang','-Wall','-Wextra','-Werror','-DGST_USE_UNSTABLE_API',*inc,str(PLUGIN/'tests/native-422.c'),str(PLUGIN/'native-422.m'),*libs,'-framework','Foundation','-framework','VideoToolbox','-framework','CoreMedia','-framework','CoreVideo','-o',str(OUT/'decode')])
dump=OUT/'native.yuv';dump.unlink(missing_ok=True)
ENV.update(DYLD_INSERT_LIBRARIES=str(NATIVE/'native-422-observer.dylib'),PV_NATIVE422_X422_DUMP=str(dump),PV_NATIVE422_OBSERVER_EXECUTABLE=str(OUT/'decode'))
run([str(OUT/'decode'),str(fixture),str(OUT/'output.v210'),'accept','1920','1080'])
ENV.pop('DYLD_INSERT_LIBRARIES');ENV.pop('PV_NATIVE422_X422_DUMP')
run(['ffmpeg','-v','error','-y','-c:v','hevc','-i',str(fixture),'-pix_fmt','yuv422p10le','-f','rawvideo',str(OUT/'reference.yuv')])
run(['ffmpeg','-v','error','-y','-f','v210','-video_size','1920x1080','-i',str(OUT/'output.v210'),'-pix_fmt','yuv422p10le','-f','rawvideo',str(OUT/'unpacked.yuv')])
reference=(OUT/'reference.yuv').read_bytes();assert len(reference)==1920*1080*4*3
assert dump.read_bytes()==reference,'native x422 differs from independently decoded compressed reference'
assert (OUT/'unpacked.yuv').read_bytes()==reference,'v210 differs from independently decoded compressed reference'
result={'passed':True,'frames':3,'width':1920,'height':1080,'fps_num':30000,'fps_den':1001,'hevc_level_id':120,'tier':0,'profile':4,'native_x422_exact':True,'v210_exact':True,'raw_source_lossless':reference==raw.read_bytes(),'shipping_admission':False,'throughput_certified':False,'sha256':hashlib.sha256(fixture.read_bytes()).hexdigest()}
(OUT/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
