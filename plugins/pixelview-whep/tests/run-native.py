#!/usr/bin/env python3
"""Native source lifecycle using an isolated official runtime; optional packaged module."""
import os
import pathlib
import shutil
import subprocess
import sys
root=pathlib.Path(__file__).resolve().parents[1];repo=root.parents[1]
work=root/'.test-build';work.mkdir(exist_ok=True)
obs_build=pathlib.Path(os.environ.get('PIXELVIEW_TEST_BUILD',str(repo/'build_macos')))
app=obs_build/'frontend/RelWithDebInfo/Pixelview Desktop.app'
framework=os.environ.get('OBS_FRAMEWORK_DIR',str(obs_build/'libobs/RelWithDebInfo'))
if not pathlib.Path(framework).exists():framework=str(app/'Contents/Frameworks')
deps=sorted((repo/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
sdk=repo/'.deps/gstreamer-upstream-1.28.3/sdk'
stage=work/'native-runtime'
if not stage.exists():shutil.copytree(repo/'.deps/pixelview-gstreamer-upstream',stage)
# The shipping closure excludes test sources. Add only these official SDK
# plugins to this private fixture runtime, resolving every dependency locally.
for name in ('libgstvideotestsrc.dylib','libgstaudiotestsrc.dylib'):
    dst=stage/'lib/gstreamer-1.0'/name
    if dst.exists():continue
    subprocess.run(['lipo',str(sdk/'lib/gstreamer-1.0'/name),'-thin','arm64','-output',str(dst)],check=True)
    lines=subprocess.check_output(['otool','-L',str(dst)],text=True).splitlines()[2:]
    for line in lines:
        ref=line.strip().split(' (')[0]
        if ref.startswith(('/usr/lib/','/System/Library/')):continue
        dep=pathlib.Path(ref).name
        assert (stage/'lib'/dep).exists(),dep
        subprocess.run(['install_name_tool','-change',ref,'@loader_path/../'+dep,str(dst)],check=True)
    subprocess.run(['codesign','--force','--sign','-',str(dst)],check=True,capture_output=True)
flags=['-I'+str(sdk/p) for p in ('include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include')]
flags+=['-L'+str(stage/'lib'),'-Wl,-rpath,'+str(stage/'lib')]
flags+=['-l'+x for x in ('gstapp-1.0.0','gstvideo-1.0.0','gstaudio-1.0.0','gstbase-1.0.0','gstreamer-1.0.0','gobject-2.0.0','glib-2.0.0')]
import native422_build
helpers=[str(root/p) for p in ('video-format.c','profile-offer.c','capability-probe.c')] + native422_build.flags(root)
base=['clang','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-mmacosx-version-min=14.0','-I'+str(repo/'libobs'),'-I'+str(obs_build/'config'),'-I'+str(deps/'include'),'-F'+framework,'-framework','libobs','-Wl,-rpath,'+framework,'-Wl,-rpath,'+str(deps/'lib')]
subprocess.run(base+helpers+[str(root/'tests/native.c'),'-o',str(work/'native')]+flags,check=True)
env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
env.update(GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(stage/'lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(stage/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(work/'native-registry.bin'))
subprocess.run([str(work/'native')],check=True,env=env,timeout=45)
if '--native-only' not in sys.argv:
    subprocess.run(base+[str(root/'tests/module.c'),'-o',str(work/'module')],check=True)
    plugin=obs_build/'plugins/pixelview-whep/RelWithDebInfo/pixelview-whep.plugin'
    if not plugin.exists():plugin=app/'Contents/PlugIns/pixelview-whep.plugin'
    subprocess.run(['codesign','--verify','--deep','--strict',str(plugin)],check=True)
    subprocess.run([str(work/'module'),str(plugin/'Contents/MacOS/pixelview-whep')],check=True,env={k:v for k,v in env.items() if not k.startswith(('GST_','DYLD_'))},timeout=15)
