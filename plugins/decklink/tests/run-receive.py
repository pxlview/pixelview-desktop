#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile real DeckLink owner code with SDK fakes; never link SDK dispatch."""
from pathlib import Path
import os, re, subprocess
ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'plugins/decklink/.test-build/receive'
OUT.mkdir(parents=True, exist_ok=True)
os.environ['DEVELOPER_DIR'] = '/Applications/Xcode.app/Contents/Developer'
os.environ['HOME'] = os.environ['CFFIXED_USER_HOME'] = str(OUT)
# Generate default refusing implementations from the actual bundled SDK declarations.
# Tests override only real SDK methods. No production owner method is extracted/replaced.
headers = '\n'.join((ROOT / ('plugins/decklink/mac/decklink-sdk/' + h)).read_text() for h in ['DeckLinkAPI.h', 'DeckLinkAPIModes.h', 'DeckLinkAPIConfiguration.h', 'DeckLinkAPIDiscovery.h'])
classes = []
for name in ['IDeckLinkOutput', 'IDeckLinkMutableVideoFrame', 'IDeckLinkDisplayMode', 'IDeckLinkConfiguration', 'IDeckLinkProfileAttributes', 'IDeckLinkKeyer', 'IDeckLinkDisplayModeIterator', 'IDeckLink']:
    body = re.search(r'class BMD_PUBLIC ' + name + r' : public \w+\s*\{(.*?)\n\};', headers, re.S).group(1)
    methods = []
    # Mutable frame inherits video frame's pure methods.
    if name == 'IDeckLinkMutableVideoFrame':
        body += re.search(r'class BMD_PUBLIC IDeckLinkVideoFrame : public IUnknown\s*\{(.*?)\n\};', headers, re.S).group(1)
    for ret, method, args in re.findall(r'virtual\s+(\w+)\s+(\w+)\s*\((.*?)\)\s*=\s*0;', body, re.S):
        args = re.sub(r'/\*.*?\*/', '', args, flags=re.S)
        methods.append(f'{ret} {method}({args}) override {{ return ' + ('E_NOTIMPL' if ret == 'HRESULT' else '{}') + '; }')
    classes.append(f'class Stub{name} : public {name} {{ public: HRESULT QueryInterface(REFIID, void **p) override {{ *p=nullptr; return E_NOINTERFACE; }} ULONG AddRef() override {{ return 1; }} ULONG Release() override {{ return 1; }} ' + '\n'.join(methods) + '\n};')
(OUT/'sdk-stubs.hpp').write_text('#pragma once\n#include "platform.hpp"\n' + '\n'.join(classes))
BUILD = Path(os.environ.get('PIXELVIEW_TEST_BUILD', ROOT/'build_macos'))
app = BUILD/'frontend/RelWithDebInfo/Pixelview Desktop.app'
frameworks = app/'Contents/Frameworks'
sdk = subprocess.check_output(['xcrun','--show-sdk-path'], text=True).strip()
deps = sorted((ROOT/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
cmd = ['xcrun','clang++','-std=c++17','-g','-O2' if os.environ.get('PV_DECKLINK_OPTIMIZE') == '1' else '-O1','-Wall','-Wextra','-Wno-unused-parameter', '-Wno-multichar', '-isysroot', sdk, '-mmacosx-version-min=14.0', '-I'+str(OUT), '-I'+str(ROOT/'libobs'), '-I'+str(ROOT/'plugins/decklink'), '-I'+str(ROOT/'deps/libcaption'), '-I'+str(BUILD/'config'), '-I'+str(deps/'include'), '-F'+str(frameworks), '-framework','libobs', '-framework','CoreFoundation', '-Wl,-rpath,'+str(frameworks), '-Wl,-dead_strip']
# Compile whole production translation units, not a parallel fake owner.
files = ['decklink-output.cpp','decklink-devices.cpp','decklink-device-instance.cpp','decklink-device.cpp','decklink-device-mode.cpp','DecklinkOutput.cpp','DecklinkInput.cpp','DecklinkBase.cpp','decklink-device-discovery.cpp','OBSVideoFrame.cpp','util.cpp','mac/platform.cpp']
if os.environ.get('PV_DECKLINK_SANITIZE') == '1': cmd += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cflags = [x for x in cmd[2:cmd.index('-framework')] if not x.startswith('-std=')]
if os.environ.get('PV_DECKLINK_SANITIZE') == '1': cflags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(['xcrun','clang', '-c', str(ROOT/'plugins/decklink/audio-repack.c'), '-o', str(OUT/'audio-repack.o')] + cflags, check=True)
base = cmd[:]
cmd += [str(OUT/'audio-repack.o'), str(BUILD/'deps/libcaption/RelWithDebInfo/libcaption.a')]
# Compile the actual manual Start readiness expression, not a copied policy.
ui_source = (ROOT/'plugins/decklink-output-ui/decklink-ui-main.cpp').read_text()
manual = re.search(r'bool ready = (.*?);', ui_source, re.S).group(0)
assert ui_source.index(manual) < ui_source.index('obs_output_start(context.output)')
media = re.search(r'obs_output_set_media\(context.output,.*?;', ui_source, re.S).group(0)
(OUT/'rendered-media.inc').write_text('static void bind_rendered_media(obs_output_t *output, video_t *video) { struct { obs_output_t *output; video_t *video_queue; } context={output,video}; ' + media + ' }')
(OUT/'manual-ready.inc').write_text('static bool manual_ready(obs_source_t *selected) { calldata_t cd; calldata_init(&cd); ' + manual + ' calldata_free(&cd); return ready; }')
qt = ROOT/'.deps/obs-deps-qt6-2026-05-21-universal/lib'
subprocess.run(base[:2]+['-F'+str(qt)]+base[2:]+['-include','arm_acle.h','-I'+str(qt/'QtCore.framework/Headers'),'-F'+str(qt),'-framework','QtCore','-Wl,-rpath,'+str(qt),str(ROOT/'plugins/decklink/tests/receive-ui.cpp'),'-o',str(OUT/'receive-ui')],check=True)
subprocess.run([str(OUT/'receive-ui')],check=True,timeout=20)
cmd += [str(ROOT/'plugins/decklink'/f) for f in files] + [str(ROOT/'plugins/decklink/tests/receive.cpp'), '-o', str(OUT/'receive')]
(OUT/'command.txt').write_text(' '.join(cmd))
subprocess.run(cmd, check=True)
subprocess.run([str(OUT/'receive'),str(frameworks/'libobs-opengl.dylib'),str(ROOT/'libobs/data')], check=True, timeout=30)
