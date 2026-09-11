"""Build isolated production worker with test-only private bus ERROR probe."""
import os, pathlib, subprocess, shutil
root=pathlib.Path(__file__).resolve().parents[1];repo=root.parents[1]
app=pathlib.Path(os.environ.get('PIXELVIEW_TEST_APP',str(repo/'build_macos/frontend/RelWithDebInfo/Pixelview Desktop.app')))
out=root/'.test-build/controller-media';contents=out/'Probe.plugin/Contents'
(contents/'MacOS').mkdir(parents=True,exist_ok=True);(contents/'Resources').mkdir(exist_ok=True)
runtime=contents/'Resources/GStreamer'
if not runtime.exists():shutil.copytree(app/'Contents/PlugIns/pixelview-whep.plugin/Contents/Resources/GStreamer',runtime)
source=(root/'pixelview-whep.c').read_text().replace('#include "runtime.h"','#include "'+str(root/'runtime.h')+'"')
source=source.replace('   if (msg) gst_message_unref(msg);','''   if (msg && GST_MESSAGE_TYPE(msg)==GST_MESSAGE_ERROR) {
    GError *error=NULL; gchar *debug=NULL; gst_message_parse_error(msg,&error,&debug);
    fprintf(stderr,"BUS_ERROR %s %s\\n",error->message,debug ? debug : "");
    g_clear_error(&error); g_free(debug);
   }
   if (msg) gst_message_unref(msg);''')
source='#include <stdio.h>\n'+source
(out/'probe.c').write_text(source)
deps=sorted((repo/'.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
sdk=repo/'.deps/gstreamer-upstream-1.28.3/sdk'
flags=['-I'+str(sdk/p) for p in ('include/gstreamer-1.0','include/glib-2.0','lib/glib-2.0/include')]
import native422_build
helpers=[str(root/p) for p in ('video-format.c','profile-offer.c','capability-probe.c')] + native422_build.flags(root)
subprocess.run(['clang','-dynamiclib','-mmacosx-version-min=14.0','-I'+str(root),'-I'+str(repo/'libobs'),'-I'+str(repo/'build_macos/config'),'-I'+str(deps/'include'),'-F'+str(app/'Contents/Frameworks'),'-framework','libobs',*flags,*helpers,str(out/'probe.c'),'-o',str(contents/'MacOS/probe'),'-L'+str(runtime/'lib'),'-lgstapp-1.0.0','-lgstvideo-1.0.0','-lgstaudio-1.0.0','-lgstbase-1.0.0','-lgstreamer-1.0.0','-lgobject-2.0.0','-lglib-2.0.0','-Wl,-rpath,'+str(runtime/'lib')],check=True)
print('Isolated test-only worker probe built')
