"""Dedicated DeckLink UI contracts and compiled production startup regression.

No card/GPU is opened. Native event code executes against recorded output boundaries.
"""
import os
import pathlib
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
UI = ROOT / 'plugins/decklink-output-ui'


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def compile_run(code):
    with tempfile.TemporaryDirectory() as td:
        source = pathlib.Path(td) / 'test.cpp'
        executable = pathlib.Path(td) / 'test'
        source.write_text(code)
        subprocess.run(['clang++', '-std=c++17', str(source), '-o', str(executable)], check=True)
        return subprocess.run([str(executable)], capture_output=True, text=True)


class DecklinkOutputUI(unittest.TestCase):
    def test_native_form_and_program_start_stop(self):
        qt = next((ROOT / '.deps').glob('obs-deps-qt*/lib/QtWidgets.framework')).parent
        cpp = (UI / 'DecklinkOutputUI.cpp').read_text()
        native = (UI / 'decklink-ui-main.cpp').read_text()
        code = r'''
#include <QApplication>
#include <QLabel>
#include <cassert>
#include "ui_output.h"
const char *obs_module_text(const char *text) { return text; }
bool main_output_running=false;
int starts=0, stops=0, saves=0;
void output_start() { ++starts; main_output_running=true; }
void output_stop() { ++stops; main_output_running=false; }
''' + function(native, 'void output_toggle(') + r'''
class DecklinkOutputUI : public QDialog {
public:
    Ui_Output storage;
    Ui_Output *ui=&storage;
    DecklinkOutputUI() { ui->setupUi(this); }
    void SaveSettings() { ++saves; }
    void on_outputButton_clicked();
    void OutputStateChanged(bool active);
};
''' + function(cpp, 'void DecklinkOutputUI::on_outputButton_clicked(') + '\n' + function(cpp, 'void DecklinkOutputUI::OutputStateChanged(') + r'''
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    DecklinkOutputUI dialog;
    dialog.show(); app.processEvents();
    assert(dialog.findChildren<QPushButton *>().size() == 1);
    assert(dialog.findChildren<QLabel *>().size() == 1);
    assert(dialog.ui->label->text() == "Output");
    assert(dialog.ui->outputButton->isVisible());
    dialog.on_outputButton_clicked();
    assert(starts == 1 && stops == 0 && saves == 1);
    dialog.OutputStateChanged(true);
    assert(dialog.ui->outputButton->text() == "Stop" && dialog.ui->outputButton->isChecked());
    dialog.on_outputButton_clicked();
    assert(starts == 1 && stops == 1 && saves == 2);
    dialog.OutputStateChanged(false);
    assert(dialog.ui->outputButton->text() == "Start" && !dialog.ui->outputButton->isChecked());
}
'''
        with tempfile.TemporaryDirectory() as td:
            temp = pathlib.Path(td)
            subprocess.run([str(qt.parent / 'libexec/uic'), str(UI / 'forms/output.ui'), '-o', str(temp / 'ui_output.h')], check=True)
            source = temp / 'native.cpp'
            source.write_text(code)
            exe = temp / 'native'
            flags = ['clang++', '-std=c++17', '-fPIC', '-F' + str(qt), '-Wl,-rpath,' + str(qt)]
            for framework in ('QtWidgets', 'QtCore', 'QtGui'):
                flags += ['-I' + str(qt / (framework + '.framework/Headers')), '-framework', framework]
            subprocess.run(flags + [str(source), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'}, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_keyer_hidden_without_removing_native_properties(self):
        cpp = (UI / 'DecklinkOutputUI.cpp').read_text()
        setup = function(cpp, 'void DecklinkOutputUI::SetupPropertiesView(')
        self.assertIn('decklink_output_properties', setup)
        reload = function(cpp, 'static obs_properties_t *decklink_output_properties(')
        result = compile_run(r'''
#include <cassert>
#include <cstring>
struct obs_property_t { bool visible=true; };
struct obs_properties_t { obs_property_t keyer, force_sdr, device, mode, auto_start; } props;
obs_properties_t *obs_get_output_properties(const char *id) {
    assert(!strcmp(id,"decklink_output")); props={}; return &props;
}
obs_property_t *obs_properties_get(obs_properties_t *p, const char *name) {
    assert(p == &props); assert(!strcmp(name,"keyer")); return &p->keyer;
}
void obs_property_set_visible(obs_property_t *p, bool visible) { p->visible=visible; }
''' + reload + r'''
int main() {
    for (int reload=0; reload<3; ++reload) {
        auto *p=decklink_output_properties(nullptr);
        assert(p == &props && !p->keyer.visible);
        assert(p->force_sdr.visible && p->device.visible && p->mode.visible && p->auto_start.visible);
    }
}
''')
        self.assertEqual(result.returncode, 0, result.stderr)
        # The device callback still needs the hidden list: never remove it.
        native = (ROOT / 'plugins/decklink/decklink-output.cpp').read_text()
        callback = function(native, 'static bool decklink_output_device_changed(')
        self.assertNotIn('obs_property_set_visible', callback)
        self.assertIn('obs_properties_add_bool(props, FORCE_SDR, TEXT_FORCE_SDR)', native)

    def test_only_program_output_controls_and_runtime_remain(self):
        form = ET.parse(UI / 'forms/output.ui')
        self.assertEqual([w.attrib['name'] for w in form.findall('.//widget[@class="QPushButton"]')], ['outputButton'])
        self.assertEqual([s.text for s in form.findall('.//widget[@class="QLabel"]/property[@name="text"]/string')], ['Output'])
        cpp = (UI / 'DecklinkOutputUI.cpp').read_text()
        self.assertIn('SetupPropertiesView();', function(cpp, 'void DecklinkOutputUI::ShowHideDialog('))
        click = function(cpp, 'void DecklinkOutputUI::on_outputButton_clicked(')
        self.assertIn('SaveSettings();', click)
        self.assertIn('output_toggle();', click)
        state = function(cpp, 'void DecklinkOutputUI::OutputStateChanged(')
        self.assertIn('obs_module_text("Start")', state)
        self.assertIn('obs_module_text("Stop")', state)
        for name in ('DecklinkOutputUI.cpp', 'DecklinkOutputUI.h', 'decklink-ui-main.cpp', 'decklink-ui-main.h'):
            source = (UI / name).read_text()
            for obsolete in ('preview_output_', 'load_preview_settings', 'PreviewProperties', 'previewProperties', 'PreviewOutputStateChanged', 'context_preview'):
                self.assertNotIn(obsolete, source, name)
        self.assertIn('obs_get_main_texture()', (UI / 'decklink-ui-main.cpp').read_text())

    def test_stale_preview_autostart_is_never_loaded_or_started(self):
        event = function((UI / 'decklink-ui-main.cpp').read_text(), 'static void OBSEvent(')
        result = compile_run(r'''
#include <cassert>
#include <cstring>
enum obs_frontend_event { OBS_FRONTEND_EVENT_FINISHED_LOADING, OBS_FRONTEND_EVENT_EXIT };
struct Data { bool auto_start; } mainSettings{false}, stalePreview{true};
using OBSData = Data *;
int mainStarts=0, previewStarts=0, previewLoads=0, mainStops=0;
bool main_output_running=false, preview_output_running=false, shutting_down=false;
OBSData load_settings() { return &mainSettings; }
OBSData load_preview_settings() { ++previewLoads; return &stalePreview; }
bool obs_data_get_bool(OBSData data, const char *key) { assert(!strcmp(key,"auto_start")); return data->auto_start; }
void output_start() { ++mainStarts; main_output_running=true; }
void output_stop() { ++mainStops; main_output_running=false; }
void preview_output_start() { ++previewStarts; preview_output_running=true; }
void preview_output_stop() { preview_output_running=false; }
''' + event + r'''
int main() {
    OBSEvent(OBS_FRONTEND_EVENT_FINISHED_LOADING, nullptr);
    assert(previewStarts == 0 && previewLoads == 0);
    assert(mainStarts == 0);
    mainSettings.auto_start=true;
    OBSEvent(OBS_FRONTEND_EVENT_FINISHED_LOADING, nullptr);
    assert(mainStarts == 1 && previewStarts == 0 && previewLoads == 0);
    OBSEvent(OBS_FRONTEND_EVENT_EXIT, nullptr);
    assert(shutting_down && mainStops == 1 && !main_output_running);
}
''')
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
