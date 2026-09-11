#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import os,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[3];P=R/'plugins/pixelview-whep';O=P/'.test-build/finite-rate';O.mkdir(exist_ok=True)
S=R/'.deps/gstreamer-upstream-1.28.3/sdk';L=P/'.test-build/native-422/runtime/lib'
E={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
E.update(HOME=str(O),CFFIXED_USER_HOME=str(O),DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',GST_PLUGIN_SCANNER=str(L.parent/'libexec/gst-plugin-scanner'),GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(L/'gstreamer-1.0'),GST_REGISTRY=str(O/'registry.bin'))
flags=['-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
inc=['-I'+str(S/p) for p in ['include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include']]
libs=['-L'+str(L),'-Wl,-rpath,'+str(L)]+['-l'+p for p in ['gstapp-1.0.0','gstvideo-1.0.0','gstcodecparsers-1.0.0','gstreamer-1.0.0','glib-2.0.0','gobject-2.0.0','gstrtp-1.0.0']]+['-framework', 'Foundation','-framework','VideoToolbox','-framework','CoreMedia','-framework','CoreVideo']
for name,sources in [('timestamps',['native-422-timestamps.m']),('deadline',['native-422-deadline.m']),('rate',['native-422-rate.c']),('policy',['native-422-policy.m']),('rtp',['native-422-rtp.c','../native-422.m'])]:
 subprocess.run(['xcrun','clang',*flags,*inc,*[str(P/'tests'/s) for s in sources],*libs,'-o',str(O/name)],env=E,check=True)
 subprocess.run([str(O/name)]+([str(P/'.test-build/obs-vt-approved/24-1.hevc')] if name=='timestamps' else []),env=E,check=True,timeout=30)
print('PASS finite-rate and strict metadata/filler policy ASan+UBSan')
