"""Compile production startup, refresh, and selection gate; no capture opened."""
import os
import pathlib
import subprocess
import tempfile
import unittest
from test_first_launch_defaults import ROOT
from test_stream_lock import body


class CaptureStartup(unittest.TestCase):
    def test_late_discovery_selects_once_without_pairing_and_preserves_choices(self):
        text = (ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        startup = body(text, 'InitPixelview').split('pixelviewRefreshTimer->start(2000);', 1)[1]
        refresh = body(text, 'RefreshPixelviewDevices')
        selection = body(text, 'SelectPixelviewDevice').split('\n\tOBSSourceAutoRelease source', 1)[0]
        code = r'''
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QWidget>
#include <QtCore/QSignalBlocker>
#include <cassert>
#include <cstring>
#include "frontend/utility/PixelviewCapturePolicy.hpp"
namespace fixture {
struct Entry {const char *id,*name; bool disabled;};
std::vector<Entry> entries; bool available=true, saved=false; int storage=0, selections=0;
std::string selected;
using OBSProperties=int*; using OBSSourceAutoRelease=int*; using OBSDataAutoRelease=int*;
int *obs_get_source_properties(const char *id){assert(!strcmp(id,"decklink-input"));return available?&storage:nullptr;}
int *obs_properties_get(int*,const char *key){assert(!strcmp(key,"device_hash"));return &storage;}
size_t obs_property_list_item_count(int*){return entries.size();}
const char *obs_property_list_item_string(int*,size_t i){return entries[i].id;}
const char *obs_property_list_item_name(int*,size_t i){return entries[i].name;}
bool obs_property_list_item_disabled(int*,size_t i){return entries[i].disabled;}
int *obs_get_source_by_name(const char*){return saved?&storage:nullptr;}
int *obs_source_get_settings(int*){return &storage;}
const char *obs_data_get_string(int*,const char*){return selected.c_str();}
int *obs_scene_find_source(int,const char*){return saved?&storage:nullptr;}
void obs_sceneitem_select(int*,bool){} void obs_sceneitem_set_locked(int*,bool){}
struct OBSBasic {
 bool pixelviewCaptureAutoSelectPending=false;
 bool pixelviewReceiving=false,busy=false,paired=false,closing=false;
 QComboBox combo; QComboBox *pixelviewDevices=&combo;
 QWidget settings, fit, preview, x, y;
 QWidget *pixelviewSettings=&settings,*pixelviewFit=&fit,*properties=nullptr;
 struct UI{QWidget *preview,*previewXContainer,*previewYScrollBar;} uiStorage{&preview,&x,&y},*ui=&uiStorage;
 bool isClosing(){return closing;}
 int GetCurrentScene(){return 0;}
 bool PixelviewSettingsBusy(){return busy;}
 bool PixelviewConfigurationLocked(){return busy || !paired || pixelviewReceiving;}
 void RefreshPixelviewModes(){} void RefreshPixelviewAudio(){} void RefreshPixelviewFPS(){} void RefreshPixelviewEncoding(){} void RefreshPixelviewStreamHint(){}
 void RefreshPixelviewDevices(){REFRESH}
 void SelectPixelviewDevice(int index,bool initializing=false){
 SELECTION
 // Hardware boundary only: production selection/revalidation above, no card open.
 ++selections; selected=id.constData();saved=true;RefreshPixelviewDevices();
 }
 void startup(){STARTUP}
};
void reset(){entries.clear();selected.clear();saved=false;selections=0;available=true;}
}
int main(int argc,char **argv){QApplication app(argc,argv);using namespace fixture;
 reset(); OBSBasic w; w.startup();assert(selected.empty());
 entries={{"","Empty",false},{"gone","Disabled",true},{"capture","4K Mini",false},{"second","Second",false}};
 w.RefreshPixelviewDevices();assert(selected=="capture" && selections==1);
 assert(w.combo.currentData().toString()=="capture");
 w.RefreshPixelviewDevices();assert(selections==1);
 // Removing a once-selected source must not restart first-run policy.
 saved=false;selected.clear();w.RefreshPixelviewDevices();assert(selections==1);
 for(const char *choice:{"second","disconnected",""}){
  reset();saved=true;selected=choice;OBSBasic existing;existing.startup();
  entries={{"capture","4K Mini",false}};existing.RefreshPixelviewDevices();
  assert(selected==choice && selections==0);
 }
 reset();OBSBasic delayed;delayed.busy=true;delayed.startup();
 entries={{"capture","4K Mini",false}};delayed.RefreshPixelviewDevices();assert(!saved);
 delayed.busy=false;delayed.pixelviewReceiving=true;delayed.RefreshPixelviewDevices();assert(!saved);
 delayed.pixelviewReceiving=false;delayed.closing=true;delayed.RefreshPixelviewDevices();assert(!saved);
 delayed.closing=false;QWidget editor;delayed.properties=&editor;delayed.RefreshPixelviewDevices();assert(!saved);
 delayed.properties=nullptr;delayed.RefreshPixelviewDevices();assert(selected=="capture" && selections==1);
 reset();available=false;OBSBasic missing;missing.startup();assert(!saved);
 available=true;entries={{"","Empty",false},{"gone","Disabled",true}};missing.RefreshPixelviewDevices();assert(!saved);
 entries.push_back({"capture","4K Mini",false});missing.RefreshPixelviewDevices();assert(selected=="capture");
 // An explicit selection before discovery settles takes precedence.
 reset();OBSBasic explicitChoice;explicitChoice.startup();saved=true;selected="explicit";
 entries={{"capture","4K Mini",false}};explicitChoice.RefreshPixelviewDevices();assert(selected=="explicit" && selections==0);
}
'''.replace('REFRESH', refresh).replace('SELECTION', selection).replace('STARTUP', startup)
        with tempfile.TemporaryDirectory(prefix='pixelview-capture-startup-') as tmp:
            cpp=pathlib.Path(tmp)/'startup.cpp';cpp.write_text(code)
            qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++','-std=c++17','-fPIC','-I'+str(ROOT),'-F'+str(qt),'-framework','QtCore','-framework','QtGui','-framework','QtWidgets','-Wl,-rpath,'+str(qt),str(cpp),'-o',tmp+'/startup'],check=True)
            subprocess.run([tmp+'/startup'],env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},check=True,timeout=15)


if __name__ == '__main__':
    unittest.main()
