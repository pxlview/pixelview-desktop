"""Execute production first-launch policy with isolated discovery boundaries."""
import pathlib
import subprocess
import tempfile
import unittest
from test_stream_lock import body
ROOT = pathlib.Path(__file__).resolve().parents[2]


def compiled(code):
    with tempfile.TemporaryDirectory() as tmp:
        src = pathlib.Path(tmp) / 'defaults.cpp'
        src.write_text(code)
        exe = pathlib.Path(tmp) / 'defaults'
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(src), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr


class FirstLaunchDefaults(unittest.TestCase):
    def test_no_hevc_falls_back_to_x264_not_hardware_h264_or_av1(self):
        compiled('''#include "frontend/utility/PixelviewEncoding.hpp"
#include <cassert>
int main() {
 using namespace pixelview;
 for (bool apple : {false, true}) {
  std::vector<EncoderChoice> e={{"obs_nvenc_h264_tex","h264",true},
   {"obs_nvenc_av1_tex","av1",true},{"obs_x264","h264",false}};
  assert(preferredEncoder(e,apple)==2);
  e.pop_back(); assert(preferredEncoder(e,apple)==-1);
  for (const char *id : {"com.apple.videotoolbox.videoencoder.ave.hevc", "obs_nvenc_hevc_tex",
    "ffmpeg_hevc_nvenc", "obs_qsv11_hevc", "h265_texture_amf", "hevc_ffmpeg_vaapi_tex"}) {
   e.push_back({id,"hevc",isHardwareEncoder(id)});
   assert(preferredEncoder(e,apple)==2); e.pop_back();
  }
 }
}''')

    def test_actual_discovery_skips_disabled_and_startup_preserves_saved_source(self):
        text = (ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        refresh = body(text, 'RefreshPixelviewDevices')
        discovery = refresh[refresh.index('std::vector<pixelview::Device> devices;'):refresh.index('OBSSourceAutoRelease source')]
        startup = body(text, 'InitPixelview').split('pixelviewRefreshTimer->start(2000);', 1)[1]
        compiled(r'''#include <cassert>
#include <string>
#include <vector>
namespace pixelview { struct Device {std::string id,name;}; }
struct Entry {const char *id,*name;bool disabled;};
std::vector<Entry> entries;
size_t obs_property_list_item_count(int*) {return entries.size();}
const char *obs_property_list_item_string(int*,size_t i) {return entries[i].id;}
const char *obs_property_list_item_name(int*,size_t i) {return entries[i].name;}
bool obs_property_list_item_disabled(int*,size_t i) {return entries[i].disabled;}
std::vector<pixelview::Device> discover() {int storage=0;auto *list=&storage;
DISCOVERY
return devices;}
bool saved=false; int item=0; std::string selected;
int GetCurrentScene(){return 0;}
int *obs_scene_find_source(int,const char*){return saved?&item:nullptr;}
int *obs_get_source_by_name(const char*){return saved?&item:nullptr;}
using OBSSourceAutoRelease=int*;
void obs_sceneitem_select(int*,bool){}
void obs_sceneitem_set_locked(int*,bool){}
struct Combo {int count(){return int(discover().size())+1;}} combo;
auto *pixelviewDevices=&combo;
void RefreshPixelviewDevices(){}
void SelectPixelviewDevice(int index,bool initializing=false) {
 assert(initializing); selected=discover().at(index-1).id;
}
void startup() {STARTUP}
int main(){
 entries={{"","empty",false},{"gone","Disconnected",true},{"actual-first","First",false},{"actual-second","Second",false}};
 auto found=discover(); assert(found.size()==2);assert(found[0].id=="actual-first");
 startup();assert(selected=="actual-first");
 saved=true;selected="saved-disconnected";startup();assert(selected=="saved-disconnected");
 saved=false;selected.clear();entries.clear();startup();assert(selected.empty());
 entries={{"gone","Disconnected",true}};startup();assert(selected.empty());
}'''.replace('DISCOVERY', discovery).replace('STARTUP', startup))

    def test_saved_encoder_is_not_replaced_even_when_temporarily_unavailable(self):
        text = (ROOT/'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        startup = text[text.index('\tconst bool initialized ='):text.index('\tconnect(pixelviewEncoder,')]
        compiled(r'''#include "frontend/utility/PixelviewEncoding.hpp"
#include <cassert>
#include <filesystem>
struct QString {
 std::string s; static QString fromUtf8(const char *p){return {p};}
 QString toString()const{return *this;} QString toUtf8()const{return *this;}
 const char *constData()const{return s.c_str();}
};
bool initializedFlag=true;std::string savedId="obs_x264";int activeConfiguration=0;
bool config_get_bool(int,const char*,const char*){return initializedFlag;}
const char *config_get_string(int,const char*,const char*){return savedId.c_str();}
std::vector<pixelview::EncoderChoice> encoders;
struct Combo {
 int findData(QString s){for(size_t i=0;i<encoders.size();++i)if(encoders[i].id==s.s)return int(i);return -1;}
 QString itemData(int i){return {encoders.at(i).id};}
} combo;
auto *pixelviewEncoder=&combo;
struct Profile{std::filesystem::path path;};Profile GetCurrentProfile(){return {};}
struct OBSData{bool fresh;};
OBSData PixelviewEncoderData(std::filesystem::path,const char*,bool fresh){return {fresh};}
int writes=0;bool freshWrite=false;
void SavePixelviewEncoding(const char *id,OBSData data,bool initializing){assert(initializing);++writes;savedId=id;freshWrite=data.fresh;}
void startup(){STARTUP}
int main(){
 encoders={{"obs_x264","h264",false},{"obs_nvenc_hevc_tex","hevc",true}};
 startup();assert(savedId=="obs_x264" && writes==1 && !freshWrite);
 savedId="missing-explicit-encoder";writes=0;startup();assert(savedId=="missing-explicit-encoder" && writes==0);
 initializedFlag=false;startup();assert(savedId=="obs_nvenc_hevc_tex" && writes==1 && freshWrite);
}'''.replace('STARTUP',startup))

    def test_capture_initialization_bypasses_pairing_only_not_busy(self):
        text = (ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        for method in ('SelectPixelviewDevice', 'FitPixelviewCapture'):
            guard = body(text, method).split('\n', 2)[1]
            compiled('''#include <cassert>
int main(){
 bool busy=false, paired=false; int refreshed=0;
 auto PixelviewSettingsBusy=[&]{return busy;};
 auto PixelviewConfigurationLocked=[&]{return !paired || busy;};
 [[maybe_unused]] auto RefreshPixelviewDevices=[&]{++refreshed;};
 bool passed=false;
 auto invoke=[&](bool initializing){ GUARD passed=true;};
 invoke(false);assert(!passed);invoke(true);assert(passed);
 passed=false;busy=true;invoke(true);assert(!passed);
 busy=false;paired=true;invoke(false);assert(passed);
}'''.replace('GUARD', guard))

    def test_native_encoder_defaults_and_saved_json_round_trip(self):
        import os
        frameworks = ROOT/'build_macos/libobs/RelWithDebInfo'
        if not (frameworks/'libobs.framework').exists():
            self.skipTest('Requires built macOS libobs and native encoder plugins')
        text = (ROOT/'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        actual = text.split('namespace {', 1)[1].split('std::string PixelviewOpusEncoder()', 1)[0]
        code = r'''#include <obs.hpp>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include "frontend/utility/PixelviewEncoding.hpp"
ACTUAL
int main(int argc,char **argv){
 assert(argc==3);assert(obs_startup("en-US",nullptr,nullptr));
 std::filesystem::path root=argv[1], saved=std::filesystem::path(argv[2])/"streamEncoder.json";
 for(const char *name:{"mac-videotoolbox","obs-x264"}) {
  auto bundle=root/"plugins"/name/"RelWithDebInfo"/(std::string(name)+".plugin")/"Contents";
  obs_module_t *module=nullptr;
  assert(obs_open_module(&module,(bundle/"MacOS"/name).c_str(),(bundle/"Resources").c_str())==MODULE_SUCCESS);
  assert(obs_init_module(module));
 }
 obs_post_load_modules();
 std::vector<pixelview::EncoderChoice> choices;const char *id=nullptr;
 for(size_t i=0;obs_enum_encoder_types(i,&id);++i){
  std::cout<<id<<" codec="<<obs_get_encoder_codec(id)<<"\n";
  if(obs_get_encoder_type(id)==OBS_ENCODER_VIDEO && !(obs_get_encoder_caps(id)&(OBS_ENCODER_CAP_INTERNAL|OBS_ENCODER_CAP_DEPRECATED)))
   choices.push_back({id,obs_get_encoder_codec(id),pixelview::isHardwareEncoder(id)});
 }
 auto selected=pixelview::preferredEncoder(choices,true);assert(selected>=0);
 assert(choices[selected].id=="com.apple.videotoolbox.videoencoder.ave.hevc");
 for(const char *encoder:{choices[selected].id.c_str(),"obs_x264"}) {
  auto data=PixelviewEncoderData(saved,encoder,true);
  assert(obs_data_get_int(data,"bitrate")==6000);
  assert(strcmp(obs_data_get_string(data,"profile"),strcmp(encoder,"obs_x264")==0?"baseline":"main")==0);
  assert(strcmp(obs_data_get_string(data,"rate_control"),"CBR")==0);
  assert(obs_data_get_int(data,"keyint_sec")==1);
  obs_data_set_int(data,"bitrate",9000);obs_data_set_string(data,"profile",strcmp(encoder,"obs_x264")==0?"high":"main10");
  assert(obs_data_save_json_safe(data,saved.c_str(),"tmp","bak"));
  auto restored=PixelviewEncoderData(saved,encoder,false);
  assert(obs_data_get_int(restored,"bitrate")==9000);
  assert(strcmp(obs_data_get_string(restored,"profile"),obs_data_get_string(data,"profile"))==0);
 }
 obs_shutdown();std::cout<<"PASS native Main/6000 CBR, x264 fallback defaults, saved profile/bitrate preserved\n";
}'''.replace('ACTUAL', actual)
        deps=ROOT/'.deps/obs-deps-2026-08-26-universal'
        with tempfile.TemporaryDirectory(prefix='pixelview-first-launch-') as tmp:
            src=pathlib.Path(tmp)/'native.cpp';src.write_text(code)
            exe=pathlib.Path(tmp)/'native'
            env=dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
            subprocess.run(['xcrun','clang++','-std=c++17','-I'+str(ROOT),'-I'+str(ROOT/'libobs'),
                '-I'+str(ROOT/'build_macos/config'),'-I'+str(ROOT/'build_macos/libobs'),'-I'+str(deps/'include'),
                str(src),'-F'+str(frameworks),'-framework','libobs','-Wl,-rpath,'+str(frameworks),
                '-Wl,-rpath,'+str(deps/'lib'),'-o',str(exe)],check=True,env=env)
            result=subprocess.run([str(exe),str(ROOT/'build_macos'),tmp],capture_output=True,text=True,env=env)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout)

if __name__ == '__main__':
    unittest.main()
