"""Production policy execution plus explicit Qt integration contracts (not GUI smoke)."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]

class SidebarPolish(unittest.TestCase):
    def test_compact_grouped_sidebar_and_authentic_wordmark(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        init = main.split('void OBSBasic::InitPixelview()', 1)[1].split('void OBSBasic::RefreshPixelviewFPS()', 1)[0]
        self.assertIn('new QVBoxLayout(sidebar)', init)
        self.assertIn('setContentsMargins(16, 16, 16, 16)', init)
        self.assertIn('setSpacing(12)', init)
        self.assertEqual(init.count('bar->addWidget('), 1)
        self.assertNotIn('QStringLiteral("Refresh")', init)
        self.assertFalse('Connect a device and refresh.' in main, 'Device help must explain automatic refresh')
        self.assertIn('pixelviewRefreshTimer->start(2000)', init)
        self.assertIn('QStringLiteral("Desktop")', init)
        self.assertIn(':/res/images/pixelview-wordmark.png', init)
        encoding = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        self.assertIn('rates->addWidget(pixelviewFPS, 1)', encoding)
        self.assertIn('rates->addWidget(pixelviewBitrate, 1)', encoding)
        self.assertIn('setSuffix(QStringLiteral(" Mbps"))', encoding)
        self.assertNotIn('Encoding • 1920', encoding)
        self.assertNotIn('pixelviewEncodingStatus->setText(QStringLiteral("%1 kbps', encoding)
        self.assertIn('text-align: left', init)
        self.assertIn('setFixedHeight(36)', init)
        self.assertIn('pixelview-wordmark.png', (ROOT / 'frontend/forms/obs.qrc').read_text())

    def test_footer_content_and_custom_fps_are_self_explanatory(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        init = main.split('void OBSBasic::InitPixelview()', 1)[1].split('void OBSBasic::RefreshPixelviewFPS()', 1)[0]
        self.assertTrue('pixelviewStatus->setSizePolicy(QSizePolicy::Expanding' in init,
                        'One left-aligned status line needs the full footer width')
        footer = init.split('auto *footer =', 1)[1].split('setMinimumSize', 1)[0]
        self.assertEqual(footer.count('footer->addWidget('), 1,
                         'Do not split the footer into competing, wrapping labels')
        self.assertTrue('control->ensurePolished();' in init,
                        'Apply theme minimums before fixing equal control heights')
        self.assertTrue('QStringLiteral("%1/%2 fps")' in main,
                        'Custom frame rates need units now that the FPS heading is removed')

    def test_native_callback_filter_runs_before_every_property_render(self):
        view = (ROOT / 'shared/properties-view/properties-view.cpp').read_text()
        body = view.split('void OBSPropertiesView::RefreshProperties()', 1)[1].split('void OBSPropertiesView::SetScrollPos', 1)[0]
        self.assertIn('emit PropertiesAboutToRefresh(properties)', body)
        self.assertLess(body.index('emit PropertiesAboutToRefresh(properties)'), body.index('AddProperty(property, layout)'))
        encoding = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        self.assertIn('&OBSPropertiesView::PropertiesAboutToRefresh', encoding)
        self.assertIn('PixelviewHideBFrameProperties(props)', encoding)
        self.assertNotIn('obs_property_set_modified_callback', encoding)
        self.assertNotIn('obs_properties_remove_by_name', encoding)
        self.assertIn('setLabelAlignment(Qt::AlignLeft', encoding)
        self.assertIn('&OBSPropertiesView::PropertiesRefreshed', encoding)


    def test_filter_preserves_native_callbacks_across_reload_and_groups(self):
        text = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        policy = 'void PixelviewHideBFrameProperties' + text.split('void PixelviewHideBFrameProperties', 1)[1].split('void PixelviewAlignProperties', 1)[0]
        harness = r'''
#include <cassert>
#include <cstring>
#include <vector>
struct obs_property_t;
struct obs_properties_t { obs_property_t *first; };
struct obs_property_t { const char *name; int type; bool visible; obs_property_t *next; obs_properties_t *group; void (*modified)(obs_property_t *); };
constexpr int OBS_PROPERTY_GROUP = 1;
obs_property_t *obs_properties_first(obs_properties_t *p) { return p ? p->first : nullptr; }
void obs_property_next(obs_property_t **p) { *p = (*p)->next; }
const char *obs_property_name(obs_property_t *p) { return p->name; }
int obs_property_get_type(obs_property_t *p) { return p->type; }
obs_properties_t *obs_property_group_content(obs_property_t *p) { return p->group; }
void obs_property_set_visible(obs_property_t *p, bool value) { p->visible = value; }
/* FILTER */
int calls = 0;
void native_callback(obs_property_t *p) { ++calls; p->visible = true; }
int main() {
 for (int reload = 0; reload < 3; ++reload) {
  obs_property_t reference{"bframe_ref_mode",0,true,nullptr,nullptr,native_callback};
  obs_property_t integer{"bf",0,true,&reference,nullptr,native_callback};
  obs_properties_t nested{&integer};
  obs_property_t group{"advanced",OBS_PROPERTY_GROUP,true,nullptr,&nested,native_callback};
  obs_property_t profile{"profile",0,true,&group,nullptr,native_callback};
  obs_property_t boolean{"bframes",0,true,&profile,nullptr,native_callback};
  obs_properties_t props{&boolean};
  for (int change = 0; change < 3; ++change) {
   for (auto *p : {&boolean, &profile, &integer, &reference}) p->modified(p);
   PixelviewHideBFrameProperties(&props);
   assert(!boolean.visible && !integer.visible && !reference.visible);
   assert(profile.visible && group.visible);
   assert(boolean.modified == native_callback && integer.modified == native_callback);
  }
 }
 assert(calls == 36);
 PixelviewHideBFrameProperties(nullptr);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'test.cpp'
            src.write_text(harness.replace('/* FILTER */', policy))
            binary = pathlib.Path(tmp) / 'test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_bframe_policy_normalizes_saved_native_and_x264_overrides(self):
        text = (ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        self.assertIn('void PixelviewEnforceNoBFrames(', text, 'Saved overrides currently bypass the B-frame policy')
        policy = text.split('// BEGIN B-FRAME POLICY', 1)[1].split('// END B-FRAME POLICY', 1)[0]
        harness = r'''
#include "frontend/utility/PixelviewEncoding.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
struct obs_data_t { std::map<std::string,std::string> values; };
void obs_data_set_bool(obs_data_t *d, const char *k, bool v) { d->values[k] = v ? "true" : "false"; }
void obs_data_set_int(obs_data_t *d, const char *k, int v) { d->values[k] = std::to_string(v); }
void obs_data_set_string(obs_data_t *d, const char *k, const char *v) { d->values[k] = v; }
const char *obs_data_get_string(obs_data_t *d, const char *k) { return d->values[k].c_str(); }
/* POLICY */
int main() {
 obs_data_t d;
 d.values = {{"bframes","true"},{"bf","4"},{"profile","main10"},{"bitrate","19000"}};
 PixelviewEnforceNoBFrames("com.apple.videotoolbox.videoencoder.ave.hevc", &d);
 assert(d.values["bframes"] == "false" && d.values["bf"] == "0");
 assert(d.values["profile"] == "main10" && d.values["bitrate"] == "19000");
 PixelviewEnforceNoBFrames("obs_qsv11_hevc", &d);
 assert(d.values["bframes"] == "0" && d.values["bf"] == "0");
 for (const char *opts : {"", "bframes=8", "ref=3 bframes=8 bframes=4", "bframes=8 ref=3", "preset=slow tune=film", " bframes=6   ref=3 "}) {
  d.values["x264opts"] = opts;
  PixelviewEnforceNoBFrames("obs_x264", &d);
  auto once = d.values["x264opts"];
  assert(once.size() >= 9 && once.substr(once.size()-9) == "bframes=0");
  assert(once.find("bframes=8") == std::string::npos && once.find("bframes=4") == std::string::npos);
  PixelviewEnforceNoBFrames("obs_x264", &d);
  assert(d.values["x264opts"] == once);
 }
 assert(pixelview::withoutBFrameOverrides("ref=3 bframes=5 aq-mode=2") == "ref=3 aq-mode=2 bframes=0");
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'test.cpp'
            src.write_text(harness.replace('/* POLICY */', policy))
            binary = pathlib.Path(tmp) / 'test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        loader = text.split('OBSData PixelviewEncoderData(', 1)[1].split('std::string PixelviewOpusEncoder()', 1)[0]
        self.assertIn('PixelviewEnforceNoBFrames(id, data)', loader)
        self.assertNotIn('obs_data_apply(data, saved); return', loader)
        startup = text.split('void OBSBasic::InitPixelviewEncoding', 1)[1].split('void OBSBasic::RefreshPixelviewEncoding', 1)[0]
        self.assertIn('Normalize persisted overrides before the native output handler is reused.', startup)
        self.assertIn('SavePixelviewEncoding(saved.toUtf8().constData(), data)', startup)
        save = text.split('bool OBSBasic::SavePixelviewEncoding', 1)[1]
        self.assertLess(save.index('PixelviewEnforceNoBFrames(id, settings)'), save.index('obs_data_save_json_safe(settings'))
