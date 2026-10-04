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
// Test pattern generator list: values and names, as the plugin registers them.
std::vector<std::pair<long long,const char*>> patterns; int patternStorage=0, deckLinkProps=0;
using OBSProperties=int*; using OBSSourceAutoRelease=int*; using OBSDataAutoRelease=int*;
int *obs_get_source_properties(const char *id){
 if(!strcmp(id,pixelview::TestPatternSourceId)) return patterns.empty()?nullptr:&patternStorage;
 assert(!strcmp(id,"decklink-input"));return available?&deckLinkProps:nullptr;}
int *obs_properties_get(int *props,const char *key){
 if(props==&patternStorage){assert(!strcmp(key,"pattern"));return &patternStorage;}
 assert(!strcmp(key,"device_hash"));return &storage;}
size_t obs_property_list_item_count(int *list){return list==&patternStorage?patterns.size():entries.size();}
const char *obs_property_list_item_string(int*,size_t i){return entries[i].id;}
long long obs_property_list_item_int(int *list,size_t i){assert(list==&patternStorage);return patterns[i].first;}
const char *obs_property_list_item_name(int *list,size_t i){return list==&patternStorage?patterns[i].second:entries[i].name;}
bool obs_property_list_item_disabled(int*,size_t i){return entries[i].disabled;}
int *obs_get_source_by_name(const char*){return saved?&storage:nullptr;}
int *obs_source_get_settings(int*){return &storage;}
const char *obs_data_get_string(int*,const char*){return selected.c_str();}
int *obs_scene_find_source(int,const char*){return saved?&storage:nullptr;}
void obs_sceneitem_select(int*,bool){} void obs_sceneitem_set_locked(int*,bool){}
struct OBSBasic {
 bool pixelviewCaptureAutoSelectPending=false;
 bool testPatternActive=false; long long pattern=0; int patternSelections=0;
 int *PixelviewTestPatternItem(){return testPatternActive?&storage:nullptr;}
 bool PixelviewTestPatternActive(){return testPatternActive;}
 std::string PixelviewSelectedCaptureId(){return testPatternActive?pixelview::testPatternId(pattern):saved?selected:std::string();}
 void SelectPixelviewTestPattern(long long value){++patternSelections;testPatternActive=true;pattern=value;RefreshPixelviewDevices();}
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
 void QueuePixelviewStatePush(){} // Admin state push; covered by test_remote_control.
 void RefreshPixelviewDevices(){REFRESH}
 void SelectPixelviewDevice(int index,bool initializing=false){
 SELECTION
 // Hardware boundary only: production selection/revalidation above, no card open.
 ++selections; selected=id.constData();saved=true;RefreshPixelviewDevices();
 }
 void startup(){STARTUP}
};
void reset(){entries.clear();patterns.clear();selected.clear();saved=false;selections=0;available=true;}
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
 // Test patterns follow the devices after a separator, and are never auto-selected.
 reset();patterns={{0,"SMPTE color bars"},{3,"Gray ramp (10-bit)"}};available=false;
 OBSBasic generated;generated.paired=true;generated.startup();
 assert(generated.combo.count()==4 && generated.combo.itemData(1).toString().isEmpty());
 assert(generated.combo.itemData(3).toString()=="test-pattern:3" && generated.combo.itemText(3)=="Test pattern: Gray ramp (10-bit)");
 assert(generated.combo.currentIndex()==0 && generated.patternSelections==0 && generated.pixelviewDevices->isEnabled());
 generated.SelectPixelviewDevice(1);assert(generated.patternSelections==0); // separator
 generated.SelectPixelviewDevice(3);assert(generated.patternSelections==1 && generated.pattern==3 && selections==0);
 assert(generated.combo.currentIndex()==3);
 // A saved, visible test pattern is a choice: a card appearing later must not replace it.
 available=true;entries={{"capture","4K Mini",false}};generated.RefreshPixelviewDevices();
 assert(selections==0 && generated.combo.currentData().toString()=="test-pattern:3");
 assert(generated.combo.itemData(1).toString()=="capture" && generated.combo.itemData(2).toString().isEmpty());
 reset();patterns={{0,"SMPTE color bars"}};OBSBasic restored;restored.testPatternActive=true;restored.startup();
 entries={{"capture","4K Mini",false}};restored.RefreshPixelviewDevices();
 assert(selections==0 && restored.combo.currentData().toString()=="test-pattern:0");
}
'''.replace('REFRESH', refresh).replace('SELECTION', selection).replace('STARTUP', startup)
        with tempfile.TemporaryDirectory(prefix='pixelview-capture-startup-') as tmp:
            cpp=pathlib.Path(tmp)/'startup.cpp';cpp.write_text(code)
            qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++','-std=c++17','-fPIC','-I'+str(ROOT),'-F'+str(qt),'-framework','QtCore','-framework','QtGui','-framework','QtWidgets','-Wl,-rpath,'+str(qt),str(cpp),'-o',tmp+'/startup'],check=True)
            subprocess.run([tmp+'/startup'],env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},check=True,timeout=15)


if __name__ == '__main__':
    unittest.main()
