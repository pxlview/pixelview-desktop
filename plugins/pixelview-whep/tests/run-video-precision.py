#!/usr/bin/env python3
"""Private runtime, no auth/capture/staging; real decode -> production callback -> main texture."""
import os
import native422_build
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
PLUGIN=ROOT/'plugins/pixelview-whep'
SDK=ROOT/'.deps/gstreamer-upstream-1.28.3/sdk'
FW=ROOT/'build_macos/libobs/RelWithDebInfo'
DEPS=sorted((ROOT/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
with tempfile.TemporaryDirectory(prefix='pv-receive-precision-') as tmp:
    work=Path(tmp); stage=work/'runtime'
    shutil.copytree(ROOT/'.deps/pixelview-gstreamer',stage)
    env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
    env.update(HOME=tmp,CFFIXED_USER_HOME=tmp,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',
        GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(stage/'lib/gstreamer-1.0'),
        GST_PLUGIN_SCANNER=str(stage/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(work/'registry.bin'))
    inc=['-I'+str(p) for p in [ROOT/'libobs',ROOT/'build_macos/config',DEPS/'include',SDK/'include/gstreamer-1.0',SDK/'include/glib-2.0',SDK/'lib/glib-2.0/include']]
    libs=['-F'+str(FW),'-framework','libobs','-Wl,-rpath,'+str(FW),'-Wl,-rpath,'+str(DEPS/'lib'),'-L'+str(stage/'lib'),'-Wl,-rpath,'+str(stage/'lib')]+['-l'+x for x in ['gstapp-1.0.0','gstaudio-1.0.0','gstvideo-1.0.0','gstbase-1.0.0','gstreamer-1.0.0','glib-2.0.0','gobject-2.0.0']]
    libs += native422_build.libraries()
    def run(cmd,**kw): return subprocess.run(cmd,check=True,env=env,**kw)
    run(['clang','-Wall','-Wextra','-Werror',*inc,str(PLUGIN/'tests/video-format.c'),str(PLUGIN/'video-format.c'),*libs,'-o',str(work/'formats')])
    run([str(work/'formats')])
    codec=work/'Codec.plugin/Contents'
    (codec/'MacOS').mkdir(parents=True); (codec/'Resources').mkdir()
    (codec/'Resources/GStreamer').symlink_to(stage)
    codec_exe=codec/'MacOS/codecs'
    run(['clang',*inc,str(PLUGIN/'tests/codecs.c'),str(PLUGIN/'video-format.c'),str(PLUGIN/'profile-offer.c'),str(PLUGIN/'capability-probe.c'),*native422_build.sources(PLUGIN),*libs,'-o',str(codec_exe)])
    run([str(codec_exe)],timeout=90)
    subprocess.run([str(codec_exe)],check=True,env={**env,"PIXELVIEW_TEST_P010":"1"},timeout=90)
    objects=[]
    for name in ['tests/callback-precision.c','video-format.c','profile-offer.c','capability-probe.c','native-422.m','native-422-filter.c']:
        obj=work/(Path(name).stem+'.o');objects.append(str(obj))
        run(['clang','-Wall','-Wextra','-Werror',*inc,'-c',str(PLUGIN/name),'-o',str(obj)])
    text=(ROOT/'frontend/widgets/OBSBasic_PixelviewReceive.inc').read_text()
    start=text.index('{',text.index('bool OBSBasic::SetPixelviewReceivePrecision'))
    end=start+1; depth=1
    while depth:
        depth += (text[end]=='{')-(text[end]=='}'); end+=1
    probe=work/'probe.mm'
    probe.write_text((PLUGIN/'tests/callback-precision.mm').read_text().replace('/* PRODUCTION_PRECISION_BODY */',text[start+1:end-1]))
    exe=work/'probe'
    run(['clang++','-std=c++17',*inc,str(probe),*objects,*libs,'-framework','Foundation','-o',str(exe)])
    import array
    y=array.array('H',[(64+x%877) for j in range(128) for x in range(1024)])
    uv=array.array('H',[512]*(1024*128//2))
    raw=work/'ramp.yuv';raw.write_bytes(y.tobytes()+uv.tobytes())
    fixture=work/'ramp.h265'
    run(['ffmpeg','-hide_banner','-loglevel','error','-y','-stream_loop','89','-f','rawvideo','-pixel_format','yuv420p10le','-video_size','1024x128','-framerate','30','-i',str(raw),'-frames:v','90','-c:v','libx265','-preset','ultrafast','-x265-params','lossless=1:log-level=error:pools=1:frame-threads=1:bframes=0:colorprim=bt709:transfer=bt709:colormatrix=bt709','-color_range','tv',str(fixture)],capture_output=True)
    for canvas in ['NV12','P010']:
        run([str(exe),str(ROOT/'libobs/data'),str(ROOT/'build_macos/libobs-opengl/RelWithDebInfo/libobs-opengl.dylib'),canvas,str(fixture)],timeout=25)
    full=work/'full.h265'
    run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pixel_format','yuv420p10le','-video_size','1024x128','-framerate','30','-i',str(raw),'-frames:v','1','-c:v','libx265','-preset','ultrafast','-x265-params','lossless=1:log-level=error:pools=1:frame-threads=1:bframes=0:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=full','-color_range','pc',str(full)],capture_output=True)
    subprocess.run([str(exe),str(ROOT/'libobs/data'),str(ROOT/'build_macos/libobs-opengl/RelWithDebInfo/libobs-opengl.dylib'),'P010',str(full)],check=True,env={**env,'PIXELVIEW_EXPECT_REJECT':'1'},timeout=25)
