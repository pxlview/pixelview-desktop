"""Audio contracts and compiled transaction policy; no hardware/audio execution."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class Audio(unittest.TestCase):
    def test_device_transaction_rolls_back_apply_and_save_failures(self):
        header = ROOT / 'frontend/utility/PixelviewAudio.hpp'
        self.assertTrue(header.exists(), 'Native audio transaction policy is missing')
        with tempfile.TemporaryDirectory() as temp:
            src = pathlib.Path(temp) / 'test.cpp'
            exe = pathlib.Path(temp) / 'test'
            src.write_text('''#include "frontend/utility/PixelviewAudio.hpp"
#include <cassert>
int main() {
 int live=0, disk=0, restores=0; bool applyOK=false, saveOK=true, restoreOK=true;
 auto change=[&] { return pixelview::changeAudio([&] {live=1; return applyOK;},
   [&] {if(saveOK) disk=live; return saveOK;},
   [&] {++restores; live=0; return restoreOK;});};
 assert(change()==pixelview::AudioChangeResult::ApplyFailed);
 assert(live==0 && disk==0 && restores==1);
 applyOK=true; saveOK=false;
 assert(change()==pixelview::AudioChangeResult::SaveFailed);
 assert(live==0 && disk==0 && restores==2);
 restoreOK=false;
 assert(change()==pixelview::AudioChangeResult::RollbackFailed);
 restoreOK=true; saveOK=true;
 assert(change()==pixelview::AudioChangeResult::Applied);
 assert(live==1 && disk==1 && restores==3);
}''')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_native_controls_lifetime_routing_and_persistence(self):
        path = ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc'
        self.assertTrue(path.exists(), 'Compact native audio controls are missing')
        audio = path.read_text()
        main = (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text()
        for text in ('new VolumeMeter(', 'OBSGetWeakRef', 'Qt::QueuedConnection',
                     'pixelviewAudioSignals.clear()', 'delete pixelviewMeter',
                     'obs_source_set_muted', 'obs_source_set_monitoring_enabled',
                     'Mute audio', 'Listen locally', 'SaveProjectNow()',
                     'obs_data_create_from_json_file', 'obs_save_source'):
            self.assertIn(text, audio)
        for forbidden in ('OBS_MONITORING_TYPE_MONITOR_ONLY', 'obs_source_create(',
                          'obs_source_set_volume', 'obs_source_set_audio_mixers', 'FitPixelview'):
            self.assertNotIn(forbidden, audio)
        self.assertIn('InitPixelviewAudio(ui->previewContainer);', main)
        shutdown = main.split('void OBSBasic::applicationShutdown()', 1)[1]
        self.assertIn('pixelviewAudioShuttingDown = true;', shutdown)
        self.assertIn('pixelviewAudioShuttingDown || isClosing()', audio)
        self.assertLess(shutdown.index('ClearPixelviewAudio();'), shutdown.index('QApplication::sendPostedEvents'))
        refresh = audio.split('void OBSBasic::RefreshPixelviewAudio()', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('obs_source_set_monitoring_enabled(source, false)', refresh)
        self.assertNotIn('obs_source_set_muted', refresh)
        self.assertNotIn('obs_set_audio_monitoring_device', refresh)

    def test_source_save_requires_canonical_monitoring_boolean(self):
        audio = (ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        save = audio.split('bool OBSBasic::SavePixelviewAudioSource(', 1)[1].split('\nvoid ', 1)[0]
        self.assertIn('obs_data_item_byname(entry, "monitoring")', save)
        self.assertIn('obs_data_item_gettype(monitoring) == OBS_DATA_BOOLEAN', save)
        self.assertIn('obs_data_item_release(&monitoring)', save)
        self.assertIn('return canonicalMonitoring &&', save)
        self.assertIn('obs_data_get_bool(entry, "monitoring") == obs_source_get_monitoring_enabled(source)', save)
        self.assertIn('obs_data_get_int(entry, "monitoring_enabled")', save)

    def test_unavailable_device_restores_saved_selection_before_warning(self):
        audio = (ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        select = audio.split('void OBSBasic::SelectPixelviewMonitorDevice(', 1)[1]
        rejected = select.split('if (device == devices.end()) {', 1)[1].split('\n\t}', 1)[0]
        before_warning = rejected.split('QMessageBox::warning', 1)[0]
        self.assertIn('const QSignalBlocker blocker(pixelviewMonitorDevice);', before_warning)
        self.assertIn('pixelviewMonitorDevice->setCurrentIndex(pixelviewMonitorDevice->findData(', before_warning)
        self.assertIn('config_get_string(activeConfiguration, "Audio", "MonitoringDeviceId")', before_warning)
        self.assertNotIn('hasFocus()', rejected)
        self.assertNotIn('RefreshPixelviewAudio()', rejected)
        self.assertNotIn('obs_set_audio_monitoring_device', rejected)
        self.assertNotIn('config_set_', rejected)

    def test_meter_inactive_while_master_muted(self):
        audio = (ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        refresh = audio.split('void OBSBasic::RefreshPixelviewAudio()', 1)[1].split('\nbool ', 1)[0]
        self.assertIn('pixelviewMeter->setVisible(!muted);', refresh)
        self.assertIn('pixelviewMeterMuted->setVisible(muted);', refresh)
        self.assertNotIn('setLevels(', audio)
        self.assertNotIn('obs_source_set_volume', audio)

    def test_tooltips_describe_master_mute_without_switching_promise(self):
        audio = (ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        mute_tip = audio.split('pixelviewStreamMute->setToolTip(', 1)[1].split('\n', 1)[0]
        device_tip = audio.split('pixelviewMonitorDevice->setToolTip(', 1)[1].split('\n', 1)[0]
        with self.subTest(tooltip='stream mute'):
            self.assertIn('stream and local monitoring', mute_tip)
            self.assertIn('Unmuting leaves local listening off', mute_tip)
            self.assertNotIn('macOS', mute_tip)
        with self.subTest(tooltip='monitor device'):
            self.assertIn('System default uses the OS default output.', device_tip)
            self.assertNotIn('follows', device_tip)
            self.assertIn('Availability is not a playback test.', device_tip)

    def test_device_enumeration_preserves_missing_and_focused_selection(self):
        path = ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc'
        self.assertTrue(path.exists(), 'Native monitoring device selection is missing')
        audio = path.read_text()
        for text in ('obs_enum_audio_monitoring_devices', 'System default',
                     'hasFocus()', 'view()->isVisible()', 'Unavailable',
                     'MonitoringDeviceName', 'MonitoringDeviceId',
                     'obs_set_audio_monitoring_device', 'config_save_safe',
                     'config_has_user_value', 'config_remove_value', 'pixelview::changeAudio'):
            self.assertIn(text, audio)

if __name__ == '__main__':
    unittest.main()
