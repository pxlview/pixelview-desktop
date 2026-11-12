#!/usr/bin/env python3
"""Lazy bundled GStreamer start: in-process plugin loading, no scanner, bounded waits.

Stages copies of the bundled runtime whose gst-plugin-scanner only sleeps (the
0.0.12 launch hang was GStreamer waiting on that helper), once as shipped and
once with an extra plugin whose init never returns.
"""
import os
import pathlib
import shutil
import subprocess
root=pathlib.Path(__file__).resolve().parents[1];repo=root.parents[1]
work=root/'.test-build'/'runtime-lazy'
shutil.rmtree(work,ignore_errors=True);work.mkdir(parents=True)
obs_build=pathlib.Path(os.environ.get('PIXELVIEW_TEST_BUILD',str(repo/'build_macos')))
framework=pathlib.Path(os.environ.get('OBS_FRAMEWORK_DIR',str(obs_build/'libobs/RelWithDebInfo')))
if not framework.exists():framework=obs_build/'frontend/RelWithDebInfo/Pixelview Desktop.app/Contents/Frameworks'
deps=sorted((repo/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
sdk=repo/'.deps/gstreamer-upstream-1.28.3/sdk'
runtime=repo/'.deps/pixelview-gstreamer-upstream'
inc=['-I'+str(sdk/p) for p in ('include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include')]
def link(lib):return ['-L'+str(lib),'-Wl,-rpath,'+str(lib),'-lgstreamer-1.0.0','-lgobject-2.0.0','-lglib-2.0.0']
cc=['clang','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-mmacosx-version-min=14.0']
def stage(name,hang):
    top=work/name;(top/'MacOS').mkdir(parents=True);(top/'Resources').mkdir()
    gst=top/'Resources/GStreamer';shutil.copytree(runtime,gst,symlinks=True)
    scanner=gst/'libexec/gst-plugin-scanner';scanner.unlink()
    scanner.write_text('#!/bin/sh\nexec sleep 600\n');scanner.chmod(0o755)
    # Link the staged copy only: two libgstreamer images in one process break GType.
    if hang:subprocess.run(cc+inc+link(gst/'lib')+['-dynamiclib',str(root/'tests/hang-plugin.c'),'-o',str(gst/'lib/gstreamer-1.0/libgstzzhang.dylib')],check=True)
    exe=top/'MacOS/runtime-lazy'
    subprocess.run(cc+['-I'+str(repo/'libobs'),'-I'+str(obs_build/'config'),'-I'+str(deps/'include')]+inc+
        ['-F'+str(framework),'-framework','libobs','-Wl,-rpath,'+str(framework),'-Wl,-rpath,'+str(deps/'lib'),str(root/'tests/runtime-lazy.c'),'-o',str(exe)]+link(gst/'lib'),check=True)
    return exe
env={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_'))}
for name,hang in (('ready',False),('hang',True)):
    exe=stage(name,hang)
    out=subprocess.run([str(exe),name],env=env,timeout=60,capture_output=True,text=True)
    print(out.stdout,end='');print(out.stderr,end='')
    assert out.returncode==0,out.returncode
    text=out.stdout+out.stderr
    assert 'gst-plugin-scanner' not in subprocess.run(['pgrep','-fl','gst-plugin-scanner'],capture_output=True,text=True).stdout
    if hang:assert 'still loading after' in text and '(at libgstzzhang.dylib)' in text,text
    else:assert 'GStreamer runtime ready in' in text,text
print('PASS lazy runtime: scanner unused, hung plugin named and bounded')
