#!/usr/bin/env python3
"""Lazy bundled GStreamer start: in-process plugin loading, no scanner, bounded waits.

Stages copies of the shipping runtime (.deps/pixelview-gstreamer) whose
gst-plugin-scanner only sleeps (the 0.0.12 launch hang was the app and that
helper deadlocked on their pipes), then: as shipped; with an extra plugin
whose init never returns; with one that finishes after the load budget; with
a truncated bundled plugin; and with a symlinked plugin.
"""
import os
import pathlib
import re
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
runtime=repo/'.deps/pixelview-gstreamer'
inc=['-I'+str(sdk/p) for p in ('include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include')]
# Link the staged copy only: two libgstreamer images in one process break GType.
def link(lib):return ['-L'+str(lib),'-Wl,-rpath,'+str(lib),'-lgstreamer-1.0.0','-lgobject-2.0.0','-lglib-2.0.0']
cc=['clang','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-mmacosx-version-min=14.0']
BUDGET=4000
def stage(name,extra=None,defines=()):
    top=work/name;(top/'MacOS').mkdir(parents=True);(top/'Resources').mkdir()
    gst=top/'Resources/GStreamer';shutil.copytree(runtime,gst,symlinks=True)
    scanner=gst/'libexec/gst-plugin-scanner';scanner.unlink()
    scanner.write_text('#!/bin/sh\nexec sleep 600\n');scanner.chmod(0o755)
    plugins=gst/'lib/gstreamer-1.0'
    if extra=='hang':subprocess.run(cc+inc+link(gst/'lib')+['-dynamiclib',str(root/'tests/hang-plugin.c'),'-o',str(plugins/'libgstzzhang.dylib')],check=True)
    if extra=='corrupt':
        data=(plugins/'libgstdtls.dylib').read_bytes();(plugins/'libgstdtls.dylib').unlink()
        (plugins/'libgstdtls.dylib').write_bytes(data[:len(data)//3])
    if extra=='symlink':(plugins/'libgstzzlink.dylib').symlink_to(runtime/'lib/gstreamer-1.0/libgstudp.dylib')
    exe=top/'MacOS/runtime-lazy'
    subprocess.run(cc+list(defines)+['-I'+str(repo/'libobs'),'-I'+str(obs_build/'config'),'-I'+str(deps/'include')]+inc+
        ['-F'+str(framework),'-framework','libobs','-Wl,-rpath,'+str(framework),'-Wl,-rpath,'+str(deps/'lib'),str(root/'tests/runtime-lazy.c'),'-o',str(exe)]+link(gst/'lib'),check=True)
    return exe
base={k:v for k,v in os.environ.items() if not k.startswith(('GST_','DYLD_','PV_TEST_'))}
cases=[('ready','ready',None,{},['GStreamer runtime ready in']),
       ('hang','hang','hang',{},['still loading after','(at libgstzzhang.dylib); receiving is unavailable until it finishes']),
       ('slow','slow','hang',{'PV_TEST_HANG_SECONDS':'8'},['receiving is unavailable until it finishes','GStreamer runtime ready in','libgstzzhang.dylib took']),
       ('corrupt','failed','corrupt',{},['bundled plugin libgstdtls.dylib did not load']),
       ('symlink','failed','symlink',{},['bundled plugin libgstzzlink.dylib is not a regular file'])]
for name,mode,extra,env,expect in cases:
    exe=stage(name,extra,['-DPIXELVIEW_GST_BUDGET_MS=%du'%BUDGET] if mode in ('hang','slow') else [])
    out=subprocess.run([str(exe),mode],env={**base,**env},timeout=120,capture_output=True,text=True)
    text=out.stdout+out.stderr
    print(''.join(l+'\n' for l in text.splitlines() if l.startswith('PASS') or 'runtime ready' in l or 'runtime failed' in l),end='')
    assert out.returncode==0,(name,out.returncode,text[-3000:])
    for needle in expect:assert needle in text,(name,needle)
    # Only the plugin's own OBS log lines reach log sharing (GLib warnings go to stderr).
    ours=[l for l in text.splitlines() if re.match(r'(debug|info|warning|error): \[pixelview-whep\]',l)]
    assert ours and not any(str(work) in l or '/Users/' in l for l in ours),(name,'absolute path in log')
    assert not subprocess.run(['pgrep','-f','gst-plugin-scanner'],capture_output=True).stdout,(name,'scanner ran')
print('PASS lazy runtime: scanner unused; hang and late finish bounded; bad plugins fail; no paths logged')
