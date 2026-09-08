#!/usr/bin/env python3
"""Exercise bundled VideoToolbox H264/HEVC + Opus through native OBS callbacks."""
import os, pathlib, shlex, shutil, subprocess
root=pathlib.Path(__file__).resolve().parents[1]; repo=root.parents[1]
build=pathlib.Path(os.environ.get('PIXELVIEW_TEST_BUILD',str(repo/'build_macos')))
plugin=build/'plugins/pixelview-whep/RelWithDebInfo/pixelview-whep.plugin'
contents=root/'.test-build/Codec.plugin/Contents'
(contents/'MacOS').mkdir(parents=True,exist_ok=True);(contents/'Resources').mkdir(exist_ok=True)
stage=root/'.test-build/codec-runtime'
shutil.copytree(plugin/'Contents/Resources/GStreamer',stage,dirs_exist_ok=True)
link=contents/'Resources/GStreamer'
if link.is_symlink(): link.unlink()
link.symlink_to(stage)
framework=build/'libobs/RelWithDebInfo'; deps=sorted((repo/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
flags=shlex.split(subprocess.check_output(['/opt/homebrew/bin/pkg-config','--cflags','gstreamer-app-1.0','gstreamer-video-1.0','gstreamer-audio-1.0'],text=True))
cmd=['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(repo/'libobs'),'-I'+str(build/'config'),'-I'+str(deps/'include'),'-F'+str(framework),'-framework','libobs','-Wl,-rpath,'+str(framework),'-Wl,-rpath,'+str(deps/'lib'),*flags,str(root/'tests/codecs.c'),'-o',str(contents/'MacOS/codecs'),'-L'+str(stage/'lib'),'-lgstapp-1.0.0','-lgstvideo-1.0.0','-lgstaudio-1.0.0','-lgstreamer-1.0.0','-lgobject-2.0.0','-lglib-2.0.0','-Wl,-rpath,'+str(stage/'lib')]
subprocess.run(cmd,check=True)
subprocess.run([str(contents/'MacOS/codecs')],check=True,env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))})
