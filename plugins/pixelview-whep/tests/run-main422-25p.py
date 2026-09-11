#!/usr/bin/env python3
"""Normal-build 25p production SDP hook and strict rate policy; no app mutation."""
import os
from pathlib import Path
import subprocess
import native422_build
ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[1]
WORK=ROOT/'.test-build/main422-25p'
WORK.mkdir(parents=True,exist_ok=True)
SDK=REPO/'.deps/gstreamer-upstream-1.28.3/sdk'
APP=REPO/'build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app'
RUNTIME=APP/'Contents/PlugIns/pixelview-whep.plugin/Contents/Resources/GStreamer'
FW=APP/'Contents/Frameworks'
DEPS=sorted((REPO/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
cmd=['/usr/bin/clang','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-mmacosx-version-min=14.0']
cmd+=['-I'+str(p) for p in [REPO/'libobs',REPO/'build_macos_native422/config',DEPS/'include',SDK/'include/gstreamer-1.0',SDK/'include/glib-2.0',SDK/'lib/glib-2.0/include']]
cmd+=['-F'+str(FW),'-framework','libobs','-Wl,-rpath,'+str(FW),'-L'+str(RUNTIME/'lib'),'-Wl,-rpath,'+str(RUNTIME/'lib')]
libs=['-l'+x for x in ['gstwebrtc-1.0.0','gstsdp-1.0.0','gstapp-1.0.0','gstvideo-1.0.0','gstaudio-1.0.0','gstbase-1.0.0','gstreamer-1.0.0','gobject-2.0.0','glib-2.0.0']]
shared=[str(ROOT/p) for p in ['video-format.c','profile-offer.c','capability-probe.c']]+native422_build.flags(ROOT)+libs
# No feature macro or environment opt-in: exactly the normal-build policy.
env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_','PIXELVIEW_DEVELOPMENT_'))}
home=WORK/'test-home';home.mkdir(exist_ok=True)
env.update(HOME=str(home),CFFIXED_USER_HOME=str(home),GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(RUNTIME/'lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(RUNTIME/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(WORK/'registry.bin'))
exe=WORK/'production-offer'
subprocess.run(cmd+[str(ROOT/'tests/production-offer.c')]+shared+['-o',str(exe)],check=True)
subprocess.run([str(exe),'--deterministic-only'],env=env,check=True,timeout=30)
rate_exe=WORK/'native-rate'
subprocess.run(cmd+[str(ROOT/'tests/main422-25p.m')]+native422_build.libraries()+libs+['-o',str(rate_exe)],check=True)
subprocess.run([str(rate_exe)],env=env,check=True,timeout=30)
print('PASS normal-build production SDP and strict 25p filter policy; no GUI/hardware changes')
