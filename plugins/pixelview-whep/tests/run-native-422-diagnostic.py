#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import os, subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[3]; P=R/'plugins/pixelview-whep'
O=P/'.test-build/diagnostic-acceptance'; O.mkdir(exist_ok=True)
S=R/'.deps/gstreamer-upstream-1.28.3/sdk'; L=P/'.test-build/native-422/runtime/lib'
D=sorted((R/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
F=R/'build_macos/libobs/RelWithDebInfo'
E={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
E.update(HOME=str(O),CFFIXED_USER_HOME=str(O),DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',GST_PLUGIN_SCANNER=str(L.parent/'libexec/gst-plugin-scanner'),GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(L/'gstreamer-1.0'),GST_REGISTRY=str(O/'registry.bin'))
inc=['-I'+str(S/p) for p in ['include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include']]+['-I'+str(p) for p in [R/'libobs',R/'build_macos/config',D/'include']]
libs=['-L'+str(L),'-Wl,-rpath,'+str(L)]+['-l'+p+'-1.0.0' for p in ['gstapp','gstvideo','gstbase','gstreamer','gstcodecparsers','gstrtp','gstaudio']]+['-lglib-2.0.0','-lgobject-2.0.0','-F'+str(F),'-framework','libobs','-Wl,-rpath,'+str(F),'-Wl,-rpath,'+str(D/'lib')]
for f in ['Foundation','VideoToolbox','CoreMedia','CoreVideo']:libs+=['-framework',f]
subprocess.run(['xcrun','clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',*inc,str(P/'tests/native-422-diagnostic.c'),*[str(P/p) for p in ['video-format.c','profile-offer.c','capability-probe.c','native-422-filter.c','native-422.m']],*libs,'-o',str(O/'diagnostic')],env=E,check=True)
subprocess.run([str(O/'diagnostic')],env=E,check=True,timeout=20)
