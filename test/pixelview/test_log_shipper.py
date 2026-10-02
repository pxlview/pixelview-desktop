"""Log upload to the Pixelview backend: compiled policy and frontend wiring."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class LogShipper(unittest.TestCase):
    def test_compiled_policy(self):
        qt = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal'
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fPIC',
                            '-I'+str(ROOT), '-F'+str(qt/'lib'), '-framework', 'QtCore',
                            '-Wl,-rpath,'+str(qt/'lib'), str(ROOT/'test/pixelview/log_shipper.cpp'),
                            '-o', tmp+'/shipper'], check=True)
            result = subprocess.run([tmp+'/shipper'], check=True, timeout=20,
                                    capture_output=True, text=True)
            self.assertIn('log shipper ok', result.stdout)

    def test_every_logged_line_is_captured(self):
        main = (ROOT/'frontend/obs-main.cpp').read_text()
        body = main.split('static void do_log(', 1)[1].split('\n}\n', 1)[0]
        # Same filter as the log file: repeated lines stay suppressed.
        guarded = body.split('if (!too_many_repeated_entries(logFile, msg, str)) {', 1)[1].split('}', 1)[0]
        self.assertIn('LogStringChunk(logFile, str, log_level);', guarded)
        self.assertIn('pixelview::LogCapture::instance().capture(log_level, str);', guarded)
        # LogStringChunk replaces every newline in str with a terminator, so a
        # capture after it would only see the first line of a multi-line
        # message (an encoder's settings block).
        chunk = main.split('static inline void LogStringChunk(', 1)[1].split('\n}\n', 1)[0]
        self.assertIn('nextLine[0] = 0;', chunk)
        self.assertLess(guarded.index('LogCapture::instance().capture(log_level, str);'),
                        guarded.index('LogStringChunk(logFile, str, log_level);'))

    def test_upload_transport(self):
        inc = (ROOT/'frontend/widgets/OBSBasic_PixelviewLogs.inc').read_text()
        self.assertIn('"PixelviewDiagnostics", "ShareLogs", true', inc)
        self.assertIn('QStringLiteral("/desktop/logs")', inc)
        self.assertIn('"Authorization", "Bearer " + bearer.toUtf8()', inc)
        self.assertIn('"X-Pixelview-Viewer-Token"', inc)
        self.assertIn('pixelviewLogs->cpu = PixelviewLogSysctl("machdep.cpu.brand_string");', inc)
        self.assertIn('pixelviewLogs->hardwareModel = PixelviewLogSysctl("hw.model");', inc)
        self.assertIn('QNetworkRequest::ManualRedirectPolicy', inc)
        self.assertIn('setTransferTimeout(15000)', inc)
        self.assertIn('pixelviewLogs->finished(http, retryAfter, lastUid, credential)', inc)
        # An answer to a request from before sharing was switched off and on is ignored.
        self.assertIn('const qint64 flight = ++pixelviewLogs->flight;', inc)
        self.assertIn('if (!pixelviewLogs || pixelviewLogs->flight != flight) return;', inc)
        # The viewer token only goes to the backend it was issued by.
        self.assertIn('if (QUrl(PixelviewReceiveOrigin()) == pixelviewOrigin) viewer = pixelviewLogViewerToken;', inc)
        # Switching off drops buffered and spooled lines.
        off = inc.split('if (!enabled) {', 1)[1].split('return;', 1)[0]
        self.assertIn('capture.setEnabled(false);', off)
        self.assertIn('pixelviewLogs->disable();', off)
        # Upload outcomes are logged on transitions only: those lines are uploaded too.
        self.assertNotIn('blog(LOG_ERROR', inc)

    def test_menu_switch_receive_context_and_shutdown_spool(self):
        basic = (ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertIn('#include "OBSBasic_PixelviewLogs.inc"', basic)
        init = basic.split('void OBSBasic::InitPixelview()\n{', 1)[1]
        self.assertTrue(init.lstrip().startswith('InitPixelviewLogs();'))
        self.assertIn('"Share Logs with Pixelview Support"', basic)
        self.assertIn('pixelviewLogSharing->setCheckable(true);', basic)
        self.assertIn('&OBSBasic::SetPixelviewLogSharing', basic)
        shutdown = basic.split('bool OBSBasic::PixelviewShutdownReady()\n{', 1)[1].split('StopPixelviewReceive();', 1)[0]
        self.assertIn('PixelviewLogsTick();', shutdown)
        receive = (ROOT/'frontend/widgets/OBSBasic_PixelviewReceive.inc').read_text()
        changed = receive.split('pixelviewReceiver->onChanged = [this] {', 1)[1].split('if (isClosing()', 1)[0]
        # The typed session ID is attached only after the backend accepted the login.
        self.assertIn('const bool signedIn = !pixelviewReceiver->clientToken().isEmpty();', changed)
        self.assertIn('setReceiveContext(signedIn ? pixelviewReceiver->session() : QString(),', changed)
        self.assertIn('signedIn ? pixelviewReceiver->viewer() : QString());', changed)
        self.assertEqual(changed.count('pixelviewReceiver->session()'), 1)
        self.assertIn('if (signedIn) pixelviewLogViewerToken = pixelviewReceiver->clientToken()', changed)
        receiver = (ROOT/'frontend/utility/PixelviewReceiver.cpp').read_text()
        self.assertIn('viewerToken=token;', receiver)
        self.assertIn('viewerToken.clear();', receiver.split('void PixelviewReceiver::stop()', 1)[1].split('}', 1)[0])


if __name__ == '__main__':
    unittest.main()
