"""Compiled encoding policy; native UI contracts are not hardware validation."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]

class Encoding(unittest.TestCase):
    def test_settings_first_native_integration(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertTrue('Qt::LeftToolBarArea' in main, 'Settings must be left of the editable native preview')
        impl = ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc'
        self.assertTrue(impl.exists(), 'Native encoder integration missing')
        text = impl.read_text()
        self.assertNotIn('OBS_ENCODER_CAP_HARDWARE', text)
        for contract in ('obs_enum_encoder_types', 'pixelview::isHardwareEncoder(id)', 'OBS_ENCODER_CAP_DEPRECATED',
                         'obs_encoder_defaults', 'obs_get_encoder_properties', 'OBSPropertiesView',
                         'streamEncoder.json', '"AdvOut", "Encoder"', '"AdvOut", "AudioEncoder"',
                         '"Advanced"', '"opus"', '"ultrafast"', '"zerolatency"', '"baseline"',
                         '"bframes"', '"keyint_sec"', '"CBR"', '"ColorFormat"',
                         'ResetVideo()', 'CreateAdvancedOutputHandler(this)', 'obs_data_save_json_safe', 'config_save_safe',
                         'setRange(1, 12)', 'QSignalBlocker', 'OBS_VIDEO_SUCCESS'):
            self.assertIn(contract, text)

    def test_actual_hardware_ids_not_software_or_lookalikes(self):
        header = ROOT / 'frontend/utility/PixelviewEncoding.hpp'
        self.assertIn('isHardwareEncoder', header.read_text(), 'Actual-ID hardware classifier missing')
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'test.cpp'
            src.write_text('''#include "frontend/utility/PixelviewEncoding.hpp"
#include <cassert>
int main() {
 using namespace pixelview;
 for (const char *id : {"com.apple.videotoolbox.videoencoder.ave.avc",
      "com.apple.videotoolbox.videoencoder.ave.hevc", "com.apple.videotoolbox.videoencoder.h264.gva", "obs_nvenc_h264_tex",
      "obs_nvenc_hevc_tex", "obs_nvenc_av1_tex", "ffmpeg_nvenc", "ffmpeg_hevc_nvenc",
      "obs_qsv11_v2", "obs_qsv11_hevc", "obs_qsv11_av1",
      "h264_texture_amf", "h265_texture_amf", "av1_texture_amf",
      "ffmpeg_vaapi_tex", "hevc_ffmpeg_vaapi_tex", "av1_ffmpeg_vaapi_tex"})
   assert(isHardwareEncoder(id));
 for (const char *id : {"obs_x264", "ffmpeg_aom_av1", "ffmpeg_svt_av1",
      "com.apple.videotoolbox.videoencoder.h264",
      "com.apple.videotoolbox.videoencoder.hevc.vcp",
      "com.apple.videotoolbox.videoencoder.hevc.ave", "nvenc_hevc",
      "obs_nvenc_future_software", "fake_obs_qsv11_v2", "", "unknown"})
   assert(!isHardwareEncoder(id));
 std::vector<EncoderChoice> registered;
 auto add = [&](const char *id, const char *codec) {
   registered.push_back({id, codec, isHardwareEncoder(id)});
 };
 add("obs_x264", "h264");
 add("com.apple.videotoolbox.videoencoder.hevc.vcp", "hevc");
 assert(preferredEncoder(registered, true) == 0);
 add("obs_nvenc_hevc_tex", "hevc");
 assert(preferredEncoder(registered, true) == 2);
 add("com.apple.videotoolbox.videoencoder.ave.hevc", "hevc");
 assert(preferredEncoder(registered, true) == 3);
 registered.clear(); assert(preferredEncoder(registered, true) == -1);
}''')
            binary = pathlib.Path(tmp) / 'test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_h264_defaults_use_native_baseline_without_x264_only_knobs(self):
        text = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        defaults = text.split('PixelviewDefaultList(data, props, "rate_control", "CBR");', 1)[1].split('for (const auto *key', 1)[0]
        harness = '''#include <cassert>
#include <cstring>
#include <map>
#include <string>
using Data = std::map<std::string,std::string>;
const char *obs_get_encoder_codec(const char *id) { return std::string(id).find("hevc") != std::string::npos ? "hevc" : "h264"; }
void PixelviewDefaultList(Data &data, const char *id, const char *key, const char *value) {
 if (std::string(key) == "profile") {
  if (std::string(obs_get_encoder_codec(id)) == "hevc") { if (std::string(value) == "main") data[key] = value; }
  else if (std::string(value) == "baseline" || std::string(value) == "high") data[key] = value;
 } else data[key] = value;
}
Data defaults(const char *id) {
 Data data{{"profile","high"}}; const char *props = id;
/* DEFAULTS */
 return data;
}
int main() {
 for (const char *id : {"obs_x264", "com.apple.videotoolbox.videoencoder.ave.avc", "obs_nvenc_h264_tex"}) {
  auto data = defaults(id); assert(data["profile"] == "baseline");
  if (std::string(id) == "obs_x264") { assert(data["tune"] == "zerolatency"); assert(data["preset"] == "ultrafast"); }
  else { assert(data.count("tune") == 0 && data.count("preset") == 0); }
 }
 assert(defaults("com.apple.videotoolbox.videoencoder.ave.hevc")["profile"] == "main");
}'''
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'test.cpp'
            src.write_text(harness.replace('/* DEFAULTS */', defaults))
            binary = pathlib.Path(tmp) / 'test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_profile_popup_and_unchanged_model_are_not_mutated(self):
        text = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        refresh = text.split('void OBSBasic::RefreshPixelviewEncoding()', 1)[1]
        block = refresh.split('obs_properties_apply_settings(props, data);', 1)[1].split('const auto kbps', 1)[0]
        harness = (ROOT / 'test/pixelview/encoding_refresh_harness.cpp.in').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'test.cpp'
            src.write_text(harness.replace('/* PRODUCTION_REFRESH */', block))
            binary = pathlib.Path(tmp) / 'test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_save_transaction_faults_and_busy_guard(self):
        text = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        body = text.split('bool OBSBasic::SavePixelviewEncoding', 1)[1].split('void OBSBasic::AdvancedPixelviewEncoding', 1)[0]
        harness = (ROOT / 'test/pixelview/encoding_save_harness.cpp.in').read_text()
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        busy = main.split('bool OBSBasic::PixelviewSettingsBusy() const', 1)[1].split('\nvoid ', 1)[0]
        harness = harness.replace('/* PRODUCTION_BUSY */', 'bool PixelviewSettingsBusy() const' + busy)
        policy = text.split('// BEGIN B-FRAME POLICY', 1)[1].split('// END B-FRAME POLICY', 1)[0]
        harness = harness.replace('/* BFRAME_POLICY */', policy)
        consumer = (ROOT / 'frontend/utility/AdvancedOutput.cpp').read_text().split('inline void AdvancedOutput::SetupStreaming()', 1)[1]
        rescale = consumer.split('int multiTrackAudioMixes', 1)[0].split('{', 1)[1]
        rescale += 'unsigned int cx = 0, cy = 0;\n' + 'if (rescaleFilter' + consumer.split('if (rescaleFilter', 1)[1].split('if (!is_multitrack_output)', 1)[0]
        rescale += 'obs_encoder_set_scaled_size' + consumer.split('obs_encoder_set_scaled_size', 1)[1].split('const char *id', 1)[0]
        harness = harness.replace('/* PRODUCTION_RESCALE */', rescale)
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            src = tmp / 'test.cpp'
            src.write_text(harness.replace('/* PRODUCTION_SAVE */', 'bool OBSBasic::SavePixelviewEncoding' + body))
            binary = tmp / 'test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(src), '-o', str(binary)], check=True)
            # A nonempty directory reliably exercises filesystem::remove's error overload.
            bad_json = tmp / 'streamEncoder.json'
            bad_json.mkdir()
            (bad_json / 'keep').write_text('not removable as a file')
            for case in ('unpaired', 'rescale-initialize', 'pending', 'lifecycle', 'rescale', 'rescale-main', 'rescale-main42210', 'json-video-rollback', 'config-delete-rollback', 'construct', 'construct-rollback', 'config', 'json-partial', 'json-partial-rollback'):
                with self.subTest(case=case):
                    profile = tmp
                    if case.startswith('json-partial'):
                        profile = tmp / case
                        profile.mkdir()
                    result = subprocess.run([str(binary), case, str(profile)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_registered_encoder_preference_and_color_policy(self):
        header = ROOT / 'frontend/utility/PixelviewEncoding.hpp'
        self.assertTrue(header.exists(), 'Encoding production policy missing')
        self.assertTrue('profileRangeSupported' in header.read_text(), 'P216 full-range guard missing')
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'test.cpp'
            src.write_text('''#include "frontend/utility/PixelviewEncoding.hpp"
#include <cassert>
int main() {
 using namespace pixelview;
 std::vector<EncoderChoice> enc = {{"obs_x264","h264",false}, {"obs_nvenc_hevc_tex","hevc",true}, {"com.apple.videotoolbox.videoencoder.ave.hevc","hevc",true}};
 assert(preferredEncoder(enc,true) == 2);
 assert(preferredEncoder(enc,false) == 1);
 enc.pop_back(); enc.pop_back(); assert(preferredEncoder(enc,true) == 0);
 enc.clear(); assert(preferredEncoder(enc,true) == -1);
 assert(quickBitrateKbps(1) == 1000 && quickBitrateKbps(12) == 12000);
 assert(quickBitrateKbps(0) == 0 && quickBitrateKbps(13) == 0);
 assert(profileFormat("main") == "NV12");
 assert(profileFormat("main10") == "P010");
 assert(profileFormat("main42210") == "P216");
 assert(profileFormat("unknown").empty());
 assert(profileRangeSupported("main42210", "Partial"));
 assert(!profileRangeSupported("main42210", "Full"));
 assert(profileRangeSupported("main10", "Full"));
}''')
            binary = pathlib.Path(tmp) / 'test'
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT),str(src),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
