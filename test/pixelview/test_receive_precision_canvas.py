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
struct obs_video_info {const char *graphics_module="opengl"; int output_format=1, colorspace=2,range=3;};
constexpr int VIDEO_FORMAT_P010=10,VIDEO_CS_709=709,VIDEO_RANGE_PARTIAL=1,OBS_VIDEO_SUCCESS=0;
obs_video_info current; int resets=0,failures=0; bool busy=false;
bool obs_get_video_info(obs_video_info *v){*v=current;return true;}
int obs_reset_video(obs_video_info *v){++resets;if(failures){--failures;return -1;}current=*v;return 0;}
struct Canvas {
 bool pixelviewReceivePrecision=false,pixelviewReceivePrecisionFault=false;
 obs_video_info pixelviewSendVideo={}; std::string pixelviewSendRenderModule;
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
 failures=2;assert(!c.set(true)&&c.pixelviewReceivePrecisionFault);
}
'''.replace('BODY',switch)
  with tempfile.TemporaryDirectory() as td:
   p=pathlib.Path(td);(p/'t.cpp').write_text(code)
   subprocess.run(['clang++','-std=c++17',str(p/'t.cpp'),'-o',str(p/'t')],check=True)
   subprocess.run([str(p/'t')],check=True)
if __name__=='__main__':unittest.main()
