#!/usr/bin/env python3
"""Actual production hooks and worker, offline isolated pinned runtime."""
import os
import sys
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[1]
SDK=REPO/'.deps/gstreamer-upstream-1.28.3/sdk'
RUNTIME=ROOT/'.test-build/decoder-profile/runtime'
WORK=ROOT/'.test-build/production-offer'
WORK.mkdir(parents=True,exist_ok=True);WORK.chmod(0o700)
FW=REPO/'build_macos/libobs/RelWithDebInfo'
DEPS=sorted((REPO/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
cmd=['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-mmacosx-version-min=14.0']
if '--asan' in sys.argv: cmd+=['-g','-fsanitize=address','-fno-omit-frame-pointer']
cmd+=['-I'+str(p) for p in [REPO/'libobs',REPO/'build_macos/config',DEPS/'include',SDK/'include/gstreamer-1.0',SDK/'include/glib-2.0',SDK/'lib/glib-2.0/include']]
cmd+=['-F'+str(FW),'-framework','libobs','-Wl,-rpath,'+str(FW),'-Wl,-rpath,'+str(DEPS/'lib'),'-L'+str(RUNTIME/'lib'),'-Wl,-rpath,'+str(RUNTIME/'lib')]
import native422_build
cmd+=[str(ROOT/p) for p in ['tests/production-offer.c','video-format.c','profile-offer.c','capability-probe.c']] + native422_build.flags(ROOT)
cmd+=['-l'+x for x in ['gstwebrtc-1.0.0','gstsdp-1.0.0','gstapp-1.0.0','gstvideo-1.0.0','gstaudio-1.0.0','gstbase-1.0.0','gstreamer-1.0.0','gobject-2.0.0','glib-2.0.0']]
exe=WORK/'production-offer'
subprocess.run(cmd+['-o',str(exe)],check=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
env.update(GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(RUNTIME/'lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(RUNTIME/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(WORK/'registry.bin'))
if '--worker-only' not in sys.argv:
    args=['--deterministic-only'] if '--deterministic-only' in sys.argv else []
    subprocess.run([str(exe),*args],env=env,check=True,timeout=30)
# No hardware decoder probes (including the cancellation worker) in this mode.
if '--deterministic-only' in sys.argv:
    sys.exit(0)
worker=WORK/'worker-capability'
worker_cmd=[str(ROOT/'tests/worker-capability.c') if x==str(ROOT/'tests/production-offer.c') else x for x in cmd]
subprocess.run(worker_cmd+['-DPIXELVIEW_CAPABILITY_TESTING','-o',str(worker)],check=True)
for mode in ['absent','cancel','timeout']:
    subprocess.run([str(worker),mode],env=env,check=True,timeout=10)
