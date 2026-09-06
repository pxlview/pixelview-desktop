"""AMF no-B-picture policy: actual OBS parser + extracted frontend policy.

SDK names/types verified in GPUOpen-LibrariesAndSDKs/AMF at
c35f613aea2e5057a688c979e75b1cf24253297e, amf/public/include/components/:
VideoEncoderVCE.h:195,263 and VideoEncoderAV1.h:381,382 (all amf_int64).
HEVC has no corresponding count properties in that SDK or native init branch.
No SDK/device execution is implied by these portable tests.
"""
import unittest
import test_nvenc_policy as native

ROOT = native.ROOT


class AmfPolicy(unittest.TestCase):
    def test_custom_bpicture_counts_cannot_override_zero(self):
        native.NvencPolicy.run_policy(self, r'''
 for (const char *id : {"h264_texture_amf", "av1_texture_amf"}) {
  const bool av1 = strcmp(id, "av1_texture_amf") == 0;
  const std::string count = av1 ? "Av1MaxConsecutiveBPictures" : "MaxConsecutiveBPictures";
  const std::string pattern = av1 ? "Av1BPicturesPattern" : "BPicturesPattern";
  for (const std::string &key : {count, pattern}) {
   for (const std::string &value : {"4", "0", "-1", "4junk", "=4", "TRUE", "false", "\t4", "4\nquality=speed"}) {
    const std::string input = "  quality=balanced  " + key + "=" + value + "  " + key + "=5 g=120  ";
    obs_data_t d; d.values = {{"ffmpeg_opts",input},{"opts","frameIntervalP=4"},{"tune","uhq"},{"bf","4"},{"profile","main"}};
    const auto before = obs_parse_options(input.c_str());
    PixelviewEnforceNoBFrames(id, &d);
    assert(d.values["ffmpeg_opts"] == "  quality=balanced     g=120  ");
    const auto after = obs_parse_options(d.values["ffmpeg_opts"].c_str());
    size_t kept = 0;
    for (size_t i = 0; i < before.count; ++i) {
     if (before.options[i].name == count || before.options[i].name == pattern) continue;
     assert(kept < after.count);
     assert(strcmp(before.options[i].name, after.options[kept].name) == 0);
     assert(strcmp(before.options[i].value, after.options[kept].value) == 0);
     ++kept;
    }
    assert(kept == after.count);
    assert(d.values["bf"] == "0" && d.values["bframes"] == "false");
    assert(d.values["opts"] == "frameIntervalP=4" && d.values["tune"] == "uhq" && d.values["profile"] == "main");
    const auto once = d.values;
    PixelviewEnforceNoBFrames(id, &d);
    assert(d.values == once);
    obs_free_options(before); obs_free_options(after);
   }
  }
 }
''')

    def test_amf_preserves_ignored_tokens_unrelated_bytes_and_other_codecs(self):
        native.NvencPolicy.run_policy(self, r'''
 for (const char *id : {"h264_texture_amf", "av1_texture_amf"}) {
  const std::string key = strcmp(id, "av1_texture_amf") == 0 ? "Av1BPicturesPattern" : "BPicturesPattern";
  const std::string other = strcmp(id, "av1_texture_amf") == 0 ? "BPicturesPattern=4" : "Av1BPicturesPattern=4";
  for (const std::string &input : {std::string(""), std::string("  "), key + "= " + key + " =4 =" + key + "=4",
       "\t" + key + "=4", "quality=speed\t" + key + "=4", "quality=speed\n" + key + "=4",
       "  bPicturesPattern=4 AV1BPicturesPattern=4 " + other + "  bf=4 qp_b=20 bf_ref=true bf_delta_qp=2 AdaptiveMiniGOP=true Av1AdaptiveMiniGop=true  "}) {
   obs_data_t d; d.values["ffmpeg_opts"] = input;
   PixelviewEnforceNoBFrames(id, &d);
   assert(d.values["ffmpeg_opts"] == input);
   // Adding a real override must not disturb malformed/ignored words either.
   const auto before = obs_parse_options(input.c_str());
   d.values["ffmpeg_opts"] += " " + key + "=4";
   PixelviewEnforceNoBFrames(id, &d);
   assert(d.values["ffmpeg_opts"] == input + " ");
   const auto after = obs_parse_options(d.values["ffmpeg_opts"].c_str());
   assert(before.count == after.count && before.ignored_word_count == after.ignored_word_count);
   for (size_t i = 0; i < before.ignored_word_count; ++i)
    assert(strcmp(before.ignored_words[i], after.ignored_words[i]) == 0);
   obs_free_options(before); obs_free_options(after);
  }
 }
 for (const char *id : {"h265_texture_amf", "h264_fallback_amf", "h265_fallback_amf", "av1_fallback_amf",
      "obs_nvenc_h264_tex", "obs_nvenc_hevc_tex", "obs_nvenc_av1_tex", "obs_x264", "obs_qsv11_v2",
      "obs_qsv11_hevc", "obs_qsv11_av1", "com.apple.videotoolbox.videoencoder.ave.hevc",
      "ffmpeg_vaapi_tex", "ffmpeg_nvenc", "ffmpeg_hevc_nvenc", "unknown"}) {
  const std::string input = "  BPicturesPattern=4 MaxConsecutiveBPictures=5 Av1BPicturesPattern=4 Av1MaxConsecutiveBPictures=5  ";
  obs_data_t d; d.values["ffmpeg_opts"] = input;
  PixelviewEnforceNoBFrames(id, &d);
  assert(d.values["ffmpeg_opts"] == input);
 }
''')

    def test_native_registration_consumer_and_callback_contracts(self):
        source = (ROOT / 'plugins/obs-ffmpeg/texture-amf.cpp').read_text()
        consumer = (ROOT / 'plugins/obs-ffmpeg/texture-amf-opts.hpp').read_text()
        for codec, prefix in (('avc', 'h264'), ('hevc', 'h265'), ('av1', 'av1')):
            registration = source.split(f'static void register_{codec}()', 1)[1].split('\n}', 1)[0]
            public, internal = registration.split(f'amf_encoder_info.id = "{prefix}_fallback_amf"', 1)
            self.assertIn(f'amf_encoder_info.id = "{prefix}_texture_amf"', public)
            self.assertIn('obs_register_encoder(&amf_encoder_info)', public)
            self.assertNotIn('OBS_ENCODER_CAP_INTERNAL', public)
            self.assertIn('OBS_ENCODER_CAP_INTERNAL', internal)
            init = source.split(f'static bool amf_{codec}_init(', 1)[1].split('\n}', 1)[0]
            self.assertIn('obs_parse_options(ffmpeg_opts)', init)
            self.assertIn('amf_apply_opt(enc, &opts.options[i])', init)
            for prop in ('MAX_CONSECUTIVE_BPICTURES', 'B_PIC_PATTERN'):
                setter = f'set_{codec}_property(enc, {prop}, bf)'
                if codec == 'hevc':
                    self.assertNotIn(prop, init)
                else:
                    self.assertLess(init.index(setter), init.index('obs_parse_options(ffmpeg_opts)'))
                    self.assertIn('int64_t bf = obs_data_get_int(settings, "bf")', init)
        self.assertIn('enc->amf_encoder->SetProperty(name, value)', source)
        self.assertIn('os_utf8_to_wcs(opt->name, 0, wname, _countof(wname))', consumer)
        self.assertIn('val = atoi(opt->value)', consumer)
        self.assertIn('astrcmpi(opt->value, "true") == 0', consumer)
        self.assertIn('astrcmpi(opt->value, "false") == 0', consumer)
        self.assertIn('set_amf_property(enc, wname, bool_val)', consumer)
        self.assertIn('set_amf_property(enc, wname, val)', consumer)
        for key in ('MaxConsecutiveBPictures', 'BPicturesPattern', 'Av1MaxConsecutiveBPictures', 'Av1BPicturesPattern'):
            self.assertNotIn(f'strcmp(opt->name, "{key}")', consumer)  # direct-property fallback
        ui = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        hide = ui.split('void PixelviewHideBFrameProperties', 1)[1].split('void PixelviewAlignProperties', 1)[0]
        self.assertIn('obs_property_set_visible(p, false)', hide)
        self.assertNotIn('obs_properties_remove', ui)
        self.assertIn('PixelviewHideBFrameProperties(obs_property_group_content(p))', hide)
        save = ui.split('bool OBSBasic::SavePixelviewEncoding', 1)[1].split('void OBSBasic::AdvancedPixelviewEncoding', 1)[0]
        before, after = save.split('obs_properties_apply_settings(props, settings)', 1)
        self.assertIn('PixelviewEnforceNoBFrames(id, settings)', before)
        self.assertLess(after.index('PixelviewEnforceNoBFrames(id, settings)'), after.index('obs_data_save_json_safe(settings'))
        self.assertIn('SavePixelviewEncoding(saved.toUtf8().constData(), data, true)', ui)
