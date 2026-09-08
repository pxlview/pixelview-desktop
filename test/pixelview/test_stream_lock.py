"""Compiled actual busy predicate + source integration; no app/hardware run."""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MAIN = ROOT / 'frontend/widgets/OBSBasic.cpp'
AUDIO = ROOT / 'frontend/widgets/OBSBasic_PixelviewAudio.inc'
ENC = ROOT / 'frontend/widgets/OBSBasic_PixelviewEncoding.inc'


def body(text, name):
    match = re.search(r'(?:void|bool) OBSBasic::' + name + r'\([^)]*\)(?: const)?\s*\{', text)
    assert match, f'Missing production method {name}'
    start = match.end()
    depth = 1
    for i in range(start, len(text)):
        depth += (text[i] == '{') - (text[i] == '}')
        if not depth:
            return text[start:i]
    raise AssertionError('Unclosed method')


class StreamLock(unittest.TestCase):
    def test_configuration_handlers_and_refreshes_share_lock_not_audio_actions(self):
        main, audio, encoding = MAIN.read_text(), AUDIO.read_text(), ENC.read_text()
        for text, name, effect in (
            (main, 'SelectPixelviewDevice', 'obs_data_set_string'),
            (main, 'FitPixelviewCapture', 'pixelviewFitPolicy.takeRequest'),
            (main, 'SelectPixelviewFPS', 'config_set_uint'),
            (audio, 'SelectPixelviewMonitorDevice', 'obs_set_audio_monitoring_device'),
            (encoding, 'SavePixelviewEncoding', 'PixelviewEnforceNoBFrames'),
            (encoding, 'AdvancedPixelviewEncoding', 'QDialog dialog')):
            handler = body(text, name)
            self.assertIn('PixelviewConfigurationLocked()', handler, name)
            self.assertLess(handler.index('PixelviewConfigurationLocked()'), handler.index(effect))
        init = body(main, 'InitPixelview')
        for control, effect in (('pixelviewFit', 'pixelviewFitPolicy.requestReset'),
                                ('pixelviewSettings', 'CreatePropertiesWindow')):
            handler = init.split('connect(' + control + ',', 1)[1].split('\n\t});', 1)[0]
            self.assertLess(handler.index('PixelviewConfigurationLocked()'), handler.index(effect))
        refresh = body(main, 'RefreshPixelviewDevices')
        for control in ('pixelviewDevices', 'pixelviewSettings', 'pixelviewFit'):
            self.assertRegex(refresh, control + r'->setEnabled\([^;]*!busy')
        self.assertIn('ui->preview->setEnabled(!busy)', refresh)
        self.assertIn('properties->setEnabled(!busy)', refresh)
        self.assertIn('PixelviewConfigurationLocked()', body(main, 'RefreshPixelviewFPS'))
        self.assertIn('PixelviewConfigurationLocked()', body(encoding, 'RefreshPixelviewEncoding'))
        encoder_init = body(encoding, 'InitPixelviewEncoding')
        for control in ('pixelviewEncoder', 'pixelviewBitrate', 'pixelviewProfile'):
            handler = encoder_init.split('connect(' + control + ',', 1)[1].split('\n\t});', 1)[0]
            self.assertIn('PixelviewConfigurationLocked()', handler, control)
            self.assertLess(handler.index('PixelviewConfigurationLocked()'), handler.index('PixelviewEncoderData'))
        self.assertIn('connect(this, &OBSBasic::StreamingPreparing, &dialog, &QDialog::reject)', body(encoding, 'AdvancedPixelviewEncoding'))
        audio_refresh = body(audio, 'RefreshPixelviewAudio')
        self.assertIn('pixelviewMonitorDevice->setEnabled((!PixelviewConfigurationLocked()', audio_refresh)
        self.assertLess(audio_refresh.index('pixelviewMonitorDevice->setEnabled'), audio_refresh.index('pixelviewMonitorDevice->hasFocus'))
        self.assertNotIn('PixelviewConfigurationLocked()', body(audio, 'ChangePixelviewAudio'))
        self.assertIn('pixelviewStreamMute->setEnabled((pixelviewReceiving || pixelviewPairingDurable) && source != nullptr)', audio_refresh)
        self.assertIn('pixelviewListen->setEnabled((pixelviewReceiving || pixelviewPairingDurable) && source != nullptr && !muted && obs_audio_monitoring_available())', audio_refresh)
        self.assertNotIn('if (PixelviewConfigurationLocked())', audio_refresh)
        streaming = body(main, 'InitPixelviewStreaming')
        for signal in ('StreamingPreparing', 'StreamingStarting', 'StreamingStarted', 'StreamingStopping', 'StreamingStopped'):
            self.assertIn('&OBSBasic::' + signal, streaming)
        self.assertIn('pixelviewStreamingBusy = true', streaming)
        self.assertIn('pixelviewStreamingBusy = withDelay', streaming)
        self.assertNotIn('streamButton->setEnabled', streaming)

    def test_audio_below_preview_and_native_transport_bottom_anchored(self):
        main, audio = MAIN.read_text(), AUDIO.read_text()
        init = body(main, 'InitPixelview')
        self.assertIn('InitPixelviewAudio(ui->previewContainer);', init)
        self.assertNotIn('InitPixelviewAudio(sidebar)', init)
        self.assertLess(init.index('InitPixelviewEncoding(sidebar)'), init.index('sidebarLayout->addStretch(1)'))
        self.assertLess(init.index('sidebarLayout->addStretch(1)'), init.index('InitPixelviewStreaming(sidebar)'))
        self.assertEqual(init.count('sidebarLayout->addStretch(1)'), 1)
        stream = body(main, 'InitPixelviewStreaming')
        self.assertIn('separator->setFrameShape(QFrame::HLine)', stream)
        self.assertLess(stream.index('separator->setFrameShape'), stream.index('sidebar->layout()->addWidget(streamButton)'))
        self.assertIn('auto *panel = new QWidget(previewContainer)', audio)
        self.assertIn('layout->addWidget(panel, 0, Qt::AlignLeft)', audio)
        self.assertEqual(audio.count('new VolumeMeter('), 1)
        self.assertEqual(re.findall(r'(\w+)->setEnabled\(([^;]*)\);', body(audio, 'InitPixelviewAudio')), [('pixelviewMeterMuted', 'false')])

    def test_real_guard_pending_ready_active_and_native_lifecycle(self):
        text = MAIN.read_text()
        self.assertIn('bool OBSBasic::PixelviewSettingsBusy()', text,
                      'All configuration needs a shared native lifecycle guard')
        predicate = body(text, 'PixelviewSettingsBusy')
        code = '''#include "frontend/utility/PixelviewEncoding.hpp"
#include <cassert>
int main() {
 bool pixelviewStreamingBusy = false, videoActive = false;
 auto obs_video_active = [&] { return videoActive; };
 struct Output { bool active = false; bool Active() const { return active; } } output;
 auto *outputHandler = &output;
 std::shared_future<void> setupStreamingGuard;
 auto busy = [&] { PREDICATE };
 assert(!busy());
 std::promise<void> preparing;
 setupStreamingGuard = preparing.get_future().share();
 int sourceWrites = 0, saves = 0, resets = 0;
 auto change = [&] { if (busy()) return; ++sourceWrites; ++saves; ++resets; };
 assert(busy()); change();
 assert(sourceWrites == 0 && saves == 0 && resets == 0);
 preparing.set_value(); assert(!busy());
 pixelviewStreamingBusy = true; assert(busy()); change();
 assert(sourceWrites == 0 && saves == 0 && resets == 0);
 pixelviewStreamingBusy = false;
 videoActive = true; assert(busy()); change();
 videoActive = false; output.active = true; assert(busy()); change();
 assert(sourceWrites == 0 && saves == 0 && resets == 0);
 outputHandler = nullptr; assert(!busy()); change();
 assert(sourceWrites == 1 && saves == 1 && resets == 1);
}'''.replace('PREDICATE', predicate)
        with tempfile.TemporaryDirectory() as temp:
            src = pathlib.Path(temp) / 'guard.cpp'
            binary = pathlib.Path(temp) / 'guard'
            src.write_text(code)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(src), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
