#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import os, subprocess, sys
from pathlib import Path
R=Path(__file__).resolve().parents[3];P=R/'plugins/pixelview-whep';O=P/'.test-build/finite-rate'
S=R/'.deps/gstreamer-upstream-1.28.3/sdk';L=P/'.test-build/native-422/runtime/lib'
E={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
E.update(HOME=str(O),CFFIXED_USER_HOME=str(O),DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',GST_PLUGIN_SCANNER=str(L.parent/'libexec/gst-plugin-scanner'),GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(L/'gstreamer-1.0'),GST_REGISTRY=str(O/'headers-registry.bin'))
inc=['-I'+str(S/p) for p in ['include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include']]
libs=['-L'+str(L),'-Wl,-rpath,'+str(L)]+['-l'+p for p in ['gstvideo-1.0.0','gstapp-1.0.0','gstcodecparsers-1.0.0','gstreamer-1.0.0','glib-2.0.0','gobject-2.0.0']]+['-framework','Foundation','-framework','VideoToolbox','-framework','CoreMedia','-framework','CoreVideo']
subprocess.run(['xcrun','clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*inc,str(P/'tests/native-422-headers.m'),*libs,'-o',str(O/'headers')],env=E,check=True)
cases=['valid','caps','hvcc','allow-hvcc:6:20','allow-hvcc:2:08','allow-caps:colorimetry=2:3:5:1']
# Mutate actual Apple headers, preserving EBSP escaping and NAL lengths.
for prefix,shift in [('hvcc',1),('vps',0),('sps',0)]:
 for index,mask in [(0,0x20),(0,0x40),(5,0x08),(5,0x04),(5,0x02),(5,0x01),
                    (6,0x80),(6,0x40),(6,0x20),(6,0x10),(6,0x08),(7,1),(10,2),(11,1)]:
  cases.append(f'{prefix}:{index+shift}:{mask:02x}')
# Summary flags cannot claim compatibility or constraints absent from a PS.
cases += ['hvcc:2:40','vps:5:20','sps:5:20','hvcc:19:01','hvcc:20:01',
          'allow-hvcc:19:18','ints:bit-depth-luma=8','ints:bit-depth-chroma=12',
          'caps:bit-depth-luma=10','allow-ints:bit-depth-luma=10']
for index,mask in [(13,0x80),(15,0x80),(16,0x80),(17,0x80),(18,0x80),(16,1),(17,1),(18,1),(21,0x10)]:
 cases.append(f'hvcc:{index}:{mask:02x}')
for key,value in [('profile','main-10'),('tier','high'),('level','5'),('chroma-format','4:2:0'),('interlace-mode','interleaved'),('colorimetry','bt2020')]:
 cases.append(f'caps:{key}={value}')
cases += ['hvcc:23:40','hvcc:29:02','hvcc:30:04','hvcc:30:08']
for case in sys.argv[1:] or cases:
 subprocess.run([str(O/'headers'),str(P/'.test-build/obs-vt-approved/24-1.hevc'),case],env=E,check=True,timeout=20)
