#!/usr/bin/env python3
"""Compile production filter routing without network, GUI or decoder sessions."""
from pathlib import Path
import os,subprocess
P=Path(__file__).resolve().parents[1];R=P.parents[1]
S=R/'.deps/gstreamer-upstream-1.28.3/sdk'
T=Path(os.environ.get('PIXELVIEW_TEST_RUNTIME',R/'.deps/pixelview-gstreamer'))
W=P/'.test-build/ordinary-route';W.mkdir(parents=True,exist_ok=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
env.update(DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(T/'lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(T/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(W/'registry.bin'))
cmd=['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-g']
cmd+=['-I'+str(S/p) for p in ['include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include']]
cmd += [str(P/p) for p in ['tests/ordinary-route.c','native-422-filter.c','native-422.m']]
cmd += ['-L'+str(T/'lib'),'-Wl,-rpath,'+str(T/'lib')]
cmd += ['-l'+p for p in ['gstcodecparsers-1.0.0','gstapp-1.0.0','gstvideo-1.0.0','gstbase-1.0.0','gstrtp-1.0.0','gstreamer-1.0.0','glib-2.0.0','gobject-2.0.0']]
for f in ['Foundation','CoreMedia','CoreVideo','VideoToolbox']:cmd+=['-framework',f]
subprocess.run(cmd+['-o',str(W/'ordinary-route')],check=True,env=env,timeout=90)
subprocess.run([str(W/'ordinary-route')],check=True,env=env,timeout=20)
