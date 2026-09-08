#!/usr/bin/env python3
"""Native source and real packaged-module checks; no GUI or hardware opened."""
import os, pathlib, shlex, subprocess
root = pathlib.Path(__file__).resolve().parents[1]
repo = root.parents[1]
build = root / '.test-build'; build.mkdir(exist_ok=True)
obs_build = pathlib.Path(os.environ.get('PIXELVIEW_TEST_BUILD', str(repo/'build_macos')))
framework = os.environ.get('OBS_FRAMEWORK_DIR', str(obs_build/'libobs/RelWithDebInfo'))
obs_deps = sorted((repo/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
flags = shlex.split(subprocess.check_output(['/opt/homebrew/bin/pkg-config', '--cflags', '--libs', 'gstreamer-app-1.0', 'gstreamer-video-1.0', 'gstreamer-audio-1.0'], text=True))
base = ['clang', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-I'+str(repo/'libobs'), '-I'+str(obs_build/'config'), '-I'+str(obs_deps/'include'), '-F'+framework, '-framework', 'libobs', '-Wl,-rpath,'+framework, '-Wl,-rpath,'+str(obs_deps/'lib')]
subprocess.run(base+[str(root/'tests/native.c'),'-o',str(build/'native')]+flags,check=True)
env=dict(os.environ, GST_PLUGIN_PATH=str(repo/'.deps/gst-build-inputs'),GST_REGISTRY=str(build/'native-registry.bin'))
subprocess.run([str(build/'native')],check=True,env=env)
subprocess.run(base+[str(root/'tests/module.c'),'-o',str(build/'module')],check=True)
plugin=obs_build/'plugins/pixelview-whep/RelWithDebInfo/pixelview-whep.plugin'
subprocess.run(['codesign','--verify','--deep','--strict',str(plugin)],check=True)
subprocess.run([str(build/'module'),str(plugin/'Contents/MacOS/pixelview-whep')],check=True,env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))})
