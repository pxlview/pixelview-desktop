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
app = ROOT/'build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app'
frameworks = app/'Contents/Frameworks'
sdk = subprocess.check_output(['xcrun','--show-sdk-path'], text=True).strip()
cmd = ['xcrun','clang++','-std=c++17','-g','-O2' if os.environ.get('PV_DECKLINK_OPTIMIZE') == '1' else '-O1','-Wall','-Wextra','-Wno-unused-parameter', '-Wno-multichar', '-isysroot', sdk, '-mmacosx-version-min=14.0', '-I'+str(OUT), '-I'+str(ROOT/'libobs'), '-I'+str(ROOT/'plugins/decklink'), '-I'+str(ROOT/'deps/libcaption'), '-I'+str(ROOT/'build_macos_native422/config'), '-I'+str(ROOT/'.deps/obs-deps-2026-08-26-universal/include'), '-F'+str(frameworks), '-framework','libobs', '-framework','CoreFoundation', '-Wl,-rpath,'+str(frameworks), '-Wl,-dead_strip']
# Compile whole production translation units, not a parallel fake owner.
files = ['decklink-output.cpp','decklink-devices.cpp','decklink-device-instance.cpp','decklink-device.cpp','decklink-device-mode.cpp','DecklinkOutput.cpp','DecklinkInput.cpp','DecklinkBase.cpp','decklink-device-discovery.cpp','OBSVideoFrame.cpp','util.cpp','mac/platform.cpp']
if os.environ.get('PV_DECKLINK_SANITIZE') == '1': cmd += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
cflags = [x for x in cmd[2:cmd.index('-framework')] if not x.startswith('-std=')]
if os.environ.get('PV_DECKLINK_SANITIZE') == '1': cflags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(['xcrun','clang', '-c', str(ROOT/'plugins/decklink/audio-repack.c'), '-o', str(OUT/'audio-repack.o')] + cflags, check=True)
base = cmd[:]
cmd += [str(OUT/'audio-repack.o'), str(ROOT/'build_macos_native422/deps/libcaption/RelWithDebInfo/libcaption.a')]
subprocess.run(base+[str(ROOT/'plugins/decklink/tests/private-media.cpp'), '-o',str(OUT/'private-media')],check=True)
subprocess.run([str(OUT/'private-media'),str(frameworks/'libobs-opengl.dylib'),str(ROOT/'libobs/data')],check=True,timeout=20)
# Compile the actual manual Start readiness expression, not a copied policy.
ui_source = (ROOT/'plugins/decklink-output-ui/decklink-ui-main.cpp').read_text()
manual = re.search(r'bool ready = (.*?);', ui_source, re.S).group(0)
assert ui_source.index(manual) < ui_source.index('main_output_running = obs_output_start(output)')
media = re.search(r'obs_output_set_media\(context.output,.*?;', ui_source, re.S).group(0)
(OUT/'rendered-media.inc').write_text('static void bind_rendered_media(obs_output_t *output, video_t *video) { struct { obs_output_t *output; video_t *video_queue; } context={output,video}; ' + media + ' }')
(OUT/'manual-ready.inc').write_text('static bool manual_ready(obs_source_t *selected) { calldata_t cd; calldata_init(&cd); ' + manual + ' calldata_free(&cd); return ready; }')
qt = ROOT/'.deps/obs-deps-qt6-2026-05-21-universal/lib'
subprocess.run(base[:2]+['-F'+str(qt)]+base[2:]+['-include','arm_acle.h','-I'+str(qt/'QtCore.framework/Headers'),'-F'+str(qt),'-framework','QtCore','-Wl,-rpath,'+str(qt),str(ROOT/'plugins/decklink/tests/receive-ui.cpp'),'-o',str(OUT/'receive-ui')],check=True)
subprocess.run([str(OUT/'receive-ui')],check=True,timeout=20)
runtime = ROOT/'.deps/pixelview-gstreamer'
gstSDK = ROOT/'.deps/gstreamer-upstream-1.28.3/sdk'
assert runtime.is_dir(), 'Stage the pinned GStreamer runtime first'
gstinc = ['-I'+str(gstSDK/p) for p in ['include','include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include']]
gstlibs = ['-L'+str(runtime/'lib'), '-Wl,-rpath,'+str(runtime/'lib')] + ['-l'+x for x in ['gstapp-1.0.0','gstvideo-1.0.0','gstaudio-1.0.0','gstbase-1.0.0','gstreamer-1.0.0','gstcodecparsers-1.0.0','gstrtp-1.0.0','glib-2.0.0','gobject-2.0.0']]
for key in list(os.environ):
    if key.startswith(('GST_','DYLD_')): del os.environ[key]
