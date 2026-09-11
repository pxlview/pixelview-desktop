#!/usr/bin/env python3
"""Deterministic production worker scheduling; no GUI, network or decoder use."""
import os
from pathlib import Path
import subprocess
import sys
import native422_build
ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / 'plugins/pixelview-whep'
WORK = PLUGIN / '.test-build/preview-dispatch'
WORK.mkdir(parents=True, exist_ok=True)
SDK = ROOT / '.deps/gstreamer-upstream-1.28.3/sdk'
STAGE = PLUGIN / '.test-build/native-422/runtime'
FW = ROOT / 'build_macos/libobs/RelWithDebInfo'
DEPS = sorted((ROOT / '.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
ENV = {k:v for k,v in os.environ.items() if not k.startswith(('GST_', 'DYLD_'))}
ENV.update(HOME=str(WORK), CFFIXED_USER_HOME=str(WORK), DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',
 GST_PLUGIN_SYSTEM_PATH_1_0='', GST_PLUGIN_PATH_1_0=str(STAGE/'lib/gstreamer-1.0'),
 GST_PLUGIN_SCANNER=str(STAGE/'libexec/gst-plugin-scanner'), GST_REGISTRY=str(WORK/'registry.bin'))
inc = ['-I'+str(SDK/p) for p in ['include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include']]
inc += ['-I'+str(p) for p in [ROOT/'libobs',ROOT/'build_macos/config',DEPS/'include']]
libs = ['-L'+str(STAGE/'lib'),'-Wl,-rpath,'+str(STAGE/'lib')]
libs += ['-l'+x+'-1.0.0' for x in ['gstapp','gstvideo','gstaudio','gstbase','gstreamer']]
libs += ['-lglib-2.0.0','-lgobject-2.0.0','-F'+str(FW),'-framework','libobs','-Wl,-rpath,'+str(FW),'-Wl,-rpath,'+str(DEPS/'lib')]
exe = WORK/'preview-dispatch'
flags = [] if '--red' in sys.argv else ['-DTEST_DISPATCH_RESERVATION']
cmd = ['xcrun','clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-g',*flags,*inc,
 str(PLUGIN/'tests/preview-dispatch.c'),*[str(PLUGIN/p) for p in ['video-format.c','profile-offer.c','capability-probe.c']],
 *native422_build.flags(PLUGIN),*libs,'-o',str(exe)]
subprocess.run(cmd,env=ENV,check=True,timeout=90)
subprocess.run([str(exe)],env=ENV,check=True,timeout=40)
