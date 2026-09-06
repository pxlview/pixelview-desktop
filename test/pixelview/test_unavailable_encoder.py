"""Compile actual startup, refresh enablement and explicit selection without GUI."""
import unittest
from test_first_launch_defaults import ROOT, compiled
from test_stream_lock import body


class UnavailableEncoder(unittest.TestCase):
    def test_missing_saved_encoder_can_be_explicitly_replaced_only_when_unlocked(self):
        text = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        startup = text[text.index('\tconst bool initialized ='):text.index('\tconnect(pixelviewEncoder,')]
        refresh = body(text, 'RefreshPixelviewEncoding')
        refresh = refresh[:refresh.index('\tOBSData data =')]
        activation = text.split('connect(pixelviewEncoder, &QComboBox::activated, this, [this](int index) {', 1)[1].split('\n\t});', 1)[0]
        compiled(r'''#include "frontend/utility/PixelviewEncoding.hpp"
#include <cassert>
#include <filesystem>
struct QString {
 std::string s;
 static QString fromUtf8(const char *p){return {p};}
 static QString fromStdString(const std::string &s){return {s};}
 QString toString()const{return *this;} QString toUtf8()const{return *this;}
 const char *constData()const{return s.c_str();}
 bool operator==(const char *p)const{return s==p;}
};
#define QStringLiteral(s) QString{s}
struct Control {
 bool enabled=false, visible=false;
 void setEnabled(bool b){enabled=b;} void setVisible(bool b){visible=b;}
 void setText(QString){} void setToolTip(QString){}
};
std::vector<pixelview::EncoderChoice> encoders={{"obs_x264","h264",false}};
struct Combo: Control {
 int current=0;
 int findData(QString s){for(size_t i=0;i<encoders.size();++i)if(encoders[i].id==s.s)return int(i);return -1;}
 QString itemData(int i){return {encoders.at(i).id};}
 int count()const{return int(encoders.size());}
 bool hasFocus()const{return false;} Combo *view(){return this;}
 bool isVisible()const{return false;} int currentIndex()const{return current;}
 void setCurrentIndex(int i){current=i;}
} combo;
auto *pixelviewEncoder=&combo;
Control advanced, bitrate, profile, status;
auto *pixelviewAdvanced=&advanced, *pixelviewBitrate=&bitrate,
 *pixelviewProfile=&profile, *pixelviewEncodingStatus=&status;
struct QSignalBlocker {template<typename T> QSignalBlocker(T*){} ~QSignalBlocker(){}};
bool paired=true, busy=false, opus=true;
bool PixelviewConfigurationLocked(){return !paired || busy;}
bool isClosing(){return false;}
std::string PixelviewOpusEncoder(){return opus?"opus":"";}
int activeConfiguration=0, writes=0;
std::string savedId="missing-explicit-encoder", savedJson="original-custom-settings";
bool config_get_bool(int,const char*,const char*){return true;}
const char *config_get_string(int,const char*,const char*){return savedId.c_str();}
struct Profile{std::filesystem::path path;}; Profile GetCurrentProfile(){return {};}
struct OBSData{bool fresh;};
OBSData PixelviewEncoderData(std::filesystem::path,const char*,bool fresh){return {fresh};}
void SavePixelviewEncoding(const char *id,OBSData data,bool initializing=false){
 assert(initializing || !PixelviewConfigurationLocked());
 ++writes;savedId=id;savedJson=data.fresh?"replacement-defaults":savedJson;
}
void startup(){STARTUP}
void RefreshPixelviewEncoding(){REFRESH}
void activate(int index){ACTIVATION}
void assertMissingSettings(){
 assert(!advanced.enabled && !bitrate.enabled && !profile.enabled && status.visible);
 assert(savedId=="missing-explicit-encoder" && savedJson=="original-custom-settings" && writes==0);
}
int main(){
 startup();RefreshPixelviewEncoding();assertMissingSettings();
 assert(combo.currentIndex()==-1);
 assert(combo.enabled && "paired idle missing encoder must allow available replacement");
 // Repeated refresh never silently replaces the saved ID or settings.
 RefreshPixelviewEncoding();assertMissingSettings();
 for(bool isPaired:{false,true})for(bool isBusy:{false,true}){
  if(isPaired && !isBusy)continue;
  paired=isPaired;busy=isBusy;RefreshPixelviewEncoding();
  assert(!combo.enabled);activate(0);assertMissingSettings();
 }
 paired=true;busy=false;encoders.clear();RefreshPixelviewEncoding();
 assert(!combo.enabled);assertMissingSettings();
 encoders={{"obs_x264","h264",false}};RefreshPixelviewEncoding();
 assert(combo.enabled);activate(0);
 assert(savedId=="obs_x264" && savedJson=="replacement-defaults" && writes==1);
 assert(combo.currentIndex()==0 && combo.enabled);
 assert(advanced.enabled && bitrate.enabled && profile.enabled && !status.visible);
 activate(0);assert(writes==1);
 opus=false;RefreshPixelviewEncoding();
 assert(!advanced.enabled && !bitrate.enabled && !profile.enabled && status.visible);
}
'''.replace('STARTUP', startup).replace('REFRESH', refresh).replace('ACTIVATION', activation))


if __name__ == '__main__':
    unittest.main()
