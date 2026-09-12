"""Source contracts: remembered send/receive tab and a bounded, quit-safe main-window close."""
import pathlib
import re
import unittest
from test_stream_lock import body
ROOT = pathlib.Path(__file__).resolve().parents[2]
INC = ROOT / 'frontend/widgets/OBSBasic_PixelviewReceive.inc'
BASIC = ROOT / 'frontend/widgets/OBSBasic.cpp'


class ModePersistence(unittest.TestCase):
    def test_mode_saved_only_by_explicit_selection(self):
        inc = INC.read_text()
        select = body(inc, 'SelectPixelviewMode')
        self.assertIn('config_set_string(App()->GetUserConfig(), "PixelviewReceive", "Mode", pixelviewReceiving ? "receiving" : "sending")', select)
        self.assertIn('config_save_safe(App()->GetUserConfig(), "tmp", nullptr)', select)
        # The write follows the committed switch, never a rejected/busy/failed one.
        self.assertLess(select.index('pixelviewReceiving = index == 1;'), select.index('"Mode"'))
        for name in ('StopPixelviewReceive', 'RefreshPixelviewModes'):
            self.assertNotIn('"Mode"', body(inc, name), f'{name} must not rewrite the remembered tab')
        self.assertNotIn('"Mode"', body(BASIC.read_text(), 'closeWindow'))

    def test_mode_restored_before_deep_links_drain(self):
        init = body(BASIC.read_text(), 'InitPixelview')
        restore = 'if (savedMode && strcmp(savedMode, "receiving") == 0) SelectPixelviewMode(1);'
        self.assertIn('config_get_string(App()->GetUserConfig(), "PixelviewReceive", "Mode")', init)
        self.assertIn(restore, init)
        self.assertLess(init.index('InitPixelviewReceive(sharedSidebar)'), init.index(restore))
        self.assertLess(init.index('deepLinkInbox().attach'), init.index(restore))
        # Before the periodic device refresh starts: the startup harnesses slice from there.
        self.assertLess(init.index(restore), init.index('pixelviewRefreshTimer = new QTimer(this);'))
        main = (ROOT / 'frontend/obs-main.cpp').read_text()
        self.assertLess(main.index('program.OBSInit()'), main.index('deepLinkInbox().ready()'))


class BoundedClose(unittest.TestCase):
    def test_close_event_keeps_window_alive_until_native_teardown_is_ready(self):
        text = BASIC.read_text()
        close_event = body(text, 'closeEvent')
        gate = close_event.index('if (!PixelviewShutdownReady())')
        # WA_DeleteOnClose: an accepted close destroys the window, so the wait
        # must happen before QWidget::closeEvent accepts it.
        self.assertLess(gate, close_event.index('QWidget::closeEvent(event);'))
        self.assertLess(close_event.index('shouldPromptForClose()'), gate)
        waiting = close_event[gate:close_event.index('QWidget::closeEvent(event);')]
        self.assertIn('event->ignore();', waiting)
        self.assertIn('QTimer::singleShot(100, this, &OBSBasic::close);', waiting)
        self.assertIn('WA_DeleteOnClose', (ROOT / 'frontend/OBSApp.cpp').read_text())

    def test_native_shutdown_wait_is_bounded_and_forced_on_quit(self):
        text = BASIC.read_text()
        ready = body(text, 'PixelviewShutdownReady')
        self.assertIn('PIXELVIEW_SHUTDOWN_WAIT_MS', ready)
        self.assertIn('pixelviewClock.elapsed() < pixelviewShutdownDeadline', ready)
        self.assertIn('!pixelviewForceClose &&', ready)
        self.assertIn('obs_output_force_stop(outputHandler->streamOutput)', ready)
        self.assertIn('pixelviewLease.fail("Stopping before shutdown.")', ready)
        self.assertLess(ready.index('return false;'), ready.index('obs_output_force_stop'))
        wait = re.search(r'#define PIXELVIEW_SHUTDOWN_WAIT_MS (\d+)', text)
        self.assertTrue(wait and 1000 <= int(wait.group(1)) <= 30000)
        close = body(text, 'closeWindow')
        self.assertLess(close.index('PixelviewShutdownReady()'), close.index('ClearSceneData();'))
        self.assertEqual(close.count('ClearSceneData();'), 1)

    def test_about_to_quit_finishes_close_on_every_platform(self):
        text = BASIC.read_text()
        start = text.index('&OBSApp::aboutToQuit')
        self.assertNotIn('#ifndef __APPLE__', text[max(0, start - 160):start])
        handler = text[start:start + 120]
        self.assertIn('pixelviewForceClose = true;', handler)
        self.assertIn('closeWindow();', handler)

if __name__ == '__main__':
    unittest.main()
