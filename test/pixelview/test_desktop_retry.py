"""Offline compiled production reconnect policy (no Keychain/network/app build)."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class DesktopRetry(unittest.TestCase):
    def test_frontend_recovery_wiring(self):
        inc = (ROOT/'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        stream = (ROOT/'frontend/widgets/OBSBasic_Streaming.cpp').read_text()
        for setting in ('Reconnect', 'RetryDelay', 'MaxRetries'):
            self.assertIn('"Output", "'+setting+'"', inc)
        self.assertIn('pixelviewLease.takeRetry(now,', inc)
        self.assertIn('pixelview::Desktop::transientClose(code)', inc)
        self.assertIn('pixelviewLease.outputStarted()', inc)
        self.assertIn('Reconnecting', inc)
        self.assertIn('ui->statusbar->showMessage', inc)
        self.assertIn('pixelviewLease.acceptSetup(attempt,', stream)
        self.assertIn('[this, attempt]', stream)
        for method in ('StopStreaming', 'ForceStopStreaming'):
            body = stream.split('void OBSBasic::'+method+'()')[1].split('\n}')[0]
            self.assertIn('CancelPixelviewStart()', body)
        action = stream.split('void OBSBasic::StreamActionTriggered()')[1]
        self.assertIn('pixelviewLease.intent', action)
        self.assertIn('OBS_OUTPUT_DISCONNECTED || code == OBS_OUTPUT_CONNECT_FAILED', stream)
        self.assertNotIn('reconnecting without resuming', inc)
        # Raw libobs retries would reuse the old service instead of acquiring authority.
        advanced = (ROOT/'frontend/utility/AdvancedOutput.cpp').read_text()
        self.assertIn('"whip_custom") == 0) reconnect = false', advanced)

    def test_compiled_frontend_drain(self):
        inc = (ROOT/'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        methods = inc[inc.index('void OBSBasic::PixelviewOutputStopped()'):inc.index('bool OBSBasic::SavePixelviewIdentity()')]
        halt = inc[inc.index(' pixelviewLease.error='):inc.index(' pixelviewLease.send=')] + inc[inc.index(' pixelviewLease.halt='):inc.index(' pixelviewLease.publish=')]
        disconnected = inc[inc.index(' pixelviewDesktop->disconnected='):inc.index(' pixelviewDesktop->paired=')]
        message = inc[inc.index(' pixelviewDesktop->message='):inc.index(' pixelviewDesktop->disconnected=')]
        publish_gate = inc[inc.index(' pixelviewLease.publish='):inc.index('  OBSDataAutoRelease settings=')] + '(void)endpoint;(void)bearer;};\n'
        stream = (ROOT/'frontend/widgets/OBSBasic_Streaming.cpp').read_text()
        callback = stream[stream.index('\tconst auto attempt ='):stream.index('\n\tsetupStreamingGuard =')]
        methods += stream[stream.index('void OBSBasic::StreamDelayStarting('):stream.index('void OBSBasic::StreamDelayStopping(')]
        methods += '\nstd::function<void(bool)> OBSBasic::MakeSetup(){\n' + callback + '\nreturn finish_stream_setup;\n}\n'
        started = inc.split(' connect(this,&OBSBasic::StreamingStarted,this,', 1)[1].split('\n pixelviewOrigin=', 1)[0].rsplit(');', 1)[0]
        call = 'handler(withDelay);' if '](bool' in started else '(void)withDelay;handler();'
        methods += '\nvoid OBSBasic::OutputStartedSignal(bool withDelay){auto handler=' + started + ';' + call + '}\n'
        methods += '\nvoid OBSBasic::bind(){\n' + halt + disconnected + message + publish_gate + '\npixelviewLease.monotonic=[this]{return pixelviewClock.elapsed();};\n}\n'
        basic = (ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        # Execute the actual accepted-close gate, stopping at native teardown.
        methods += basic[basic.index('void OBSBasic::closeWindow()'):basic.index('\n\t/* While closing,', basic.index('void OBSBasic::closeWindow()'))] + '\n++teardowns;\n}\n'
        connect = inc[inc.index('void OBSBasic::ConnectPixelviewDesktop()'):inc.index(' QString token=')]
        methods += connect + '\n++authentications;\n}\n'
        methods += inc[inc.index('bool OBSBasic::PixelviewLeaseValid()'):inc.index('void OBSBasic::PixelviewOutputStopped()')]
        methods += stream[stream.index('void OBSBasic::StartStreaming()'):stream.index('\n\tif (!pixelviewDesktop && auth')] + '\n++nativePreparations;\n}\n'
        for name, marker, end in (
            ('Watchdog', 'connect(pixelviewWatchdog,&QTimer::timeout,this,[this]{', ' }); pixelviewWatchdog->start();'),
            ('Heartbeat', 'connect(pixelviewHeartbeat,&QTimer::timeout,this,[this]{', '\n });')):
            body = inc.split(marker, 1)[1].split(end, 1)[0]
            methods += '\nvoid OBSBasic::' + name + '(){' + body + '\n}\n'
        source = (ROOT/'test/pixelview/desktop_retry_frontend.cpp').read_text().replace('// PRODUCTION_METHODS', methods)
        self.compile_run(source)

    def compile_run(self, source):
        qt = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal'
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp)/'frontend.cpp'
            src.write_text(source)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fPIC',
                            '-I'+str(ROOT), '-F'+str(qt/'lib'), '-framework', 'QtCore',
                            '-Wl,-rpath,'+str(qt/'lib'), str(src), '-o', tmp+'/test'], check=True)
            subprocess.run([tmp+'/test'], check=True, timeout=10)

    def test_compiled_retry_policy(self):
        qt = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal'
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fPIC',
                            '-I'+str(ROOT), '-F'+str(qt/'lib'), '-framework', 'QtCore',
                            '-Wl,-rpath,'+str(qt/'lib'), str(ROOT/'test/pixelview/desktop_retry.cpp'),
                            '-o', tmp+'/retry'], check=True)
            subprocess.run([tmp+'/retry'], check=True, timeout=10)

if __name__ == '__main__':
    unittest.main()
