#!/usr/bin/env python3
"""Live bundled production pipeline; private credentials/logs, safe summaries."""
import json, os, pathlib, re, shlex, shutil, subprocess
root=pathlib.Path(__file__).resolve().parents[1]; repo=root.parents[1]
build=pathlib.Path(os.environ.get('PIXELVIEW_TEST_BUILD',str(repo/'build_macos')))
plugin=build/'plugins/pixelview-whep/RelWithDebInfo/pixelview-whep.plugin'
if not plugin.exists(): plugin=build/'frontend/RelWithDebInfo/Pixelview Desktop.app/Contents/PlugIns/pixelview-whep.plugin'
contents=root/'.test-build/Live.plugin/Contents'
(contents/'MacOS').mkdir(parents=True,exist_ok=True); (contents/'Resources').mkdir(exist_ok=True)
stage=root/'.test-build/live-runtime'
if not stage.exists(): shutil.copytree(plugin/'Contents/Resources/GStreamer',stage)
link=contents/'Resources/GStreamer'
if not link.exists(): link.symlink_to(stage)
framework=build/'libobs/RelWithDebInfo'; deps=sorted((repo/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
sdk=repo/'.deps/gstreamer-upstream-1.28.3/sdk'
flags=['-I'+str(sdk/p) for p in ('include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include')]
import native422_build
helpers=[str(root/p) for p in ('video-format.c','profile-offer.c','capability-probe.c')] + native422_build.flags(root)
cmd=['clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(repo/'libobs'),'-I'+str(build/'config'),'-I'+str(deps/'include'),'-F'+str(framework),'-framework','libobs','-Wl,-rpath,'+str(framework),'-Wl,-rpath,'+str(deps/'lib'),*flags,*helpers,str(root/'tests/live-source.c'),'-o',str(contents/'MacOS/live-source'),'-L'+str(stage/'lib'),'-lgstapp-1.0.0','-lgstvideo-1.0.0','-lgstaudio-1.0.0','-lgstbase-1.0.0','-lgstreamer-1.0.0','-lgobject-2.0.0','-lglib-2.0.0','-Wl,-rpath,'+str(stage/'lib')]
subprocess.run(cmd,check=True)
private=json.loads(pathlib.Path(os.environ.get('PIXELVIEW_LIVE_PRIVATE','/Users/max/src/pixelview-whep-spike/runtime/viewer-private.json')).read_text())
log=root/'.test-build/live-private.log'
fd=os.open(log,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600); os.chmod(log,0o600)
with os.fdopen(fd,'w') as out:
 result=subprocess.run([str(contents/'MacOS/live-source')],input=private['whep_url']+'\n',text=True,stdout=out,stderr=subprocess.STDOUT,timeout=60,env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))})
text=log.read_text()
for value in private.values():
 if isinstance(value,str) and len(value)>5: text=text.replace(value,'[PRIVATE]')
text=re.sub(r'https?://[^\s\"\)]+','[PRIVATE_URL]',text)
for line in text.splitlines():
 if any(key in line for key in ('BUS_ERROR','LIVE_RESULT','Signalling error:','CODEC_CAPS')): print(line)
print('live exit:',result.returncode,'private diagnostic:',log)
raise SystemExit(result.returncode)
