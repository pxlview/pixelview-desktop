"""Execute frontend policy against the native option parser (no GPU/Qt required)."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class NvencPolicy(unittest.TestCase):
    def test_native_consumers_and_startup_save_policy_contract(self):
        nvenc = (ROOT / 'plugins/obs-nvenc/nvenc.c').read_text()
        props = (ROOT / 'plugins/obs-nvenc/nvenc-properties.c').read_text()
        consumer = (ROOT / 'plugins/obs-nvenc/nvenc-opts-parser.c').read_text()
        self.assertIn('APPLY_INT_OPT(frameIntervalP, int32_t, PRIi32)', consumer)
        self.assertIn('strcmp(opt->name, #opt_name) == 0', consumer)
        self.assertIn('strtol(opt->value, NULL, 10)', consumer)
        self.assertIn('props->opts = obs_parse_options(props->opts_str)', props)
        self.assertIn('obs_data_set_default_string(settings, "tune", "hq")', props)
        self.assertIn('add_tune("hq")', props)
        self.assertIn('astrcmpi(tuning, "uhq") == 0', nvenc)
        self.assertIn('nv_tuning == NV_ENC_TUNING_INFO_ULTRA_HIGH_QUALITY && enc->props.bf < 4', nvenc)
        self.assertIn('enc->props.bf = 4;', nvenc)
        self.assertIn('config->frameIntervalP = gop_size == 1 ? 0 : (int32_t)enc->props.bf + 1', nvenc)
        for codec in ('h264', 'hevc', 'av1'):
            self.assertIn(f'.id = "obs_nvenc_{codec}_tex"', nvenc)
            self.assertIn(f'obs_register_encoder(&{codec}_nvenc_info)', nvenc)
        self.assertEqual(nvenc.count('if (!apply_user_args(enc))'), 3)
        text = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        loader = text.split('OBSData PixelviewEncoderData(', 1)[1].split('std::string PixelviewOpusEncoder()', 1)[0]
        self.assertIn('PixelviewEnforceNoBFrames(id, data)', loader)
        startup = text.split('void OBSBasic::InitPixelviewEncoding', 1)[1].split('void OBSBasic::RefreshPixelviewEncoding', 1)[0]
        self.assertIn('SavePixelviewEncoding(saved.toUtf8().constData(), data, true)', startup)
        save = text.split('bool OBSBasic::SavePixelviewEncoding', 1)[1].split('void OBSBasic::AdvancedPixelviewEncoding', 1)[0]
        before, after = save.split('obs_properties_apply_settings(props, settings)', 1)
        self.assertIn('PixelviewEnforceNoBFrames(id, settings)', before)
        self.assertLess(after.index('PixelviewEnforceNoBFrames(id, settings)'), after.index('obs_data_save_json_safe(settings'))

    def run_policy(self, checks):
        text = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        policy = text.split('// BEGIN B-FRAME POLICY', 1)[1].split('// END B-FRAME POLICY', 1)[0]
        # Compile the actual shared parser and actual space splitter as C. Only
        # allocation is shimmed; no reimplementation of option grammar in tests.
        parser = (ROOT / 'shared/opts-parser/opts-parser.c').read_text()
        parser = '\n'.join(line for line in parser.splitlines() if not line.startswith('#include'))
        dstr = (ROOT / 'libobs/util/dstr.c').read_text()
        split = 'char **strlist_split' + dstr.split('char **strlist_split', 1)[1].split('void dstr_init_copy_strref', 1)[0]
        preamble = '''
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "shared/opts-parser/opts-parser.h"
#define bmalloc malloc
#define bfree free
static char *bstrdup_n(const char *s, size_t n) {
 char *p = malloc(n + 1); memcpy(p, s, n); p[n] = 0; return p;
}
'''
        harness = r'''
#include "frontend/utility/PixelviewEncoding.hpp"
#include "shared/opts-parser/opts-parser.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
struct obs_data_t { std::map<std::string,std::string> values; };
void obs_data_set_bool(obs_data_t *d, const char *k, bool v) { d->values[k] = v ? "true" : "false"; }
void obs_data_set_int(obs_data_t *d, const char *k, int v) { d->values[k] = std::to_string(v); }
void obs_data_set_string(obs_data_t *d, const char *k, const char *v) { d->values[k] = v; }
const char *obs_data_get_string(obs_data_t *d, const char *k) { return d->values[k].c_str(); }
/* POLICY */
int main() {
/* CHECKS */
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            (tmp / 'parser.c').write_text(preamble + split + parser)
            (tmp / 'test.cpp').write_text(harness.replace('/* POLICY */', policy).replace('/* CHECKS */', checks))
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), '-c', str(tmp / 'parser.c'), '-o', str(tmp / 'parser.o')], check=True)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(tmp / 'test.cpp'), str(tmp / 'parser.o'), '-o', str(tmp / 'test')], check=True)
            subprocess.run([str(tmp / 'test')], check=True)

    def test_native_nvenc_uhq_is_replaced_with_supported_hq(self):
        self.run_policy(r'''
 for (const char *id : {"obs_nvenc_h264_tex", "obs_nvenc_hevc_tex", "obs_nvenc_av1_tex"}) {
  for (const char *tune : {"uhq", "UHQ", "UhQ", "uHq"}) {
   obs_data_t d; d.values = {{"opts","aqStrength=8"},{"tune",tune},{"bf","0"}};
   PixelviewEnforceNoBFrames(id, &d);
   assert(d.values["tune"] == "hq");
   assert(d.values["opts"] == "aqStrength=8" && d.values["bf"] == "0");
   auto once = d.values;
   PixelviewEnforceNoBFrames(id, &d);
   assert(d.values == once);
  }
  for (const char *tune : {"hq", "ll", "ull", "HQ", "", " uhq", "uhq ", "unknown"}) {
   obs_data_t d; d.values["tune"] = tune;
   PixelviewEnforceNoBFrames(id, &d);
   assert(d.values["tune"] == tune);
  }
 }
 for (const char *id : {"obs_x264", "obs_qsv11_v2", "obs_qsv11_hevc", "obs_qsv11_av1",
      "com.apple.videotoolbox.videoencoder.ave.hevc", "h264_texture_amf", "h265_texture_amf",
      "av1_texture_amf", "ffmpeg_vaapi_tex", "ffmpeg_nvenc", "ffmpeg_hevc_nvenc",
      "obs_nvenc_h264_soft", "obs_nvenc_hevc_soft", "obs_nvenc_av1_soft", "unknown"}) {
  obs_data_t d; d.values = {{"opts","frameIntervalP=4"},{"tune","uhq"},{"ffmpeg_opts","bf=4"}};
  PixelviewEnforceNoBFrames(id, &d);
  assert(d.values["opts"] == "frameIntervalP=4" && d.values["tune"] == "uhq");
  assert(d.values["ffmpeg_opts"] == "bf=4");
 }
''')

    def test_native_nvenc_custom_interval_cannot_override_no_bframes(self):
        self.run_policy(r'''
 for (const char *id : {"obs_nvenc_h264_tex", "obs_nvenc_hevc_tex", "obs_nvenc_av1_tex"}) {
  for (const char *input : {"frameIntervalP=4", "", "  aqStrength=8  frameIntervalP=4  ",
       "frameIntervalP=1 frameIntervalP=8 frameIntervalP=4", "frameIntervalP=-1", "frameIntervalP=4junk",
       "frameIntervalP==4", "frameIntervalP=\t4", "frameIntervalP=4\naqStrength=8",
       "frameIntervalP= frameIntervalP =4 =frameIntervalP=4", "FrameIntervalP=4",
       "\tframeIntervalP=4", "aqStrength=8\tframeIntervalP=4", "aqStrength=8\nframeIntervalP=4"}) {
   obs_data_t d; d.values = {{"opts",input},{"tune","hq"},{"bf","4"},{"profile","main"}};
   const auto before = obs_parse_options(input);
   PixelviewEnforceNoBFrames(id, &d);
   const auto once = d.values;
   const auto after = obs_parse_options(d.values["opts"].c_str());
   size_t kept = 0;
   for (size_t i = 0; i < before.count; ++i) {
    if (strcmp(before.options[i].name, "frameIntervalP") == 0) continue;
    assert(kept < after.count);
    assert(strcmp(before.options[i].name, after.options[kept].name) == 0);
    assert(strcmp(before.options[i].value, after.options[kept].value) == 0);
    ++kept;
   }
   assert(kept == after.count);
   assert(before.ignored_word_count == after.ignored_word_count);
   for (size_t i = 0; i < before.ignored_word_count; ++i)
    assert(strcmp(before.ignored_words[i], after.ignored_words[i]) == 0);
   assert(d.values["bf"] == "0" && d.values["tune"] == "hq" && d.values["profile"] == "main");
   PixelviewEnforceNoBFrames(id, &d);
   assert(d.values == once);
   obs_free_options(before); obs_free_options(after);
  }
 }
''')