os.environ.update(GST_PLUGIN_SYSTEM_PATH_1_0='',GST_PLUGIN_PATH_1_0=str(runtime/'lib/gstreamer-1.0'),GST_PLUGIN_SCANNER=str(runtime/'libexec/gst-plugin-scanner'),GST_REGISTRY=str(OUT/'registry.bin'))
# C and Objective-C source adapters remain their original language, linked with
# the C++ owner only after independent compilation.
compileflags = [x for x in base[2:base.index('-framework')] if not x.startswith('-std=')]
if os.environ.get('PV_DECKLINK_SANITIZE') == '1': compileflags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
whep = os.environ.get('PV_DECKLINK_WHEP_BUILD') == '1'
for file in ['plugins/decklink/tests/' + ('whep-feed.c' if whep else 'production-feed.c')] + ['plugins/pixelview-whep/'+f for f in ['video-format.c','profile-offer.c','capability-probe.c','native-422-filter.c','native-422.m']]:
    obj=OUT/(Path(file).name+'.o')
    source=ROOT/file
    if file.endswith('native-422-filter.c') and os.environ.get('PV_BOUNDED_CAMPAIGN') == '1':
        source=ROOT/'plugins/decklink/tests/whep-campaign-filter.c'
    if file.endswith('native-422-filter.c') and os.environ.get('PV_MATRIX_TRACE') == '1':
        text=source.read_text().replace('gboolean ok = pv_native422_decode_preview', 'fprintf(stderr,"MATRIX_NATIVE_ENTER pts=%llu\\n",(unsigned long long)GST_BUFFER_PTS(buffer)); gboolean ok = pv_native422_decode_preview')
        text=text.replace('gst_sample_unref(sample); gst_buffer_unref(buffer);', 'fprintf(stderr,"MATRIX_NATIVE_EXIT ok=%d\\n",ok); gst_sample_unref(sample); gst_buffer_unref(buffer);')
        source=OUT/'native-422-filter-matrix.c';source.write_text('#include <stdio.h>\n'+text)
    if file.endswith('native-422-filter.c') and os.environ.get('PV_NATIVE422_DIAGNOSTIC') == '1':
        text=source.read_text().replace('gst_rtp_buffer_unmap(&packet);', 'if(r->rate.failed) fprintf(stderr,"RTP_REJECT seq=%u previous=%u timestamp=%u markerprev=%u discont=%d intervals=%u candidates=%u\\n",gst_rtp_buffer_get_seq(&packet),r->rate.sequence,gst_rtp_buffer_get_timestamp(&packet),r->rate.marker_timestamp,GST_BUFFER_FLAG_IS_SET(b,GST_BUFFER_FLAG_DISCONT),r->rate.intervals,r->rate.candidates); gst_rtp_buffer_unmap(&packet);')
        text=text.replace('gst_rtp_buffer_unmap(&packet);', 'if(r->rate.failed) fprintf(stderr,"RTP_REJECT_CLOCK gap_ns=%llu num=%u den=%u ssrc=%u expected_ssrc=%u\\n",(unsigned long long)(g_get_monotonic_time()*1000ULL-r->rate.last),r->rate.num,r->rate.den,gst_rtp_buffer_get_ssrc(&packet),r->rate.ssrc); gst_rtp_buffer_unmap(&packet);')
        source=OUT/'native-422-filter-diagnostic.c';source.write_text('#include <stdio.h>\n'+text)
    if file.endswith('native-422.m') and os.environ.get('PV_NATIVE422_DIAGNOSTIC') == '1':
        text=source.read_text()
        text=text.replace('return FALSE;', 'do { fprintf(stderr,"NATIVE_FALSE %d\\n",__LINE__); return FALSE; } while(0);')
        text=text.replace('goto done;', 'do { fprintf(stderr,"NATIVE_CONFIG_REJECT %d\\n",__LINE__); goto done; } while(0);')
        text=text.replace('goto failed;', 'do { fprintf(stderr,"NATIVE_REJECT_LINE %d\\n",__LINE__); goto failed; } while(0);'.replace('\\n','\\\\n'))
        text=text.replace('GstBuffer *buffer = gst_sample_get_buffer(sample);', 'GstBuffer *buffer = gst_sample_get_buffer(sample); fprintf(stderr,"NATIVE_AU pts=%llu duration=%llu bytes=%zu\\\\n", GST_BUFFER_PTS(buffer), GST_BUFFER_DURATION(buffer), gst_buffer_get_size(buffer));')
        text=text.replace('if (decoded.image) CVPixelBufferRelease(decoded.image);', 'if (!ok) fprintf(stderr,"NATIVE_VT status=%d waited=%d callback=%d count=%u image=%p\\n",(int)status,(int)waited,(int)decoded.status,decoded.count,(void*)decoded.image); if (decoded.image) CVPixelBufferRelease(decoded.image);'.replace('\n','\\n'))
        source=OUT/'native-422-diagnostic.m'; source.write_text('#include <stdio.h>\n'+text)
    subprocess.run(['xcrun','clang',*compileflags,*gstinc,'-I'+str(ROOT/'plugins/pixelview-whep'),'-c',str(source),'-o',str(obj)],check=True)
    cmd.append(str(obj))
cmd += gstlibs + ['-framework', 'Foundation','-framework','VideoToolbox','-framework','CoreMedia','-framework','CoreVideo']
if whep:
    cmd += ['-lnice.10', '-lgstwebrtc-1.0.0', '-lgstsdp-1.0.0']
target = 'whep-approved' if whep and os.environ.get('PV_WHEP_APPROVED') else 'whep-receive' if whep else 'receive'
if whep and os.environ.get('PV_DECKLINK_WHEP_CADENCE_AUDIT') == '1':
    target = 'whep-cadence-reject'
cmd += [str(ROOT/'plugins/decklink'/f) for f in files] + [str(ROOT/'plugins/decklink/tests'/(target+'.cpp')), '-o', str(OUT/target)]
(OUT/'command.txt').write_text(' '.join(cmd))
subprocess.run(cmd, check=True)
if whep:
    raise SystemExit(0)
fixtures=Path(os.environ.get('PV_DECKLINK_FIXTURES', ROOT/'plugins/pixelview-whep/.test-build/native-422'))
subprocess.run([str(OUT/'receive'),str(fixtures/'limited.h265'),str(fixtures/'limited.v210'),str(frameworks/'libobs-opengl.dylib'),str(ROOT/'libobs/data')], check=True, timeout=30)
