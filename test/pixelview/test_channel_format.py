"""Execute the toolbar's native-option validation with simulated device lists."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ChannelFormat(unittest.TestCase):
    def test_device_switch_validates_channels_without_muting_supported_audio(self):
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        select = main.split('void OBSBasic::SelectPixelviewDevice(', 1)[1].split('\nvoid ', 1)[0]
        validation = select.split('// Dependent options come from the chosen hardware, not a hand-written mode table.', 1)[1]
        validation = validation.split('obs_properties_apply_settings(props, settings);', 1)[0]
        code = r'''
#include "frontend/utility/PixelviewCapturePolicy.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct Data { std::map<std::string, int64_t> values; };
using OBSDataAutoRelease = Data *;
struct Option { std::vector<int64_t> values; std::vector<bool> disabled; };
using Props = std::map<std::string, Option>;
Data defaults{{{"channel_format", 2}}}; // DeckLink SPEAKERS_STEREO
Data *obs_get_source_defaults(const char *id) {
    assert(std::string(id) == "decklink-input");
    return &defaults;
}
int64_t obs_data_get_int(Data *data, const char *key) { return data->values[key]; }
void obs_data_set_int(Data *data, const char *key, int64_t value) { data->values[key] = value; }
Option *obs_properties_get(Props *props, const char *key) {
    auto it = props->find(key);
    return it == props->end() ? nullptr : &it->second;
}
size_t obs_property_list_item_count(Option *option) { return option->values.size(); }
bool obs_property_list_item_disabled(Option *option, size_t i) { return option->disabled[i]; }
int64_t obs_property_list_item_int(Option *option, size_t i) { return option->values[i]; }
void obs_property_modified(Option *, Data *) {}
void validate(Props *props, Data *settings) {
''' + validation + r'''
}
int main() {
    Props stereo{
        {"mode_id", {{1, 2}, {false, false}}},
        {"video_connection", {{1, 2}, {false, false}}},
        {"audio_connection", {{1, 2}, {false, false}}},
        {"channel_format", {{0, 2}, {false, false}}}
    };
    Data settings{{{"mode_id", 2}, {"video_connection", 2},
                   {"audio_connection", 9}, {"channel_format", 8}}};
    validate(&stereo, &settings);
    assert(settings.values["channel_format"] == 2 && "unsupported multichannel must fall back to stereo, not None");
    assert(settings.values["mode_id"] == 2);
    assert(settings.values["video_connection"] == 2);
    assert(settings.values["audio_connection"] == 1);
    for (int64_t supported : {0, 2}) {
        settings.values["channel_format"] = supported;
        validate(&stereo, &settings);
        assert(settings.values["channel_format"] == supported && "preserve supported Stereo and explicit None");
    }
    Props multichannel = stereo;
    multichannel["channel_format"] = {{0, 2, 8}, {false, false, false}};
    settings.values["channel_format"] = 8;
    validate(&multichannel, &settings);
    assert(settings.values["channel_format"] == 8);
    multichannel["channel_format"].disabled[2] = true;
    validate(&multichannel, &settings);
    assert(settings.values["channel_format"] == 2);
    stereo["channel_format"] = {{}, {}};
    settings.values["channel_format"] = 8;
    validate(&stereo, &settings);
    assert(settings.values["channel_format"] == 8);
    stereo.erase("channel_format");
    validate(&stereo, &settings);
    assert(settings.values["channel_format"] == 8);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'channel_format.cpp'
            binary = pathlib.Path(directory) / 'channel_format'
            source.write_text(code)
            subprocess.run(['c++', '-std=c++17', '-I', str(ROOT), str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
