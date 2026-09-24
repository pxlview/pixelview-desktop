import pathlib, subprocess, tempfile, unittest
from test_stream_lock import body
ROOT=pathlib.Path(__file__).resolve().parents[2]
class ReceivePrecision(unittest.TestCase):
 def test_switch_precision_before_source_mutation(self):
  text=(ROOT/'frontend/widgets/OBSBasic_PixelviewReceive.inc').read_text()
  switch=body(text,'SelectPixelviewMode')
  self.assertLess(switch.index('SetPixelviewReceivePrecision(index == 1)'),switch.index('ClearPixelviewAudio()'))
  stop=body(text,'StopPixelviewReceive')
  self.assertIn('SetPixelviewReceivePrecision(false)',stop)
  busy=body(text,'PixelviewReceiveVideoBusy')
  self.assertIn('obs_enum_outputs',busy); self.assertIn('obs_output_active',busy)
  reset=body((ROOT/'frontend/widgets/OBSBasic.cpp').read_text().replace('int OBSBasic::ResetVideo','bool OBSBasic::ResetVideo'),'ResetVideo')
  self.assertIn('pixelviewReceivePrecision',reset)
  self.assertNotIn('config_set_',body(text,'SetPixelviewReceivePrecision'))
 def test_transaction(self):
  text=(ROOT/'frontend/widgets/OBSBasic_PixelviewReceive.inc').read_text()
  switch=body(text,'SetPixelviewReceivePrecision')
  code=r'''#include <cassert>
#include <string>
struct obs_video_info {const char *graphics_module="opengl"; int output_format=1, colorspace=2,range=3; unsigned fps_num=30,fps_den=1;};
constexpr int VIDEO_FORMAT_P010=10,VIDEO_CS_709=709,VIDEO_CS_2100_PQ=2084,VIDEO_CS_2100_HLG=2067,VIDEO_RANGE_PARTIAL=1,OBS_VIDEO_SUCCESS=0;
float sdr_white=300.f,hdr_peak=1000.f;
float obs_get_video_sdr_white_level(){return sdr_white;}
float obs_get_video_hdr_nominal_peak_level(){return hdr_peak;}
void obs_set_video_levels(float white,float peak){sdr_white=white;hdr_peak=peak;}
obs_video_info current; int resets=0,failures=0; bool busy=false;
bool obs_get_video_info(obs_video_info *v){*v=current;return true;}
int obs_reset_video(obs_video_info *v){++resets;if(failures){--failures;return -1;}current=*v;return 0;}
struct Canvas {
 bool pixelviewReceivePrecision=false,pixelviewReceivePrecisionFault=false;
 obs_video_info pixelviewSendVideo={}; std::string pixelviewSendRenderModule;
 unsigned pixelviewReceiveFPSNum=0,pixelviewReceiveFPSDen=0;
 int pixelviewReceiveHdr=0,pixelviewReceiveHdrNits=1000; float pixelviewSendSdrWhite=0.f,pixelviewSendHdrPeak=0.f;
 bool PixelviewReceiveVideoBusy(){return busy;}
 bool set(bool enabled){BODY}
};
int main(){Canvas c; auto original=current;
 busy=true;assert(!c.set(true)&&resets==0);busy=false;
 failures=1;assert(!c.set(true)&&resets==2&&!c.pixelviewReceivePrecision&&current.output_format==original.output_format);
 assert(c.set(true)&&c.pixelviewReceivePrecision&&current.output_format==10&&current.colorspace==709);
 busy=true;int old=resets;assert(!c.set(false)&&resets==old&&c.pixelviewReceivePrecision);busy=false;
 failures=1;assert(!c.set(false)&&c.pixelviewReceivePrecision&&current.output_format==10);
 assert(c.set(false)&&!c.pixelviewReceivePrecision&&current.output_format==original.output_format&&current.colorspace==original.colorspace&&current.range==original.range);
 assert(current.fps_num==30&&current.fps_den==1); // Unset receive FPS follows the sender.
 c.pixelviewReceiveFPSNum=24;c.pixelviewReceiveFPSDen=1;
 assert(c.set(true)&&current.fps_num==24&&current.fps_den==1); // Receive FPS is independent of the sender's 30.
 assert(c.set(false)&&current.fps_num==30&&current.fps_den==1); // Sender FPS restored untouched.
 // HDR receive: Rec.2100 canvas with the operator's nits; the sender's levels come back.
 hdr_peak=800.f;c.pixelviewReceiveHdr=1;c.pixelviewReceiveHdrNits=1000;
 assert(c.set(true)&&current.output_format==10&&current.colorspace==2084&&current.range==1&&hdr_peak==1000.f&&sdr_white==300.f);
 assert(c.set(false)&&current.colorspace==original.colorspace&&hdr_peak==800.f&&sdr_white==300.f);
 c.pixelviewReceiveHdr=2;c.pixelviewReceiveHdrNits=600;
 assert(c.set(true)&&current.colorspace==2067&&hdr_peak==600.f);
 assert(c.set(false)&&hdr_peak==800.f);
 c.pixelviewReceiveHdr=0;
 assert(c.set(true)&&current.colorspace==709&&hdr_peak==800.f); // SDR receive keeps the sender's peak.
 assert(c.set(false));
 failures=2;assert(!c.set(true)&&c.pixelviewReceivePrecisionFault);
}
'''.replace('BODY',switch)
  with tempfile.TemporaryDirectory() as td:
   p=pathlib.Path(td);(p/'t.cpp').write_text(code)
   subprocess.run(['clang++','-std=c++17',str(p/'t.cpp'),'-o',str(p/'t')],check=True)
   subprocess.run([str(p/'t')],check=True)
if __name__=='__main__':unittest.main()
