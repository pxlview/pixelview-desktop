"""Admin remote control (DESKTOP_CONTROL) source contracts; no socket or hardware execution."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONTROL = ROOT / 'frontend/widgets/OBSBasic_PixelviewControl.inc'


def body(source, signature):
    return source.split(signature, 1)[1].split('\n}\n', 1)[0]


class RemoteControl(unittest.TestCase):
    def setUp(self):
        self.control = CONTROL.read_text()
        self.run_body = body(self.control, 'QString OBSBasic::RunPixelviewControl(')

    def test_only_a_ready_authenticated_socket_dispatches_commands(self):
        desktop = (ROOT / 'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        handler = desktop.split('pixelviewDesktop->message=[this](QByteArray body){', 1)[1].split('\n };', 1)[0]
        closing = handler.split('if(pixelviewClosingSocket) {', 1)[1].split('\n  }', 1)[0]
        self.assertNotIn('DESKTOP_CONTROL', closing)
        self.assertIn('else if(mutation=="DESKTOP_CONTROL" && pixelviewLease.ready) HandlePixelviewControl(', handler)
        self.assertIn('#include "OBSBasic_PixelviewControl.inc"', (ROOT / 'frontend/widgets/OBSBasic.cpp').read_text())

    def test_result_echoes_request_and_carries_state(self):
        handle = body(self.control, 'void OBSBasic::HandlePixelviewControl(')
        self.assertIn('requestId.size() > 64', handle)
        self.assertIn('QTimer::singleShot(0, this', handle)
        self.assertIn('"DESKTOP_CONTROL_RESULT"', handle)
        self.assertIn('{"request_id", requestId}, {"ok", error.isEmpty()}, {"state", PixelviewControlState()}', handle)
        self.assertIn('if (pixelviewLease.ready) pixelviewLease.send(', handle)

    def test_commands_use_the_sidebar_paths_and_lock(self):
        run = self.run_body
        for action in ('get_state', 'start', 'stop', 'set_mute', 'select_device', 'set_capture', 'fit',
                       'set_fps', 'set_encoder', 'set_bitrate', 'set_profile'):
            self.assertIn(f'"{action}"', run, action)
        self.assertIn('StartStreaming();', run)
        # No remote "stream a blank screen?" confirmation: start needs a selected device.
        start = run.split('if (action == "start") {', 1)[1].split('if (action == "stop")', 1)[0]
        self.assertLess(start.index('if (!PixelviewCaptureSelected()) return'), start.index('StartStreaming();'))
        self.assertIn('PixelviewCaptureSelected()},', body(self.control, 'QJsonObject OBSBasic::PixelviewControlState()'))
        self.assertIn('StopStreaming();', run)
        self.assertIn('SelectPixelviewDevice(index);', run)
        self.assertIn('SelectPixelviewFPS(index);', run)
        self.assertIn('SavePixelviewEncoding(', run)
        self.assertIn('ChangePixelviewAudio(false, muted);', run)
        self.assertIn('pixelview::quickBitrateKbps(', run)
        # Receiving and pairing gate every command; sender configuration uses the sidebar lock.
        self.assertLess(run.index('if (pixelviewReceiving)'), run.index('if (action == "start")'))
        lock = run.index('if (PixelviewConfigurationLocked())')
        for action in ('select_device', 'set_capture', 'fit', 'set_fps', 'set_encoder', 'set_bitrate'):
            self.assertLess(lock, run.index(f'action == "{action}"'), action)
        # Mute stays available while streaming, like the local checkbox.
        self.assertLess(run.index('action == "set_mute"'), lock)
        self.assertIn('if (properties) return', run)
        self.assertTrue(run.rstrip().endswith('return QStringLiteral("Unknown command.");'))

    def test_capture_settings_are_limited_to_offered_native_values(self):
        remote = body(self.control, 'bool PixelviewRemoteProperty(')
        self.assertIn('"device_hash"', remote)
        self.assertIn('obs_property_visible(prop)', remote)
        self.assertIn('OBS_PROPERTY_BOOL', remote)
        self.assertIn('OBS_PROPERTY_LIST', remote)
        capture = self.run_body.split('if (action == "set_capture") {', 1)[1].split('if (action == "fit")', 1)[0]
        self.assertIn('!obs_property_list_item_disabled(prop, i) && PixelviewListItemValue(prop, i) == value', capture)
        self.assertIn('obs_property_enabled(prop)', capture)
        self.assertLess(capture.index('obs_property_modified(prop, settings);'), capture.index('obs_source_update(source, settings);'))
        self.assertIn('SaveProject();', capture)

    def test_remote_failures_are_returned_not_shown(self):
        self.assertNotIn('QMessageBox', self.run_body)
        self.assertIn('QScopedValueRollback<QString *> quiet(pixelviewRemoteError, &error);', self.run_body)
        warn = body(self.control, 'void OBSBasic::PixelviewWarn(')
        self.assertLess(warn.index('if (pixelviewRemoteError)'), warn.index('QMessageBox::warning'))
        sources = {
            'frontend/widgets/OBSBasic_PixelviewEncoding.inc': 'bool OBSBasic::SavePixelviewEncoding(',
            'frontend/widgets/OBSBasic.cpp': 'void OBSBasic::SelectPixelviewFPS(int index)',
            'frontend/widgets/OBSBasic_PixelviewAudio.inc': 'void OBSBasic::ChangePixelviewAudio(',
        }
        for path, signature in sources.items():
            function = body((ROOT / path).read_text(), signature)
            self.assertIn('PixelviewWarn(', function, path)
            self.assertNotIn('QMessageBox::warning', function, path)
        select = body((ROOT / 'frontend/widgets/OBSBasic.cpp').read_text(), 'void OBSBasic::SelectPixelviewDevice(')
        self.assertNotIn('QMessageBox::warning', select)

    def test_state_reports_every_remote_setting(self):
        state = body(self.control, 'QJsonObject OBSBasic::PixelviewControlState()')
        for key in ('"version", 1', '"lock_reason"', '"stream"', '"capture"', '"encoding"', '"audio"',
                    '"can_start"', '"can_stop"', '"devices"', '"properties"', '"encoders"', '"profiles"',
                    '"bitrate_kbps"', '"fps_options"', '"muted"'):
            self.assertIn(key, state, key)
        self.assertEqual(re.findall(r'"bitrate_mbps_(?:min|max)", (\d+)', state), ['1', '12'])


if __name__ == '__main__':
    unittest.main()
